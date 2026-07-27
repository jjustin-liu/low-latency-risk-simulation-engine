// Price-mediated (fire-sale) contagion, executed through the real order book.
//
// This is the SECOND contagion channel, orthogonal to counterparty default:
//   a distressed bank must deleverage -> it MARKET-SELLS assets into a limit
//   order book with finite depth -> the book's fills push the price DOWN
//   (emergent slippage, not an assumed impact function) -> every OTHER bank
//   holding that asset marks it to the new price -> its equity falls -> it too
//   deleverages -> ... a feedback loop driven by COMMON HOLDINGS (asset overlap).
//
// Coupling the execution engine (risksim::lob) to the systemic-risk engine is
// the point: the liquidity venue is the actual matching engine, so the price
// impact is whatever the resting depth produces. Deterministic (integer prices
// and lots, fixed processing order), so it is reproducible and unit-testable.
#ifndef RISKSIM_MARKET_FIRE_SALE_HPP
#define RISKSIM_MARKET_FIRE_SALE_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "risksim/network.hpp"

namespace risksim::market {

// Resting buy-side liquidity seeded into each asset book: a ladder of `depth`
// price levels, `lots_per_level` each, starting at the mid and stepping down by
// `tick_gap`. Shallower liquidity => larger price impact per unit sold.
struct BidLadder {
    std::int64_t lots_per_level = 1000;
    int depth = 64;
    std::int32_t tick_gap = 1;
};

struct FireSaleConfig {
    BidLadder liquidity;
    double lgd = 1.0;                    // loss given default for counterparty coupling
    bool counterparty_coupling = false;  // also propagate default losses via interbank links
    int max_rounds = 200;
};

struct FireSaleResult {
    std::vector<double> node_loss;     // equity loss per bank (pre-shock equity minus final)
    std::vector<char> defaulted;       // 1 if the bank was wiped out
    std::vector<double> price_impact;  // fractional price drop per asset
    double system_loss = 0.0;
    int num_defaults = 0;
    int rounds = 0;
};

class FireSaleModel {
public:
    // holdings: n_banks x n_assets integer lots (row-major). price0: per-asset mid
    // in ticks. equity0: pre-shock equity per bank (>0). `net` is optional and only
    // used when counterparty_coupling is on (must have n == n_banks).
    FireSaleModel(FireSaleConfig cfg, std::size_t n_banks, std::size_t n_assets,
                  std::span<const std::int64_t> holdings, std::span<const std::int64_t> price0,
                  std::span<const std::int64_t> equity0, const ExposureNetwork* net = nullptr);

    // shock[i] in [0,1]: initial fractional write-down of bank i's asset value.
    [[nodiscard]] FireSaleResult run(std::span<const double> shock) const;

    [[nodiscard]] std::size_t n_banks() const noexcept { return b_; }
    [[nodiscard]] std::size_t n_assets() const noexcept { return m_; }

private:
    FireSaleConfig cfg_;
    std::size_t b_;
    std::size_t m_;
    std::vector<std::int64_t> holdings0_;  // b_*m_
    std::vector<std::int64_t> price0_;      // m_
    std::vector<std::int64_t> equity0_;     // b_
    const ExposureNetwork* net_;
};

}  // namespace risksim::market

#endif  // RISKSIM_MARKET_FIRE_SALE_HPP
