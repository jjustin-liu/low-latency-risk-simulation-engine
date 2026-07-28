#include "risksim/runtime/sim_engine.hpp"

#include <algorithm>
#include <cmath>

#include "risksim/contagion.hpp"
#include "risksim/copula.hpp"
#include "risksim/linalg.hpp"
#include "risksim/merton.hpp"
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

    // Recapitalize for a well-behaved cascade: ensure each bank's equity
    // comfortably exceeds its single largest counterparty exposure, so no single
    // default wipes a bank outright (the DebtRank impact factor W = exposure /
    // equity stays well below 1). Distress then ATTENUATES as it spreads, giving
    // containable, gradual contagion instead of instant total collapse.
    for (std::size_t i = 0; i < n; ++i) {
        double max_in = 0.0;
        for (std::size_t j = 0; j < n; ++j)
            max_in = std::max(max_in, net_.liabilities(j, i));  // i's claim on j
        net_.equity[i] = std::max(net_.equity[i], cfg_.cap_cushion * max_in);
        // Set exogenous assets equal to equity so a DIRECT shock of magnitude m
        // maps to an initial equity loss of m (the targeted bank reddens
        // proportionally), decoupled from the transmission strength W = exposure
        // / equity, which the cushion attenuates.
        net_.external_assets[i] = net_.equity[i];
    }

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

    // ---- Merton default points (V0, D per bank) --------------------------------
    // Distance-to-default uses V = current asset value and D = the default point.
    // Take D = V0 - E (the implied obligations), so at zero shock DD is large; as
    // the tick's contagion erodes equity, V falls toward D and PD rises.
    debt_.resize(n);
    asset_value0_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double v0 = net_.total_interbank_assets(i) + net_.external_assets[i];
        asset_value0_[i] = v0;
        debt_[i] = std::max(1e-9, v0 - net_.equity[i]);
    }

    // ---- Fire-sale channel: overlapping asset holdings + order books -----------
    // A handful of tradeable assets; each bank holds a random (equity-scaled)
    // subset, so books overlap and a forced sale by one bank marks down others --
    // the price-mediated cascade, cleared through the real matching engine.
    n_assets_ = std::max<std::size_t>(1, cfg_.n_assets);
    ladder_ = market::BidLadder{/*lots_per_level=*/45, /*depth=*/220, /*tick_gap=*/3};
    constexpr std::int64_t kAssetPrice = 1000;
    constexpr double kLeverage = 2.0;
    constexpr std::int64_t kBaseLots = 600;
    asset_price0_.assign(n_assets_, kAssetPrice);

    double max_eq = 1e-9;
    for (std::size_t i = 0; i < n; ++i) max_eq = std::max(max_eq, net_.equity[i]);

    std::vector<std::int64_t> holdings(n * n_assets_, 0);
    std::vector<std::int64_t> equity0(n, 0);
    DrawCursor hc(cfg_.seed, 7);  // dedicated holdings stream
    for (std::size_t i = 0; i < n; ++i) {
        const double size = net_.equity[i] / max_eq;  // (0,1], bigger banks hold more
        std::int64_t hv0 = 0;
        for (std::size_t m = 0; m < n_assets_; ++m) {
            const double hold = hc.uniform();
            const double mult = hc.uniform();
            if (hold > 0.30) {  // ~70% chance to hold each asset
                const auto lots = static_cast<std::int64_t>(kBaseLots * size * (0.5 + mult));
                holdings[i * n_assets_ + m] = lots;
                hv0 += lots * kAssetPrice;
            }
        }
        if (hv0 == 0) {  // guarantee at least one holding so equity0 > 0
            const auto lots = static_cast<std::int64_t>(kBaseLots * std::max(0.2, size));
            holdings[i * n_assets_ + (i % n_assets_)] = lots;
            hv0 = lots * kAssetPrice;
        }
        equity0[i] = std::max<std::int64_t>(1, static_cast<std::int64_t>(static_cast<double>(hv0) / kLeverage));
    }
    market::FireSaleConfig fcfg;
    fcfg.liquidity = ladder_;
    fcfg.counterparty_coupling = false;  // pure price-mediated channel (orthogonal to DebtRank)
    fire_.emplace(fcfg, n, n_assets_, holdings, asset_price0_, equity0);

    node_pd_.assign(n, 0.0f);
    asset_impact_.assign(n_assets_, 0.0f);
    asset_depth_.assign(n_assets_, 1.0f);

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
    mc.hist_bins = cfg_.hist_bins;
    const sim::Result r = engine.run(copula, model, mc);
    var_ = r.var;
    es_ = r.es;
    loss_hist_.assign(r.loss_hist.begin(), r.loss_hist.end());
    loss_hist_max_ = r.hist_max;
}

