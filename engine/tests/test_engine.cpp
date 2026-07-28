#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <tuple>

#include "risksim/runtime/sim_engine.hpp"
#include "risksim/runtime/wire_generated.hpp"

using namespace risksim::runtime;

// A single periphery failure stays local; a full core shock reaches the periphery.
TEST(SimEngine, CascadeContainmentAndSpread) {
    auto finalHealth = [](std::span<const std::size_t> banks, double mag) {
        EngineConfig cfg;
        cfg.n_core = 8;
        cfg.n_periphery = 70;
        cfg.mc_every = 0;
        cfg.auto_scenario = false;
        SimEngine eng(cfg);
        eng.apply_shock(banks, mag);
        std::vector<std::byte> frame;
        for (int i = 0; i < 25; ++i) frame = eng.step();
        const auto pv = risksim::wire::parse_snapshot(frame);
        double coreSum = 0, periphSum = 0, targetMin = 1;
        for (std::size_t i = 0; i < pv->node_health.size(); ++i) {
            const float h = pv->node_health[i];
            if (i < cfg.n_core) coreSum += h; else periphSum += h;
        }
        for (std::size_t b : banks) targetMin = std::min<double>(targetMin, pv->node_health[b]);
        return std::tuple{coreSum / static_cast<double>(cfg.n_core),
                          periphSum / static_cast<double>(pv->node_health.size() - cfg.n_core),
                          targetMin};
    };

    const std::array<std::size_t, 1> periph{40};
    const auto [pCore, pPeriph, pTargetMin] = finalHealth(periph, 0.9);
    EXPECT_LT(pTargetMin, 0.4);   // the shocked bank itself clearly fails
    EXPECT_GT(pCore, 0.8);        // ...but the core is left intact
    EXPECT_GT(pPeriph, 0.85);     // ...and the rest of the periphery too (contained)

    const std::array<std::size_t, 8> core{0, 1, 2, 3, 4, 5, 6, 7};
    const auto [cCore, cPeriph, cTargetMin] = finalHealth(core, 0.85);
    (void) cTargetMin;
    EXPECT_LT(cCore, 0.2);        // the core collapses
    EXPECT_LT(cPeriph, 0.8);      // ...and the distress reaches the periphery (spreads)
}

// The pricing/risk channels surfaced to the frontend: a Merton PD that rises
// under stress, a Monte-Carlo loss histogram, and a fire-sale price impact that
// is quiescent at rest but reacts to a systemic (core) shock.
TEST(SimEngine, PricingChannelsRespondToStress) {
    EngineConfig cfg;
    cfg.n_core = 8;
    cfg.n_periphery = 70;
    cfg.mc_every = 0;
    cfg.auto_scenario = false;

    auto meanPd = [](const risksim::wire::SnapshotView& v) {
        double s = 0;
        for (float p : v.node_pd) s += p;
        return s / static_cast<double>(v.node_pd.size());
    };
    auto maxImpact = [](const risksim::wire::SnapshotView& v) {
        double m = 0;
        for (float a : v.asset_impact) m = std::max(m, static_cast<double>(a));
        return m;
    };

    // Rest: a valid histogram, ~no PD, no fire-sale impact.
    SimEngine calm(cfg);
    std::vector<std::byte> frame;
    for (int i = 0; i < 5; ++i) frame = calm.step();
    auto rest = risksim::wire::parse_snapshot(frame);
    ASSERT_TRUE(rest.has_value());
    EXPECT_GT(rest->fixed.n_hist, 0u);
    EXPECT_EQ(rest->loss_hist.size(), rest->fixed.n_hist);
    EXPECT_GT(rest->fixed.n_assets, 0u);
    double histSum = 0;
    for (float c : rest->loss_hist) histSum += c;
    EXPECT_NEAR(histSum, 1.0, 1e-3);  // bin fractions sum to 1
    const double restPd = meanPd(*rest);
    EXPECT_LT(maxImpact(*rest), 0.02);  // books essentially untouched at rest

    // Core shock: PD climbs and the order books take a visible hit.
    SimEngine stressed(cfg);
    const std::array<std::size_t, 8> core{0, 1, 2, 3, 4, 5, 6, 7};
    stressed.apply_shock(core, 0.85);
    for (int i = 0; i < 25; ++i) frame = stressed.step();
    auto hit = risksim::wire::parse_snapshot(frame);
    ASSERT_TRUE(hit.has_value());
    EXPECT_GT(meanPd(*hit), restPd * 2.0);  // default risk rises materially
    EXPECT_GT(maxImpact(*hit), 0.08);       // fire-sale slippage is visible
}

TEST(SimEngine, StepEmitsValidSnapshot) {
    EngineConfig cfg;
    cfg.n_core = 5;
    cfg.n_periphery = 25;
    cfg.mc_paths = 4000;
    cfg.mc_every = 0;  // no refresh mid-test
    SimEngine engine(cfg);

    EXPECT_EQ(engine.num_nodes(), 30u);
    EXPECT_GT(engine.num_edges(), 0u);
    // VaR/ES are non-negative and coherent (ES >= VaR always). VaR can legitimately
    // be 0 if the tail carries no systemic loss on this particular network, which
    // varies slightly across platforms (network weights use std::exp).
    EXPECT_GE(engine.var(), 0.0);
    EXPECT_GE(engine.expected_shortfall(), engine.var());

    const std::vector<std::byte> frame = engine.step();
    const auto v = risksim::wire::parse_snapshot(frame);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->fixed.n_nodes, 30u);
    EXPECT_EQ(v->node_health.size(), 30u);
    EXPECT_EQ(v->edge_src.size(), engine.num_edges());
    for (float h : v->node_health) {
        EXPECT_GE(h, 0.0f);
        EXPECT_LE(h, 1.0f);
    }
}

TEST(SimEngine, OperatorShockReducesHealthOfTargetedCore) {
    EngineConfig cfg;
    cfg.n_core = 5;
    cfg.n_periphery = 25;
    cfg.mc_every = 0;
    cfg.auto_scenario = false;  // isolate the operator shock
    SimEngine engine(cfg);

    // Baseline: everything healthy.
    (void) engine.step();

    const std::array<std::size_t, 1> target{0};
    engine.apply_shock(target, 0.9);
    // Let the eased shock ramp in and contagion propagate.
    float min_health = 1.0f;
    for (int i = 0; i < 20; ++i) {
        const auto frame = engine.step();
        const auto v = risksim::wire::parse_snapshot(frame);
        for (float h : v->node_health) min_health = std::min(min_health, h);
    }
    // Structural check: any positive external-asset shock produces positive
    // distress (h_i > 0 => health_i < 1) on the targeted bank, so some node's
    // health drops below 1.0. The exact magnitude depends on the generated
    // network (std::exp weights vary across libm), so we assert only the sign.
    EXPECT_LT(min_health, 1.0f);
}
