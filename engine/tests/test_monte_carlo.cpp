#include <gtest/gtest.h>

#include <vector>

#include "risksim/copula.hpp"
#include "risksim/linalg.hpp"
#include "risksim/network.hpp"
#include "risksim/sim/monte_carlo.hpp"

using namespace risksim;

namespace {
Matrix equicorrelation(std::size_t n, double rho) {
    Matrix c = Matrix::identity(n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            if (i != j) c(i, j) = rho;
    return c;
}

ExposureNetwork small_network() {
    CorePeripheryParams params;
    params.n_core = 6;
    params.n_periphery = 30;
    params.seed = 99;
    return generate_core_periphery(params);
}
}  // namespace

TEST(MonteCarlo, EsExceedsVarAndControlVariateHelps) {
    const ExposureNetwork net = small_network();
    const std::vector<double> vol(net.n, 0.30);
    const sim::SystemicMonteCarlo engine(net, vol);

    const GaussianCopula copula(equicorrelation(net.n, 0.5));
    const Furfine model(1.0);

    sim::Config cfg;
    cfg.num_paths = 15000;
    cfg.alpha = 0.975;
    cfg.threads = 4;
    const sim::Result r = engine.run(copula, model, cfg);

    EXPECT_EQ(r.samples, 30000u);  // antithetic doubling
    EXPECT_GE(r.es, r.var);
    EXPECT_GT(r.mean_loss, 0.0);
    // Direct loss is a strong control variate -> variance reduction > 1.
    EXPECT_GT(r.variance_reduction, 1.0);
    EXPECT_LE(r.cv_std_error, r.std_error + 1e-12);
}

TEST(MonteCarlo, StudentTProducesHigherSystemicEsThanGaussian) {
    // Identical marginals and correlation; only the copula differs. The
    // t-copula's tail dependence must inflate systemic Expected Shortfall.
    const ExposureNetwork net = small_network();
    const std::vector<double> vol(net.n, 0.30);
    const sim::SystemicMonteCarlo engine(net, vol);
    const Matrix corr = equicorrelation(net.n, 0.5);
    const Furfine model(1.0);

    sim::Config cfg;
    cfg.num_paths = 20000;
    cfg.alpha = 0.975;
    cfg.threads = 4;

    const sim::Result g = engine.run(GaussianCopula(corr), model, cfg);
    const sim::Result t = engine.run(StudentTCopula(corr, 3.0), model, cfg);

    EXPECT_GT(t.es, g.es);
    EXPECT_GT(t.var, g.var);
}

TEST(MonteCarlo, BitReproducibleAcrossThreadCounts) {
    // The headline systems property: counter-based RNG + path-indexed output ->
    // identical VaR/ES no matter how the paths are partitioned across threads.
    const ExposureNetwork net = small_network();
    const std::vector<double> vol(net.n, 0.25);
    const sim::SystemicMonteCarlo engine(net, vol);
    const GaussianCopula copula(equicorrelation(net.n, 0.4));
    const EisenbergNoe model;

    sim::Config cfg;
    cfg.num_paths = 6000;
    cfg.seed = 0xABCDEF;

    cfg.threads = 1;
    const sim::Result one = engine.run(copula, model, cfg);
    cfg.threads = 4;
    const sim::Result four = engine.run(copula, model, cfg);

    EXPECT_EQ(one.var, four.var);              // bit-identical, not just close
    EXPECT_EQ(one.es, four.es);
    EXPECT_EQ(one.mean_loss, four.mean_loss);
    EXPECT_EQ(one.loss_max, four.loss_max);
}