std::vector<std::byte> SimEngine::step() {
    advance_scenario();
    // Ease the actual shock toward the target: a gentle onset so the cascade is
    // visible, a quicker recovery so "clear" feels responsive. Snap tiny values
    // to zero so a cleared shock fully heals.
    for (std::size_t i = 0; i < net_.n; ++i) {
        const double gap = target_shock_[i] - current_shock_[i];
        current_shock_[i] += (gap > 0.0 ? 0.14 : 0.30) * gap;
        if (std::fabs(current_shock_[i]) < 1e-4) current_shock_[i] = 0.0;
    }

    // Reveal the contagion ONE propagation round per tick so distress visibly
    // spreads outward from the shocked banks instead of hitting everyone at once.
    double max_shock = 0.0;
    for (double s : current_shock_) max_shock = std::max(max_shock, s);
    if (max_shock > 0.01)
        ++shock_age_;
    else
        shock_age_ = 0;
    const int rounds = std::clamp(shock_age_, 1, 60);

    // Distress from dynamical DebtRank (capped to `rounds` hops) -> per-bank health.
    const ContagionResult r = DebtRank{/*dynamical=*/true, rounds}.propagate(net_, current_shock_);
    std::vector<float> health(net_.n);
    for (std::size_t i = 0; i < net_.n; ++i) {
        const double h = net_.equity[i] > 0.0 ? r.node_loss[i] / net_.equity[i] : 0.0;
        health[i] = static_cast<float>(std::clamp(1.0 - h, 0.0, 1.0));
    }
    total_loss_ = static_cast<float>(r.system_loss);
    num_defaults_ = static_cast<std::uint32_t>(r.num_defaults);

    // Per-bank Merton PD from the current stressed asset value V = D + remaining
    // equity. As contagion erodes equity (health -> 0), V -> D and PD -> ~0.5.
    for (std::size_t i = 0; i < net_.n; ++i) {
        const double v = debt_[i] + net_.equity[i] * static_cast<double>(health[i]);
        node_pd_[i] = static_cast<float>(
            merton_pd(v, debt_[i], cfg_.node_vol, cfg_.merton_mu, cfg_.merton_horizon));
    }

    // Fire-sale (price-mediated) channel: the same shock forces distressed banks to
    // deleverage into the order books; the resulting slippage is the price impact,
    // and how far it ate into the resting bid ladder is the remaining depth.
    if (fire_) {
        const market::FireSaleResult fr = fire_->run(current_shock_);
        const double cap_ticks =
            static_cast<double>(ladder_.depth) * static_cast<double>(ladder_.tick_gap);
        for (std::size_t m = 0; m < n_assets_; ++m) {
            const double impact = std::clamp(fr.price_impact[m], 0.0, 1.0);
            asset_impact_[m] = static_cast<float>(impact);
            const double consumed =
                cap_ticks > 0.0 ? impact * static_cast<double>(asset_price0_[m]) / cap_ticks : 0.0;
            asset_depth_[m] = static_cast<float>(std::clamp(1.0 - consumed, 0.0, 1.0));
        }
    }

    if (cfg_.mc_every > 0 && tick_ > 0 && tick_ % static_cast<std::uint64_t>(cfg_.mc_every) == 0)
        refresh_risk();

    wire::SnapshotFixed fixed;
    fixed.n_nodes = static_cast<std::uint32_t>(net_.n);
    fixed.n_edges = static_cast<std::uint32_t>(edge_src_.size());
    fixed.var = static_cast<float>(var_);
    fixed.es = static_cast<float>(es_);
    fixed.total_loss = total_loss_;
    fixed.num_defaults = num_defaults_;
    fixed.n_hist = static_cast<std::uint32_t>(loss_hist_.size());
    fixed.hist_max = static_cast<float>(loss_hist_max_);
    fixed.n_assets = static_cast<std::uint32_t>(n_assets_);

    const std::uint64_t sim_time_ns =
        static_cast<std::uint64_t>(static_cast<double>(tick_) * 1e8);  // 10 Hz nominal
    std::vector<std::byte> frame = wire::build_snapshot(
        tick_, sim_time_ns, /*wall_emit_ns=*/0, fixed, health, node_value_, node_x_, node_y_,
        node_pd_, edge_src_, edge_dst_, edge_flow_, loss_hist_, asset_impact_, asset_depth_);
    ++tick_;
    return frame;
}

}  // namespace risksim::runtime
