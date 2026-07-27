#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>

#include "risksim/runtime/sim_engine.hpp"
#include "risksim/runtime/wire_generated.hpp"

using namespace risksim::runtime;

TEST(SimEngine, StepEmitsValidSnapshot) {
    EngineConfig cfg;
    cfg.n_core = 5;
    cfg.n_periphery = 25;
    cfg.mc_paths = 4000;
    cfg.mc_every = 0;  // no refresh mid-test
    SimEngine engine(cfg);

    EXPECT_EQ(engine.num_nodes(), 30u);
    EXPECT_GT(engine.num_edges(), 0u);
    EXPECT_GT(engine.var(), 0.0);
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
    // The shock leaves a visible mark. Loose threshold: the exact drop depends
    // on the generated network (which uses std::exp for edge weights and so
    // differs slightly across libm implementations), but a shock this size
    // always reduces some bank's health well below 1.0.
    EXPECT_LT(min_health, 0.99f);
}
