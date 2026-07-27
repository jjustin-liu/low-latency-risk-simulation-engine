// Limit-order-book matching engine, built for hot-path determinism.
//
// Complexity (the honest version -- say it this way in an interview):
//   * add-limit (resting), cancel, best-bid/ask read  -> O(1)
//   * a marketable order                               -> O(price levels swept)
// achieved with:
//   * INTEGER TICK prices and a DIRECT-INDEXED array of price levels (no tree,
//     no hashing on the hot path);
//   * an intrusive FIFO doubly-linked list per level (price-time priority);
//   * a preallocated slab pool of order nodes with a free list -> ZERO hot-path
//     allocation; order handles carry a generation counter so a stale cancel of
//     a filled/reused slot is safely ignored (ABA-proof);
//   * occupancy BITSETS with hardware bit-scan (ctz/clz) to find the next best
//     price level in O(1) amortized.
//
// Fully deterministic: integer arithmetic only, FIFO within a level, so an
// identical order stream produces byte-identical fills (verified by a replay test).
#ifndef RISKSIM_LOB_ORDER_BOOK_HPP
#define RISKSIM_LOB_ORDER_BOOK_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace risksim::lob {

using Price = std::int32_t;    // tick index in [0, num_levels)
using Qty = std::int64_t;      // signed so partial arithmetic never underflows silently
using OrderId = std::uint64_t; // handle: (generation << 32) | slot; 0 == "none"

enum class Side : std::uint8_t { Buy, Sell };

struct Fill {
    OrderId maker;      // resting order that was hit
    OrderId taker;      // aggressor
    Price price;        // execution price (the maker's price -- price improvement for the taker)
    Qty qty;
    Side aggressor;
};

class OrderBook {
public:
    OrderBook(std::size_t num_levels, std::size_t max_orders)
        : num_levels_(num_levels),
          bid_levels_(num_levels),
          ask_levels_(num_levels),
          bid_occ_((num_levels + 63) / 64, 0),
          ask_occ_((num_levels + 63) / 64, 0),
          best_bid_(-1),
          best_ask_(static_cast<Price>(num_levels)) {
        pool_.resize(max_orders);
        free_.reserve(max_orders);
        for (std::size_t i = max_orders; i-- > 0;) free_.push_back(static_cast<std::uint32_t>(i));
    }

    // Submit a limit order. Crosses against the book first (appending fills),
    // then rests any remainder. Returns the resting OrderId, or 0 if the order
    // was fully filled (or rejected).
    OrderId add_limit(Side side, Price price, Qty qty, std::vector<Fill>& fills) {
        if (qty <= 0 || price < 0 || price >= static_cast<Price>(num_levels_)) return 0;
        const std::uint32_t slot = alloc();
        if (slot == kNil) return 0;  // capacity exhausted
        Node& n = pool_[slot];
        const OrderId id = make_id(slot, n.gen);
        n.id = id;
        n.side = side;
        n.price = price;
        n.next = n.prev = kNil;

        const Qty remaining = match(side, price, qty, id, fills);
        if (remaining > 0) {
            n.qty = remaining;
            rest(slot);
            return id;
        }
        free_slot(slot);
        return 0;
    }

    // Marketable order: sweep the book with no price limit; no remainder rests.
    // Returns the unfilled quantity (0 if fully filled).
    Qty add_market(Side side, Qty qty, std::vector<Fill>& fills) {
        if (qty <= 0) return 0;
        const Price limit = side == Side::Buy ? static_cast<Price>(num_levels_) - 1 : 0;
        return match(side, limit, qty, kMarketFlag | (++taker_seq_), fills);
    }

    // Cancel a resting order by handle. Returns false if it is not live (already
    // filled, already cancelled, or a stale handle to a reused slot).
    bool cancel(OrderId id) {
        const std::uint32_t slot = static_cast<std::uint32_t>(id & 0xFFFFFFFFu);
        const std::uint32_t gen = static_cast<std::uint32_t>(id >> 32);
        if (slot >= pool_.size()) return false;
        Node& n = pool_[slot];
        if (!n.live || n.gen != gen) return false;
        unlink(slot);
        free_slot(slot);
        return true;
    }

