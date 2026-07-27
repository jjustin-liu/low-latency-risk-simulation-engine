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
#include <span>
#include <vector>

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
    double var_ = 0.0;
    double es_ = 0.0;
    float total_loss_ = 0.0f;
    std::uint32_t num_defaults_ = 0;
};

}  // namespace risksim::runtime

#endif  // RISKSIM_RUNTIME_SIM_ENGINE_HPP
