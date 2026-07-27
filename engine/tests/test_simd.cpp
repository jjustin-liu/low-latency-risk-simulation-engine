#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "risksim/distributions.hpp"
#include "risksim/simd/simd_kernels.hpp"

using namespace risksim;

TEST(Simd, NormCdfBatchMatchesScalarReference) {
    std::vector<float> x;
    for (int i = -600; i <= 600; ++i) x.push_back(static_cast<float>(i) / 100.0f);  // [-6, 6]
    std::vector<float> simd(x.size());
    std::vector<float> scalar(x.size());

    simd::norm_cdf_batch(x, simd);
    simd::norm_cdf_batch_scalar(x, scalar);

    float max_abs = 0.0f;
    for (std::size_t i = 0; i < x.size(); ++i) {
        // SIMD vs the scalar float reference.
        max_abs = std::max(max_abs, std::fabs(simd[i] - scalar[i]));
        // Both must also track the exact double CDF within the approximation tol.
        const float exact = static_cast<float>(norm_cdf(x[i]));
        EXPECT_NEAR(simd[i], exact, 1e-5f) << "x=" << x[i];
    }
    EXPECT_LT(max_abs, 1e-5f);
}

TEST(Simd, HandlesNonMultipleOfLaneCountTail) {
    // Length not a multiple of any SIMD width exercises the masked tail path.
    std::vector<float> x(103);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = -3.0f + 0.05f * static_cast<float>(i);
    std::vector<float> out(x.size());
    simd::norm_cdf_batch(x, out);
    for (std::size_t i = 0; i < x.size(); ++i) {
        EXPECT_NEAR(out[i], static_cast<float>(norm_cdf(x[i])), 1e-5f);
        EXPECT_GE(out[i], 0.0f);
        EXPECT_LE(out[i], 1.0f);
    }
    SUCCEED() << "SIMD target: " << simd::active_target();
}