    // ---- Queries (all O(1)) ----
    [[nodiscard]] std::optional<Price> best_bid() const {
        return best_bid_ >= 0 ? std::optional<Price>(best_bid_) : std::nullopt;
    }
    [[nodiscard]] std::optional<Price> best_ask() const {
        return best_ask_ < static_cast<Price>(num_levels_) ? std::optional<Price>(best_ask_)
                                                           : std::nullopt;
    }
    [[nodiscard]] Qty qty_at(Side side, Price price) const {
        if (price < 0 || price >= static_cast<Price>(num_levels_)) return 0;
        return (side == Side::Buy ? bid_levels_ : ask_levels_)[static_cast<std::size_t>(price)].total;
    }
    [[nodiscard]] std::size_t live_orders() const { return pool_.size() - free_.size(); }

private:
    static constexpr std::uint32_t kNil = 0xFFFFFFFFu;
    static constexpr OrderId kMarketFlag = 1ULL << 63;

    struct Node {
        OrderId id = 0;
        Qty qty = 0;
        Price price = 0;
        Side side = Side::Buy;
        bool live = false;
        std::uint32_t gen = 0;
        std::uint32_t next = kNil;
        std::uint32_t prev = kNil;
    };
    struct Level {
        Qty total = 0;
        std::uint32_t head = kNil;
        std::uint32_t tail = kNil;
        std::uint32_t count = 0;
    };

    static OrderId make_id(std::uint32_t slot, std::uint32_t gen) {
        return (static_cast<OrderId>(gen) << 32) | slot;
    }

    std::uint32_t alloc() {
        if (free_.empty()) return kNil;
        const std::uint32_t slot = free_.back();
        free_.pop_back();
        ++pool_[slot].gen;  // new generation invalidates stale handles to this slot
        pool_[slot].live = true;
        return slot;
    }
    void free_slot(std::uint32_t slot) {
        pool_[slot].live = false;
        free_.push_back(slot);
    }

    std::vector<Level>& levels(Side s) { return s == Side::Buy ? bid_levels_ : ask_levels_; }

    // Consume the opposite side up to `limit`, appending fills; return unfilled qty.
    Qty match(Side side, Price limit, Qty qty, OrderId taker, std::vector<Fill>& fills) {
        if (side == Side::Buy) {
            while (qty > 0 && best_ask_ < static_cast<Price>(num_levels_) && best_ask_ <= limit) {
                Level& lv = ask_levels_[static_cast<std::size_t>(best_ask_)];
                consume(lv, best_ask_, Side::Buy, qty, taker, fills);
                if (lv.count == 0) {
                    clear_bit(ask_occ_, best_ask_);
                    best_ask_ = lowest_set_ge(ask_occ_, best_ask_ + 1);
                }
            }
        } else {
            while (qty > 0 && best_bid_ >= 0 && best_bid_ >= limit) {
                Level& lv = bid_levels_[static_cast<std::size_t>(best_bid_)];
                consume(lv, best_bid_, Side::Sell, qty, taker, fills);
                if (lv.count == 0) {
                    clear_bit(bid_occ_, best_bid_);
                    best_bid_ = highest_set_le(bid_occ_, best_bid_ - 1);
                }
            }
        }
        return qty;
    }

    // Walk one price level FIFO, filling against the taker; mutates `qty`.
    void consume(Level& lv, Price price, Side aggressor, Qty& qty, OrderId taker,
                 std::vector<Fill>& fills) {
        while (qty > 0 && lv.head != kNil) {
            Node& m = pool_[lv.head];
            const Qty traded = std::min(qty, m.qty);
            fills.push_back({m.id, taker, price, traded, aggressor});
            qty -= traded;
            m.qty -= traded;
            lv.total -= traded;
            if (m.qty == 0) {
                const std::uint32_t h = lv.head;
                lv.head = m.next;
                if (lv.head != kNil)
                    pool_[lv.head].prev = kNil;
                else
                    lv.tail = kNil;
                --lv.count;
                free_slot(h);
            }
        }
    }

