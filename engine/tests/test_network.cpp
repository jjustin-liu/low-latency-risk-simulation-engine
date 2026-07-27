#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "risksim/network.hpp"

using namespace risksim;

TEST(Reconstruction, MaxEntropyMatchesMarginals) {
    const std::vector<double> liab_out{100.0, 50.0, 30.0, 20.0};  // row sums
    const std::vector<double> assets_in{40.0, 60.0, 45.0, 55.0};  // col sums (same total = 200)
    const Matrix x = reconstruct_max_entropy(assets_in, liab_out);
    const std::size_t n = liab_out.size();
    for (std::size_t i = 0; i < n; ++i) {
        double rs = 0.0;
        double cs = 0.0;
        for (std::size_t j = 0; j < n; ++j) {
            rs += x(i, j);
            cs += x(j, i);
        }
        EXPECT_NEAR(rs, liab_out[i], 1e-6);
        EXPECT_NEAR(cs, assets_in[i], 1e-6);
        EXPECT_NEAR(x(i, i), 0.0, 1e-9);  // no self-lending
    }
}

TEST(Reconstruction, MinDensityMatchesMarginalsAndIsSparser) {
    const std::vector<double> liab_out{100.0, 50.0, 30.0, 20.0};
    const std::vector<double> assets_in{40.0, 60.0, 45.0, 55.0};
    const Matrix me = reconstruct_max_entropy(assets_in, liab_out);
    const Matrix md = reconstruct_min_density(assets_in, liab_out);
    const std::size_t n = liab_out.size();

    auto nnz = [&](const Matrix& m) {
        int c = 0;
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                if (m(i, j) > 1e-9) ++c;
        return c;
    };
    // Min-density row sums still match liabilities.
    for (std::size_t i = 0; i < n; ++i) {
        double rs = 0.0;
        for (std::size_t j = 0; j < n; ++j) rs += md(i, j);
        EXPECT_NEAR(rs, liab_out[i], 1e-6);
    }
    // ...but it uses strictly fewer links than the (near-dense) max-entropy fill.
    EXPECT_LT(nnz(md), nnz(me));
}

TEST(Generator, CorePeripheryIsBaselineSolvent) {
    CorePeripheryParams params;
    params.n_core = 5;
    params.n_periphery = 45;
    params.seed = 7;
    const ExposureNetwork net = generate_core_periphery(params);
    EXPECT_EQ(net.n, 50u);

    // Positive equity everywhere; core banks are more connected (larger balance
    // sheets) than the periphery on average.
    double core_assets = 0.0;
    double periph_assets = 0.0;
    for (std::size_t i = 0; i < net.n; ++i) {
        EXPECT_GT(net.equity[i], 0.0);
        const double ta = net.total_interbank_assets(i) + net.external_assets[i];
        if (i < params.n_core) core_assets += ta / params.n_core;
        else periph_assets += ta / params.n_periphery;
    }
    EXPECT_GT(core_assets, periph_assets);
}

TEST(Generator, MarginalsRoundTripThroughReconstruction) {
    CorePeripheryParams params;
    params.n_core = 4;
    params.n_periphery = 26;
    params.seed = 11;
    const ExposureNetwork net = generate_core_periphery(params);

    // Reconstruct from the (public) marginals; the reconstruction must reproduce
    // the marginals even though it cannot recover the true bilateral matrix.
    const Matrix rec = reconstruct_max_entropy(net.asset_marginals(), net.liability_marginals());
    for (std::size_t i = 0; i < net.n; ++i) {
        double rs = 0.0;
        for (std::size_t j = 0; j < net.n; ++j) rs += rec(i, j);
        EXPECT_NEAR(rs, net.total_liabilities(i), 1e-4);
    }
}
