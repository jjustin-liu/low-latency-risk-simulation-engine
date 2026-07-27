#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "risksim/risk.hpp"
#include "risksim/rng.hpp"

using namespace risksim;

TEST(Welford, MeanAndVarianceExact) {
    // Dataset {2,4,4,4,5,5,7,9}: mean 5, sum of squared deviations 32.
    Welford w;
    for (double x : {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}) w.add(x);
    EXPECT_EQ(w.count(), 8u);
    EXPECT_NEAR(w.mean(), 5.0, 1e-15);
    EXPECT_NEAR(w.variance(), 32.0 / 7.0, 1e-13);  // sample variance (n-1)
    EXPECT_NEAR(w.stddev(), std::sqrt(32.0 / 7.0), 1e-13);
}

TEST(Welford, MatchesTwoPassOnRandomData) {
    const Philox rng(7);
    Welford w;
    std::vector<double> xs;
    for (std::uint64_t i = 0; i < 100000; ++i) {
        const double x = 3.0 + 2.0 * rng.normal(i);
        w.add(x);
        xs.push_back(x);
    }
    double mean = 0.0;
    for (double x : xs) mean += x;
    mean /= static_cast<double>(xs.size());
    double m2 = 0.0;
    for (double x : xs) m2 += (x - mean) * (x - mean);
    const double var = m2 / static_cast<double>(xs.size() - 1);
    EXPECT_NEAR(w.mean(), mean, 1e-9);
    EXPECT_NEAR(w.variance(), var, 1e-6);
}

TEST(TailEstimator, ExactAgainstEmpirical) {
    // Fixed sample; streaming heap must equal the order-statistic reference.
    std::vector<double> losses;
    const Philox rng(99);
    const std::size_t n = 50000;
    TailEstimator est(n, 0.975);
    for (std::size_t i = 0; i < n; ++i) {
        const double loss = rng.normal(i);
        losses.push_back(loss);
        est.add(loss);
    }
    const VarEs ref = empirical_var_es(losses, 0.975);
    EXPECT_NEAR(est.value_at_risk(), ref.var, 1e-12);
    EXPECT_NEAR(est.expected_shortfall(), ref.es, 1e-12);
}

TEST(VarEs, MonteCarloConvergesToGaussianClosedForm) {
    // Losses ~ Normal(mu, sigma). Empirical VaR/ES must approach the closed form.
    const double mu = 1.0;
    const double sigma = 2.5;
    const double alpha = 0.975;
    const Philox rng(2024);
    const std::size_t n = 2'000'000;

    TailEstimator est(n, alpha);
    for (std::size_t i = 0; i < n; ++i) est.add(mu + sigma * rng.normal(i));

    const VarEs closed = gaussian_var_es(mu, sigma, alpha);
    // Tail has ~50k samples; a ~1% tolerance is comfortable.
    EXPECT_NEAR(est.value_at_risk(), closed.var, 0.01 * std::fabs(closed.var));
    EXPECT_NEAR(est.expected_shortfall(), closed.es, 0.01 * std::fabs(closed.es));
    // ES must exceed VaR (it is the mean beyond the quantile).
    EXPECT_GT(est.expected_shortfall(), est.value_at_risk());
}

TEST(VarEs, ExpectedShortfallExceedsVar) {
    // Coherence sanity: ES >= VaR always.
    std::vector<double> losses;
    const Philox rng(5);
    for (std::size_t i = 0; i < 10000; ++i) losses.push_back(rng.normal(i));
    const VarEs r = empirical_var_es(losses, 0.99);
    EXPECT_GE(r.es, r.var);
}
