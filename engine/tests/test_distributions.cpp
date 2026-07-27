#include <gtest/gtest.h>

#include <cmath>

#include "risksim/distributions.hpp"

using namespace risksim;

TEST(Normal, PdfAndCdfKnownValues) {
    EXPECT_NEAR(norm_pdf(0.0), 0.3989422804014327, 1e-15);
    EXPECT_NEAR(norm_cdf(0.0), 0.5, 1e-15);
    // Phi(1.959963985) == 0.975 to high precision.
    EXPECT_NEAR(norm_cdf(1.959963984540054), 0.975, 1e-12);
    EXPECT_NEAR(norm_cdf(-1.959963984540054), 0.025, 1e-12);
    // Symmetry.
    EXPECT_NEAR(norm_cdf(0.7) + norm_cdf(-0.7), 1.0, 1e-15);
}

TEST(Normal, InverseCdfKnownValues) {
    EXPECT_NEAR(norm_inv_cdf(0.5), 0.0, 1e-12);
    EXPECT_NEAR(norm_inv_cdf(0.975), 1.959963984540054, 1e-9);
    EXPECT_NEAR(norm_inv_cdf(0.025), -1.959963984540054, 1e-9);
    EXPECT_NEAR(norm_inv_cdf(0.99), 2.326347874040841, 1e-9);
}

TEST(Normal, InverseCdfRoundTrip) {
    for (double p = 0.001; p < 1.0; p += 0.001) {
        EXPECT_NEAR(norm_cdf(norm_inv_cdf(p)), p, 1e-12) << "p=" << p;
    }
}

TEST(StudentT, CauchyExactValues) {
    // nu = 1 is the Cauchy distribution: F(x) = 0.5 + atan(x)/pi.
    EXPECT_NEAR(students_t_cdf(0.0, 1.0), 0.5, 1e-12);
    EXPECT_NEAR(students_t_cdf(1.0, 1.0), 0.75, 1e-10);
    EXPECT_NEAR(students_t_cdf(-1.0, 1.0), 0.25, 1e-10);
    EXPECT_NEAR(students_t_inv_cdf(0.75, 1.0), 1.0, 1e-8);
}

TEST(StudentT, KnownQuantiles) {
    // Classic table values.
    EXPECT_NEAR(students_t_cdf(0.0, 10.0), 0.5, 1e-12);
    EXPECT_NEAR(students_t_inv_cdf(0.975, 10.0), 2.228138852, 1e-6);
    EXPECT_NEAR(students_t_inv_cdf(0.975, 30.0), 2.042272456, 1e-6);
    // Round trip.
    EXPECT_NEAR(students_t_cdf(students_t_inv_cdf(0.9, 5.0), 5.0), 0.9, 1e-9);
    // Heavy tails: t has more mass in the tail than the normal at the same x.
    EXPECT_LT(students_t_cdf(2.0, 3.0), norm_cdf(2.0));
}

TEST(IncompleteBeta, Symmetry) {
    // I_x(a,b) = 1 - I_{1-x}(b,a).
    for (double x = 0.1; x < 1.0; x += 0.1) {
        EXPECT_NEAR(reg_incomplete_beta(2.0, 3.0, x),
                    1.0 - reg_incomplete_beta(3.0, 2.0, 1.0 - x), 1e-12);
    }
    // I_x(1,1) = x (uniform).
    EXPECT_NEAR(reg_incomplete_beta(1.0, 1.0, 0.37), 0.37, 1e-12);
}
