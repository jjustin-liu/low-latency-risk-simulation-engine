#include <gtest/gtest.h>

#include <cmath>

#include "risksim/merton.hpp"

using namespace risksim;

TEST(Merton, DistanceToDefaultAndPd) {
    // Well-capitalized firm: V well above D, low vol -> large DD, tiny PD.
    const double dd = distance_to_default(120.0, 80.0, 0.20, 0.05, 1.0);
    EXPECT_GT(dd, 2.0);
    const double pd = merton_pd(120.0, 80.0, 0.20, 0.05, 1.0);
    EXPECT_GT(pd, 0.0);
    EXPECT_LT(pd, 0.02);
    // More leverage (higher D) must raise PD.
    EXPECT_GT(merton_pd(120.0, 110.0, 0.20, 0.05, 1.0), pd);
    // More asset vol must raise PD.
    EXPECT_GT(merton_pd(120.0, 80.0, 0.40, 0.05, 1.0), pd);
}

TEST(Merton, EquityValueIsBlackScholesCall) {
    // Classic BS call: S=100, K=80, sigma=0.2, r=0.05, T=1 -> ~24.589.
    const double e = merton_equity_value(100.0, 80.0, 0.20, 0.05, 1.0);
    EXPECT_NEAR(e, 24.5888, 1e-3);
    // Equity delta Phi(d1) is in (0,1) and near 1 for deep in-the-money.
    const double delta = merton_equity_delta(100.0, 80.0, 0.20, 0.05, 1.0);
    EXPECT_GT(delta, 0.9);
    EXPECT_LT(delta, 1.0);
}

TEST(Merton, KmvRecoversLatentAssetsAndVol) {
    // Forward: from a known (V, sigma_V) compute the observable (E, sigma_E),
    // then confirm the KMV iteration inverts back to (V, sigma_V).
    const double v_true = 100.0;
    const double sigma_v_true = 0.25;
    const double debt = 70.0;
    const double r = 0.03;
    const double t = 1.0;

    const double e = merton_equity_value(v_true, debt, sigma_v_true, r, t);
    const double delta = merton_equity_delta(v_true, debt, sigma_v_true, r, t);
    const double sigma_e = (v_true / e) * delta * sigma_v_true;

    const KmvCalibration cal = kmv_calibrate(e, sigma_e, debt, r, t);
    EXPECT_TRUE(cal.converged);
    EXPECT_NEAR(cal.asset_value, v_true, 1e-6);
    EXPECT_NEAR(cal.asset_vol, sigma_v_true, 1e-6);
}
