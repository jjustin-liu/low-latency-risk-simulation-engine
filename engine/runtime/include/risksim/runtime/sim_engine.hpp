// SimEngine: the per-tick simulation that feeds the visualization.
//
// Holds a fixed interbank network + precomputed layout and edge list. Each tick
// it eases the current shock toward a target (driven either by an operator
// control message or a built-in auto-scenario), runs a dynamical-DebtRank
// contagion to get per-bank distress, periodically refreshes systemic VaR/ES via
// Monte Carlo, and serializes a Snapshot wire frame. Pure of any I/O or threads,
// so it is unit-testable in isolation.
#ifndef RISKSIM_RUNTIME_SIM_ENGINE_HPP
#define RISKSIM_RUNTIME_SIM_ENGINE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "risksim/market/fire_sale.hpp"
#include "risksim/network.hpp"

namespace risksim::runtime {

struct EngineConfig {
    std::size_t n_core = 8;
    std::size_t n_periphery = 92;
    std::uint64_t seed = 0xC0FFEE;
    double node_vol = 0.30;     // asset-return volatility (shock severity)
    double correlation = 0.55;  // equicorrelation across banks
    double t_dof = 4.0;         // Student-t copula degrees of freedom
    std::uint32_t mc_paths = 20000;
    double alpha = 0.975;
    int mc_every = 10;          // refresh VaR/ES every N ticks
    bool auto_scenario = true;  // self-driving demo shocks
    // Recapitalization: each bank's equity is raised to at least this multiple of
    // its single largest counterparty exposure. Higher => distress attenuates more
    // (contained cascades); lower => contagion spreads further. 3.5 keeps a single
    // periphery failure local while letting a full core shock reach the periphery.
    double cap_cushion = 3.5;

    // Per-bank Merton PD: default point / horizon / drift for DD = [ln(V/D) +
    // (mu-0.5 sigma^2)T]/(sigma sqrt T). V falls as the tick's contagion impairs a
    // bank's equity, so PD rises as distress spreads.
    double merton_mu = 0.05;
    double merton_horizon = 1.0;

    // Fire-sale (price-mediated) channel: a handful of tradeable assets with
    // overlapping bank holdings, cleared through the real limit-order book each
    // tick. Orthogonal to the counterparty channel above.
    std::size_t n_assets = 6;
    std::uint32_t hist_bins = 48;  // Monte-Carlo loss-distribution resolution
};

class SimEngine {
public:
    explicit SimEngine(const EngineConfig& cfg);

    // Advance one tick and return the serialized Snapshot frame.
    [[nodiscard]] std::vector<std::byte> step();

    // Operator control: apply a fractional external-asset shock (in [0,1]) to a
    // set of banks; overrides the auto-scenario for a while.
    void apply_shock(std::span<const std::size_t> banks, double magnitude);
    void clear_shock();

    [[nodiscard]] std::size_t num_nodes() const noexcept { return net_.n; }
    [[nodiscard]] std::size_t num_edges() const noexcept { return edge_src_.size(); }
    [[nodiscard]] std::uint64_t tick() const noexcept { return tick_; }
    // Latest headline risk numbers (also embedded in each frame).
    [[nodiscard]] double var() const noexcept { return var_; }
    [[nodiscard]] double expected_shortfall() const noexcept { return es_; }

private:
    void refresh_risk();
    void advance_scenario();

    EngineConfig cfg_;
    ExposureNetwork net_;

    // Merton default-point per bank (D in the DD formula) and unstressed asset
    // value (V at zero shock); PD is recomputed each tick from the current health.
    std::vector<double> debt_;
    std::vector<double> asset_value0_;

    // Fire-sale channel: a matching-engine-backed price-mediated cascade over
    // `n_assets_` assets with overlapping holdings. Rebuilt once; run each tick.
    std::optional<market::FireSaleModel> fire_;
    std::size_t n_assets_ = 0;
    market::BidLadder ladder_;
    std::vector<std::int64_t> asset_price0_;  // per-asset mid in ticks

    std::vector<float> node_x_;
    std::vector<float> node_y_;
    std::vector<float> node_value_;
    std::vector<std::uint32_t> edge_src_;
    std::vector<std::uint32_t> edge_dst_;
    std::vector<float> edge_flow_;

    std::vector<double> target_shock_;   // where the scenario is heading
    std::vector<double> current_shock_;  // eased actual shock

    std::uint64_t tick_ = 0;
    int control_override_ticks_ = 0;  // >0 while an operator shock overrides auto
    int shock_age_ = 0;  // ticks a shock has been active; caps contagion rounds so it spreads
    double var_ = 0.0;
    double es_ = 0.0;
    float total_loss_ = 0.0f;
    std::uint32_t num_defaults_ = 0;

    // Per-tick visualization outputs (serialized into each frame).
    std::vector<float> node_pd_;       // Merton PD per bank
    std::vector<float> loss_hist_;     // MC systemic-loss distribution (bin fractions)
    double loss_hist_max_ = 0.0;       // right edge of the histogram
    std::vector<float> asset_impact_;  // fractional fire-sale price drop per asset
    std::vector<float> asset_depth_;   // remaining book liquidity fraction per asset
};

}  // namespace risksim::runtime

#endif  // RISKSIM_RUNTIME_SIM_ENGINE_HPP
