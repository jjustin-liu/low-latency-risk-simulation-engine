#include <gtest/gtest.h>

#include <vector>

#include "risksim/lob/order_book.hpp"
#include "risksim/rng.hpp"

using namespace risksim::lob;
using risksim::Philox;

namespace {
OrderBook make_book() { return OrderBook(/*num_levels=*/1024, /*max_orders=*/100000); }
}  // namespace

TEST(OrderBook, RestingAndBestQuotes) {
    OrderBook b = make_book();
    std::vector<Fill> fills;
    b.add_limit(Side::Buy, 100, 10, fills);
    b.add_limit(Side::Buy, 99, 5, fills);
    b.add_limit(Side::Sell, 105, 7, fills);
    b.add_limit(Side::Sell, 106, 3, fills);
    EXPECT_TRUE(fills.empty());  // no crossing
    EXPECT_EQ(b.best_bid().value(), 100);
    EXPECT_EQ(b.best_ask().value(), 105);
    EXPECT_EQ(b.qty_at(Side::Buy, 100), 10);
    EXPECT_EQ(b.qty_at(Side::Sell, 105), 7);
    EXPECT_EQ(b.live_orders(), 4u);
}

TEST(OrderBook, HandComputedCrossingFills) {
    // Book: two asks at 50 (A=10 first, B=5 second), one ask at 51 (C=20).
    OrderBook b = make_book();
    std::vector<Fill> f;
    const OrderId a = b.add_limit(Side::Sell, 50, 10, f);
    const OrderId bb = b.add_limit(Side::Sell, 50, 5, f);
    b.add_limit(Side::Sell, 51, 20, f);
    ASSERT_TRUE(f.empty());

    // Aggressive buy for 12 @ limit 52: fills A fully (10), then B for 2 (FIFO).
    f.clear();
    const OrderId taker = b.add_limit(Side::Buy, 52, 12, f);
    EXPECT_EQ(taker, 0u);  // fully filled, nothing rests
    ASSERT_EQ(f.size(), 2u);
    EXPECT_EQ(f[0].maker, a);
    EXPECT_EQ(f[0].price, 50);
    EXPECT_EQ(f[0].qty, 10);
    EXPECT_EQ(f[0].aggressor, Side::Buy);
    EXPECT_EQ(f[1].maker, bb);
    EXPECT_EQ(f[1].price, 50);
    EXPECT_EQ(f[1].qty, 2);

    // Best ask stays 50 (B has 3 left); depth updated.
    EXPECT_EQ(b.best_ask().value(), 50);
    EXPECT_EQ(b.qty_at(Side::Sell, 50), 3);
    EXPECT_EQ(b.qty_at(Side::Sell, 51), 20);
    EXPECT_FALSE(b.best_bid().has_value());
}

TEST(OrderBook, MarketOrderSweepsLevelsAndReportsUnfilled) {
    OrderBook b = make_book();
    std::vector<Fill> f;
    b.add_limit(Side::Sell, 50, 3, f);
    b.add_limit(Side::Sell, 51, 20, f);
    f.clear();

    // Market buy 30: takes 3 @50, 20 @51, 7 unfilled (book exhausted).
    const Qty unfilled = b.add_market(Side::Buy, 30, f);
    EXPECT_EQ(unfilled, 7);
    ASSERT_EQ(f.size(), 2u);
    EXPECT_EQ(f[0].price, 50);
    EXPECT_EQ(f[0].qty, 3);
    EXPECT_EQ(f[1].price, 51);
    EXPECT_EQ(f[1].qty, 20);
    EXPECT_FALSE(b.best_ask().has_value());  // book empty on the ask side
}

TEST(OrderBook, CrossingLimitRestsRemainder) {
    OrderBook b = make_book();
    std::vector<Fill> f;
    b.add_limit(Side::Sell, 50, 4, f);
    f.clear();
    // Buy 10 @ 50: fills 4, rests 6 at bid 50 -> best bid becomes 50.
    const OrderId id = b.add_limit(Side::Buy, 50, 10, f);
    EXPECT_NE(id, 0u);
    ASSERT_EQ(f.size(), 1u);
    EXPECT_EQ(f[0].qty, 4);
    EXPECT_EQ(b.best_bid().value(), 50);
    EXPECT_EQ(b.qty_at(Side::Buy, 50), 6);
    EXPECT_FALSE(b.best_ask().has_value());
}

TEST(OrderBook, CancelUpdatesBestAndRejectsStale) {
    OrderBook b = make_book();
    std::vector<Fill> f;
    const OrderId top = b.add_limit(Side::Buy, 100, 10, f);
    b.add_limit(Side::Buy, 99, 5, f);
    EXPECT_EQ(b.best_bid().value(), 100);

    EXPECT_TRUE(b.cancel(top));       // cancel the top-of-book bid
    EXPECT_EQ(b.best_bid().value(), 99);  // best rolls down to the next level
    EXPECT_FALSE(b.cancel(top));      // double cancel -> false
}

TEST(OrderBook, GenerationHandlePreventsAbaCancel) {
    // A filled order's slot gets reused; a stale cancel of the old handle must
    // NOT cancel the new order occupying that slot.
    OrderBook b = make_book();
    std::vector<Fill> f;
    const OrderId first = b.add_limit(Side::Buy, 100, 5, f);
    b.add_market(Side::Sell, 5, f);   // fully fills `first`, freeing its slot
    EXPECT_FALSE(b.cancel(first));    // already filled

    // New order likely reuses the freed slot but with a bumped generation.
    const OrderId second = b.add_limit(Side::Buy, 100, 8, f);
    EXPECT_NE(second, first);
    EXPECT_FALSE(b.cancel(first));    // stale handle rejected
    EXPECT_TRUE(b.cancel(second));    // real handle works
}

TEST(OrderBook, DeterministicReplayByteIdentical) {
    // Same pseudo-random-but-deterministic order stream twice -> identical fills.
    auto run = []() {
        OrderBook b(2048, 200000);
        std::vector<Fill> fills;
        std::vector<OrderId> live;
        const Philox rng(2024);
        std::uint64_t k = 0;
        for (int i = 0; i < 40000; ++i) {
            const double u = rng.uniform(k++);
            if (u < 0.15 && !live.empty()) {
                const auto idx = static_cast<std::size_t>(rng.uniform(k++) * static_cast<double>(live.size()));
                b.cancel(live[idx]);
                live[idx] = live.back();
                live.pop_back();
            } else {
                const Side s = rng.uniform(k++) < 0.5 ? Side::Buy : Side::Sell;
                const Price p = 900 + static_cast<Price>(rng.uniform(k++) * 200.0);
                const Qty q = 1 + static_cast<Qty>(rng.uniform(k++) * 50.0);
                const OrderId id = b.add_limit(s, p, q, fills);
                if (id != 0) live.push_back(id);
            }
        }
        return fills;
    };
    const std::vector<Fill> a = run();
    const std::vector<Fill> b = run();
    ASSERT_EQ(a.size(), b.size());
    EXPECT_GT(a.size(), 100u);  // the stream actually trades
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].maker, b[i].maker);
        EXPECT_EQ(a[i].price, b[i].price);
        EXPECT_EQ(a[i].qty, b[i].qty);
    }
}