    // Insert a node at the tail of its price level (time priority) and update best.
    void rest(std::uint32_t slot) {
        Node& n = pool_[slot];
        Level& lv = levels(n.side)[static_cast<std::size_t>(n.price)];
        n.next = kNil;
        n.prev = lv.tail;
        if (lv.tail != kNil)
            pool_[lv.tail].next = slot;
        else
            lv.head = slot;
        lv.tail = slot;
        lv.total += n.qty;
        ++lv.count;
        if (n.side == Side::Buy) {
            set_bit(bid_occ_, n.price);
            if (n.price > best_bid_) best_bid_ = n.price;
        } else {
            set_bit(ask_occ_, n.price);
            if (n.price < best_ask_) best_ask_ = n.price;
        }
    }

    // Remove a live resting node from its level and fix up best if it emptied.
    void unlink(std::uint32_t slot) {
        Node& n = pool_[slot];
        Level& lv = levels(n.side)[static_cast<std::size_t>(n.price)];
        if (n.prev != kNil)
            pool_[n.prev].next = n.next;
        else
            lv.head = n.next;
        if (n.next != kNil)
            pool_[n.next].prev = n.prev;
        else
            lv.tail = n.prev;
        lv.total -= n.qty;
        --lv.count;
        if (lv.count == 0) {
            if (n.side == Side::Buy) {
                clear_bit(bid_occ_, n.price);
                if (n.price == best_bid_) best_bid_ = highest_set_le(bid_occ_, best_bid_ - 1);
            } else {
                clear_bit(ask_occ_, n.price);
                if (n.price == best_ask_) best_ask_ = lowest_set_ge(ask_occ_, best_ask_ + 1);
            }
        }
    }

    // ---- Occupancy bitset helpers ----
    static void set_bit(std::vector<std::uint64_t>& b, Price i) {
        b[static_cast<std::size_t>(i) >> 6] |= (1ULL << (static_cast<unsigned>(i) & 63u));
    }
    static void clear_bit(std::vector<std::uint64_t>& b, Price i) {
        b[static_cast<std::size_t>(i) >> 6] &= ~(1ULL << (static_cast<unsigned>(i) & 63u));
    }
    // Lowest occupied tick >= from, else num_levels_ (none).
    Price lowest_set_ge(const std::vector<std::uint64_t>& b, Price from) const {
        if (from < 0) from = 0;
        if (static_cast<std::size_t>(from) >= num_levels_) return static_cast<Price>(num_levels_);
        std::size_t w = static_cast<std::size_t>(from) >> 6;
        std::uint64_t word = b[w] & (~0ULL << (static_cast<unsigned>(from) & 63u));
        for (;;) {
            if (word) {
                const Price idx =
                    static_cast<Price>(w * 64 + static_cast<std::size_t>(__builtin_ctzll(word)));
                return idx < static_cast<Price>(num_levels_) ? idx : static_cast<Price>(num_levels_);
            }
            if (++w >= b.size()) return static_cast<Price>(num_levels_);
            word = b[w];
        }
    }
    // Highest occupied tick <= from, else -1 (none).
    Price highest_set_le(const std::vector<std::uint64_t>& b, Price from) const {
        if (from >= static_cast<Price>(num_levels_)) from = static_cast<Price>(num_levels_) - 1;
        if (from < 0) return -1;
        long w = static_cast<long>(from) >> 6;
        const unsigned off = static_cast<unsigned>(from) & 63u;
        std::uint64_t word =
            b[static_cast<std::size_t>(w)] & (off == 63 ? ~0ULL : ((1ULL << (off + 1)) - 1));
        for (;;) {
            if (word) {
                return static_cast<Price>(w * 64 + 63 - __builtin_clzll(word));
            }
            if (--w < 0) return -1;
            word = b[static_cast<std::size_t>(w)];
        }
    }

    std::size_t num_levels_;
    std::vector<Node> pool_;
    std::vector<std::uint32_t> free_;
    std::vector<Level> bid_levels_;
    std::vector<Level> ask_levels_;
    std::vector<std::uint64_t> bid_occ_;
    std::vector<std::uint64_t> ask_occ_;
    Price best_bid_;
    Price best_ask_;
    OrderId taker_seq_ = 0;
};

}  // namespace risksim::lob

#endif  // RISKSIM_LOB_ORDER_BOOK_HPP
