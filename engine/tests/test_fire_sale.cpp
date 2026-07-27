#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "risksim/market/fire_sale.hpp"
#include "risksim/network.hpp"

using namespace risksim::market;
using risksim::ExposureNetwork;
using risksim::Matrix;

namespace {
FireSaleConfig ladder_cfg(std::int64_t lots, int depth) {
    FireSaleConfig c;
    c.liquidity.lots_per_level = lots;
    c.liquidity.depth = depth;
    c.liquidity.tick_gap = 1;
    return c;
}
}  // namespace

TEST(FireSale, NoShockNoLoss) {
    const std::vector<std::int64_t> holdings{100, 100};
    const std::vector<std::int64_t> price0{100};
    const std::vector<std::int64_t> equity0{5000, 5000};
    FireSaleModel m(ladder_cfg(50, 20), 2, 1, holdings, price0, equity0);
    const FireSaleResult r = m.run(std::vector<double>{0.0, 0.0});
    EXPECT_NEAR(r.price_impact[0], 0.0, 1e-12);
    EXPECT_NEAR(r.system_loss, 0.0, 1e-6);
    EXPECT_EQ(r.num_defaults, 0);
}

TEST(FireSale, HandComputedPriceImpactAndMarkdown) {
    // Bank 0 (120 lots, thin equity) is fully shocked -> defaults -> liquidates
    // 120 lots into a ladder of 50-lot bids at 100, 99, 98, ...:
    //   fills 50@100, 50@99, 20@98 -> new best bid 98 -> 2% price impact.
    // Bank 1 (200 lots, unlevered, huge equity) only marks down: loss = 2*200 = 400.
    const std::vector<std::int64_t> holdings{120, 200};
    const std::vector<std::int64_t> price0{100};
    const std::vector<std::int64_t> equity0{1000, 20000};
    FireSaleModel m(ladder_cfg(50, 20), 2, 1, holdings, price0, equity0);
    const FireSaleResult r = m.run(std::vector<double>{1.0, 0.0});

    EXPECT_NEAR(r.price_impact[0], 0.02, 1e-9);
    EXPECT_EQ(r.defaulted[0], 1);
    EXPECT_EQ(r.defaulted[1], 0);
    EXPECT_NEAR(r.node_loss[1], 400.0, 1e-6);  // pure mark-to-market contagion loss
}

TEST(FireSale, OverlapCreatesContagionDisjointDoesNot) {
    // Overlap: both banks hold the SAME asset -> shocking bank 0 marks down bank 1.
    {
        const std::vector<std::int64_t> holdings{100, 100};
        const std::vector<std::int64_t> price0{100};
        const std::vector<std::int64_t> equity0{1000, 10000};
        FireSaleModel m(ladder_cfg(50, 20), 2, 1, holdings, price0, equity0);
        const FireSaleResult r = m.run(std::vector<double>{1.0, 0.0});
        EXPECT_GT(r.node_loss[1], 0.0);
        EXPECT_GT(r.price_impact[0], 0.0);
    }
    // Disjoint: bank 0 holds asset 0, bank 1 holds asset 1 -> no cross-contagion.
    {
        const std::vector<std::int64_t> holdings{100, 0, 0, 100};  // 2 banks x 2 assets
        const std::vector<std::int64_t> price0{100, 100};
        const std::vector<std::int64_t> equity0{1000, 10000};
        FireSaleModel m(ladder_cfg(50, 20), 2, 2, holdings, price0, equity0);
        const FireSaleResult r = m.run(std::vector<double>{1.0, 0.0});
        EXPECT_GT(r.price_impact[0], 0.0);      // the shocked asset moves
        EXPECT_NEAR(r.price_impact[1], 0.0, 1e-12);  // the other asset does not
        EXPECT_NEAR(r.node_loss[1], 0.0, 1e-6);      // bank 1 untouched
    }
}

TEST(FireSale, MonotoneInShock) {
    const std::vector<std::int64_t> holdings{200, 200, 200};
    const std::vector<std::int64_t> price0{100};
    const std::vector<std::int64_t> equity0{4000, 4000, 4000};  // leverage 5x
    FireSaleModel m(ladder_cfg(40, 40), 3, 1, holdings, price0, equity0);
    const FireSaleResult small = m.run(std::vector<double>{0.2, 0.0, 0.0});
    const FireSaleResult big = m.run(std::vector<double>{0.5, 0.0, 0.0});
    EXPECT_GE(big.system_loss, small.system_loss);
    EXPECT_GE(big.price_impact[0], small.price_impact[0]);
    EXPECT_GT(big.system_loss, 0.0);
}

TEST(FireSale, CounterpartyCouplingAmplifiesLosses) {
    // Bank 0 owes banks 1 and 2 via interbank links. When it defaults in the
    // fire sale, coupling propagates its shortfall to those creditors -- the two
    // channels together must produce at least as much loss as fire-sale alone.
    ExposureNetwork net;
    net.n = 3;
    net.liabilities = Matrix(3, 3);
    net.liabilities(0, 1) = 5000.0;  // bank 0 owes bank 1
    net.liabilities(0, 2) = 5000.0;  // bank 0 owes bank 2
    net.external_assets.assign(3, 0.0);
    net.equity.assign(3, 0.0);

    const std::vector<std::int64_t> holdings{100, 100, 100};
    const std::vector<std::int64_t> price0{100};
    const std::vector<std::int64_t> equity0{1000, 8000, 8000};
    const std::vector<double> shock{1.0, 0.0, 0.0};

    FireSaleConfig off = ladder_cfg(50, 30);
    FireSaleModel m_off(off, 3, 1, holdings, price0, equity0, &net);
    const FireSaleResult r_off = m_off.run(shock);

    FireSaleConfig on = ladder_cfg(50, 30);
    on.counterparty_coupling = true;
    on.lgd = 1.0;
    FireSaleModel m_on(on, 3, 1, holdings, price0, equity0, &net);
    const FireSaleResult r_on = m_on.run(shock);

    EXPECT_GT(r_on.system_loss, r_off.system_loss);  // counterparty channel adds loss
}
