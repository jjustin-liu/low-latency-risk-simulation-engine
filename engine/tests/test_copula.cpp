#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include "risksim/copula.hpp"
#include "risksim/distributions.hpp"
#include "risksim/linalg.hpp"
#include "risksim/rng.hpp"

using namespace risksim;

namespace {
Matrix corr2(double rho) {
    Matrix c = Matrix::identity(2);
    c(0, 1) = c(1, 0) = rho;
    return c;
}
}  // namespace

TEST(Copula, KendallTauToPearson) {
    EXPECT_NEAR(kendall_tau_to_pearson(0.0), 0.0, 1e-15);
    EXPECT_NEAR(kendall_tau_to_pearson(1.0), 1.0, 1e-15);
    EXPECT_NEAR(kendall_tau_to_pearson(-1.0), -1.0, 1e-15);
    EXPECT_NEAR(kendall_tau_to_pearson(0.5), std::sin(0.25 * 3.14159265358979323846), 1e-12);
}

TEST(Copula, TCopulaTailDependence) {
    // Positive for finite nu, monotone increasing in rho, decreasing in nu,
    // and vanishing in the Gaussian (nu -> infinity) limit.
    EXPECT_GT(t_copula_tail_dependence(0.5, 4.0), 0.0);
    EXPECT_LT(t_copula_tail_dependence(0.5, 4.0), 1.0);
    EXPECT_GT(t_copula_tail_dependence(0.8, 4.0), t_copula_tail_dependence(0.3, 4.0));
    EXPECT_GT(t_copula_tail_dependence(0.5, 3.0), t_copula_tail_dependence(0.5, 20.0));
    EXPECT_LT(t_copula_tail_dependence(0.5, 1000.0), 0.02);
}

TEST(Gamma, MeanApproximatesShape) {
    // E[Gamma(shape,1)] = shape.
    DrawCursor c(123, 0);
    const double shape = 3.5;
    double sum = 0.0;
    const int n = 200000;
    for (int i = 0; i < n; ++i) sum += sample_gamma(c, shape);
    EXPECT_NEAR(sum / n, shape, 0.02);
}

TEST(GaussianCopula, RecoversCorrelation) {
    const double rho = 0.7;
    const GaussianCopula cop(corr2(rho));
    const std::uint64_t n = 300000;
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, su = 0;
    for (std::uint64_t p = 0; p < n; ++p) {
        DrawCursor c(2024, p);
        std::array<double, 2> u{};
        cop.sample(c, u);
        su += u[0];
        // Map back through the standard-normal marginal to check linear corr.
        const double x = norm_inv_cdf(u[0]);
        const double y = norm_inv_cdf(u[1]);
        sx += x;
        sy += y;
        sxx += x * x;
        syy += y * y;
        sxy += x * y;
    }
    const double dn = static_cast<double>(n);
    const double cov = sxy / dn - (sx / dn) * (sy / dn);
    const double vx = sxx / dn - (sx / dn) * (sx / dn);
    const double vy = syy / dn - (sy / dn) * (sy / dn);
    const double corr = cov / std::sqrt(vx * vy);
    EXPECT_NEAR(corr, rho, 0.01);
    EXPECT_NEAR(su / dn, 0.5, 0.01);  // uniform marginal
}

TEST(StudentTCopula, UniformMarginalsAndHeavierJointTails) {
    const double rho = 0.5;
    const GaussianCopula gauss(corr2(rho));
    const StudentTCopula student(corr2(rho), 3.0);
    const std::uint64_t n = 400000;
    const double q = 0.95;

    auto co_exceedance = [&](auto& cop) {
        std::uint64_t both = 0;
        double su0 = 0;
        for (std::uint64_t p = 0; p < n; ++p) {
            DrawCursor c(777, p);
            std::array<double, 2> u{};
            cop.sample(c, u);
            su0 += u[0];
            if (u[0] > q && u[1] > q) ++both;
        }
        return std::pair{static_cast<double>(both) / static_cast<double>(n), su0 / static_cast<double>(n)};
    };

    const auto [g_both, g_mean] = co_exceedance(gauss);
    const auto [t_both, t_mean] = co_exceedance(student);

    // Marginals remain uniform for both.
    EXPECT_NEAR(g_mean, 0.5, 0.01);
    EXPECT_NEAR(t_mean, 0.5, 0.01);
    // The t-copula puts materially more mass in the joint upper tail at the same
    // correlation -- the systemic-co-crash effect.
    EXPECT_GT(t_both, g_both * 1.2);
}
