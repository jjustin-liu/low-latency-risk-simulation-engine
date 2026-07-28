#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "risksim/runtime/wire_generated.hpp"

using namespace risksim::wire;

TEST(Wire, SnapshotRoundTrip) {
    SnapshotFixed fixed;
    fixed.n_nodes = 3;
    fixed.n_edges = 2;
    fixed.var = 12.5f;
    fixed.es = 20.25f;
    fixed.total_loss = 100.0f;
    fixed.num_defaults = 1;
    fixed.n_hist = 4;
    fixed.hist_max = 250.0f;
    fixed.n_assets = 2;

    const std::vector<float> health{1.0f, 0.5f, 0.0f};
    const std::vector<float> value{10.0f, 20.0f, 30.0f};
    const std::vector<float> x{0.1f, 0.2f, 0.3f};
    const std::vector<float> y{0.4f, 0.5f, 0.6f};
    const std::vector<float> pd{0.01f, 0.2f, 0.5f};
    const std::vector<std::uint32_t> esrc{0, 1};
    const std::vector<std::uint32_t> edst{1, 2};
    const std::vector<float> flow{5.0f, 7.5f};
    const std::vector<float> hist{0.7f, 0.2f, 0.07f, 0.03f};
    const std::vector<float> impact{0.1f, 0.4f};
    const std::vector<float> depth{0.9f, 0.6f};

    const std::vector<std::byte> buf = build_snapshot(42, 1000, 2000, fixed, health, value, x, y,
                                                      pd, esrc, edst, flow, hist, impact, depth);

    // Header body_len must equal the buffer minus the header.
    const auto parsed = parse_snapshot(buf);
    ASSERT_TRUE(parsed.has_value());
    const SnapshotView& v = *parsed;

    EXPECT_EQ(v.header.magic, kMagic);
    EXPECT_EQ(v.header.version, kSchemaVersionMajor);
    EXPECT_EQ(v.header.tick_seq, 42u);
    EXPECT_EQ(v.header.sim_time_ns, 1000u);
    EXPECT_EQ(v.header.wall_emit_ns, 2000u);
    EXPECT_EQ(v.header.body_len, buf.size() - kHeaderSize);

    EXPECT_EQ(v.fixed.n_nodes, 3u);
    EXPECT_EQ(v.fixed.n_edges, 2u);
    EXPECT_FLOAT_EQ(v.fixed.var, 12.5f);
    EXPECT_FLOAT_EQ(v.fixed.es, 20.25f);
    EXPECT_EQ(v.fixed.num_defaults, 1u);

    ASSERT_EQ(v.node_health.size(), 3u);
    EXPECT_FLOAT_EQ(v.node_health[1], 0.5f);
    ASSERT_EQ(v.node_pd.size(), 3u);
    EXPECT_FLOAT_EQ(v.node_pd[2], 0.5f);
    ASSERT_EQ(v.edge_src.size(), 2u);
    EXPECT_EQ(v.edge_dst[1], 2u);
    EXPECT_FLOAT_EQ(v.edge_flow[0], 5.0f);
    ASSERT_EQ(v.loss_hist.size(), 4u);
    EXPECT_FLOAT_EQ(v.loss_hist[0], 0.7f);
    EXPECT_FLOAT_EQ(v.fixed.hist_max, 250.0f);
    ASSERT_EQ(v.asset_impact.size(), 2u);
    EXPECT_FLOAT_EQ(v.asset_impact[1], 0.4f);
    ASSERT_EQ(v.asset_depth.size(), 2u);
    EXPECT_FLOAT_EQ(v.asset_depth[0], 0.9f);
}

TEST(Wire, RejectsTruncatedAndBadMagic) {
    SnapshotFixed fixed;
    fixed.n_nodes = 1;
    fixed.n_edges = 0;
    const std::vector<float> one{1.0f};
    const std::vector<std::uint32_t> none0;
    const std::vector<float> none;
    std::vector<std::byte> buf = build_snapshot(1, 0, 0, fixed, one, one, one, one, one, none0,
                                                none0, none, none, none, none);

    // Truncated buffer -> nullopt.
    std::vector<std::byte> truncated(buf.begin(), buf.begin() + 10);
    EXPECT_FALSE(parse_snapshot(truncated).has_value());

    // Corrupt the magic -> nullopt.
    buf[0] = std::byte{0xFF};
    EXPECT_FALSE(parse_snapshot(buf).has_value());
}
