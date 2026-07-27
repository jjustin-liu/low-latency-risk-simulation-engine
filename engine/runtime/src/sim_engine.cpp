#include "risksim/runtime/sim_engine.hpp"

#include <algorithm>
#include <cmath>

#include "risksim/contagion.hpp"
#include "risksim/copula.hpp"
#include "risksim/linalg.hpp"
#include "risksim/rng.hpp"
#include "risksim/sim/monte_carlo.hpp"
#include "risksim/runtime/wire_generated.hpp"

namespace risksim::runtime {

namespace {
constexpr double kTwoPi = 6.283185307179586;
}

SimEngine::SimEngine(const EngineConfig& cfg) : cfg_(cfg) {
    CorePeripheryParams params;
    params.n_core = cfg.n_core;
    params.n_periphery = cfg.n_periphery;
    params.seed = cfg.seed;
    net_ = generate_core_periphery(params);
    const std::size_t n = net_.n;

    // Deterministic layout: dense core near the origin, periphery on an outer
    // ring, with a little jitter so overlapping banks separate.
    node_x_.resize(n);
    node_y_.resize(n);
    node_value_.resize(n);
    DrawCursor layout(cfg.seed, 1);  // dedicated layout stream
    for (std::size_t i = 0; i < n; ++i) {
        const bool core = i < cfg.n_core;
        const std::size_t idx = core ? i : i - cfg.n_core;
        const std::size_t count = core ? cfg.n_core : cfg.n_periphery;
        const double r = core ? 0.28 : 1.0;
        const double ang = kTwoPi * static_cast<double>(idx) / static_cast<double>(std::max<std::size_t>(1, count));
        const double jitter = core ? 0.03 : 0.08;
        node_x_[i] = static_cast<float>(r * std::cos(ang) + jitter * (layout.uniform() - 0.5));
        node_y_[i] = static_cast<float>(r * std::sin(ang) + jitter * (layout.uniform() - 0.5));
        node_value_[i] =
            static_cast<float>(net_.total_interbank_assets(i) + net_.external_assets[i]);
    }

    // Static edge list from the liability matrix's nonzeros.
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            const double w = net_.liabilities(i, j);
            if (w > 0.0) {
                edge_src_.push_back(static_cast<std::uint32_t>(i));
                edge_dst_.push_back(static_cast<std::uint32_t>(j));
                edge_flow_.push_back(static_cast<float>(w));
            }
        }
    }

    target_shock_.assign(n, 0.0);
    current_shock_.assign(n, 0.0);
    refresh_risk();
}

void SimEngine::apply_shock(std::span<const std::size_t> banks, double magnitude) {
    std::fill(target_shock_.begin(), target_shock_.end(), 0.0);
    for (std::size_t b : banks)
        if (b < target_shock_.size()) target_shock_[b] = std::clamp(magnitude, 0.0, 1.0);
    control_override_ticks_ = 120;  // hold operator control for ~12s at 10Hz
}

void SimEngine::clear_shock() {
    std::fill(target_shock_.begin(), target_shock_.end(), 0.0);
    control_override_ticks_ = 0;
}

void SimEngine::advance_scenario() {
    if (control_override_ticks_ > 0) {
        --control_override_ticks_;
        return;
    }
    if (!cfg_.auto_scenario) return;

    // Cycle of 140 ticks: ramp (0-20), hold (20-70), decay (70-110), quiet.
    const std::uint64_t cycle = tick_ % 140;
    if (cycle == 0) {
        std::fill(target_shock_.begin(), target_shock_.end(), 0.0);
        // Shock a rotating core bank -- the systemically important ones.
        const std::size_t target = (tick_ / 140) % std::max<std::size_t>(1, cfg_.n_core);
        target_shock_[target] = 0.85;
    } else if (cycle >= 70 && cycle < 110) {
        for (double& s : target_shock_) s *= 0.94;  // decay
    } else if (cycle >= 110) {
        std::fill(target_shock_.begin(), target_shock_.end(), 0.0);
    }
}

void SimEngine::refresh_risk() {
    const std::vector<double> vol(net_.n, cfg_.node_vol);
    sim::SystemicMonteCarlo engine(net_, vol);
    Matrix corr = Matrix::identity(net_.n);
    for (std::size_t i = 0; i < net_.n; ++i)
        for (std::size_t j = 0; j < net_.n; ++j)
            if (i != j) corr(i, j) = cfg_.correlation;
    const StudentTCopula copula(corr, cfg_.t_dof);
    const Furfine model(1.0);
    sim::Config mc;
    mc.num_paths = cfg_.mc_paths;
    mc.alpha = cfg_.alpha;
    mc.seed = cfg_.seed ^ 0x5121;
    const sim::Result r = engine.run(copula, model, mc);
    var_ = r.var;
    es_ = r.es;
}

std::vector<std::byte> SimEngine::step() {
    advance_scenario();
    // Ease the actual shock toward the target for a smooth animation.
    for (std::size_t i = 0; i < net_.n; ++i)
        current_shock_[i] += 0.30 * (target_shock_[i] - current_shock_[i]);

    // Distress from dynamical DebtRank -> per-bank health.
    const ContagionResult r = DebtRank{/*dynamical=*/true}.propagate(net_, current_shock_);
    std::vector<float> health(net_.n);
    for (std::size_t i = 0; i < net_.n; ++i) {
        const double h = net_.equity[i] > 0.0 ? r.node_loss[i] / net_.equity[i] : 0.0;
        health[i] = static_cast<float>(std::clamp(1.0 - h, 0.0, 1.0));
    }
    total_loss_ = static_cast<float>(r.system_loss);
    num_defaults_ = static_cast<std::uint32_t>(r.num_defaults);

    if (cfg_.mc_every > 0 && tick_ > 0 && tick_ % static_cast<std::uint64_t>(cfg_.mc_every) == 0)
        refresh_risk();

    wire::SnapshotFixed fixed;
    fixed.n_nodes = static_cast<std::uint32_t>(net_.n);
    fixed.n_edges = static_cast<std::uint32_t>(edge_src_.size());
    fixed.var = static_cast<float>(var_);
    fixed.es = static_cast<float>(es_);
    fixed.total_loss = total_loss_;
    fixed.num_defaults = num_defaults_;

    const std::uint64_t sim_time_ns =
        static_cast<std::uint64_t>(static_cast<double>(tick_) * 1e8);  // 10 Hz nominal
    std::vector<std::byte> frame =
        wire::build_snapshot(tick_, sim_time_ns, /*wall_emit_ns=*/0, fixed, health, node_value_,
                             node_x_, node_y_, edge_src_, edge_dst_, edge_flow_);
    ++tick_;
    return frame;
}

}  // namespace risksim::runtime
