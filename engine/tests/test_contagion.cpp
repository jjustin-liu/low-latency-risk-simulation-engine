#include <gtest/gtest.h>

#include <vector>

#include "risksim/contagion.hpp"
#include "risksim/network.hpp"

using namespace risksim;

namespace {
// Two banks: bank 0 owes bank 1 exactly 100; bank 1 owes nothing.
ExposureNetwork two_bank_line() {
    ExposureNetwork net;
    net.n = 2;
    net.liabilities = Matrix(2, 2);
    net.liabilities(0, 1) = 100.0;
    net.external_assets = {100.0, 0.0};
    net.equity = {8.0, 8.0};
    return net;
}
}  // namespace

TEST(EisenbergNoe, NoShockNoDefault) {
    const ExposureNetwork net = two_bank_line();
    const std::vector<double> no_shock{0.0, 0.0};
    const ContagionResult r = EisenbergNoe{}.propagate(net, no_shock);
    EXPECT_EQ(r.num_defaults, 0);
    EXPECT_NEAR(r.system_loss, 0.0, 1e-9);
}

TEST(EisenbergNoe, HandComputedClearing) {
    // 60% external-asset shock to bank 0 -> exogenous cash 100*0.4 = 40, so it
    // pays only 40 of its 100 obligation. Shortfall 60, one default.
    const ExposureNetwork net = two_bank_line();
    const std::vector<double> shock{0.6, 0.0};
    const ContagionResult r = EisenbergNoe{}.propagate(net, shock);
    EXPECT_EQ(r.num_defaults, 1);
    EXPECT_EQ(r.defaulted[0], 1);
    EXPECT_NEAR(r.node_loss[0], 60.0, 1e-6);  // p-bar 100 - payment 40
    EXPECT_NEAR(r.system_loss, 60.0, 1e-6);
}

TEST(EisenbergNoe, PicardMatchesFictitiousDefault) {
    // Two independent solvers must agree on a non-trivial network.
    CorePeripheryParams params;
    params.n_core = 4;
    params.n_periphery = 26;
    params.seed = 21;
    const ExposureNetwork net = generate_core_periphery(params);

    // Heavy shock to the core to force real cascades.
    std::vector<double> shock(net.n, 0.0);
    for (std::size_t i = 0; i < params.n_core; ++i) shock[i] = 0.9;

    const ContagionResult picard = EisenbergNoe{}.propagate(net, shock);

    std::vector<double> e_shocked(net.n);
    for (std::size_t i = 0; i < net.n; ++i)
        e_shocked[i] = net.external_assets[i] * (1.0 - shock[i]);
    const std::vector<double> p_fict = eisenberg_noe_fictitious(net, e_shocked);

    const std::vector<double> pbar = net.liability_marginals();
    for (std::size_t i = 0; i < net.n; ++i) {
        const double p_picard = pbar[i] - picard.node_loss[i];
        EXPECT_NEAR(p_picard, p_fict[i], 1e-4) << "node " << i;
    }
}

TEST(Furfine, ThresholdCascadePropagates) {
    // Chain: bank 2 owes bank 1 (8), bank 1 owes bank 0 (8). Each has equity 5
    // and external assets 10. A full shock to bank 2 topples all three.
    ExposureNetwork net;
    net.n = 3;
    net.liabilities = Matrix(3, 3);
    net.liabilities(2, 1) = 8.0;
    net.liabilities(1, 0) = 8.0;
    net.external_assets = {10.0, 10.0, 10.0};
    net.equity = {5.0, 5.0, 5.0};
    const std::vector<double> shock{0.0, 0.0, 1.0};

    EXPECT_EQ(Furfine{1.0}.propagate(net, shock).num_defaults, 3);
    // With zero loss-given-default, contagion cannot spread past the first bank.
    EXPECT_EQ(Furfine{0.0}.propagate(net, shock).num_defaults, 1);
}

TEST(DebtRank, DynamicalDominatesOriginalOnACycle) {
    // Directed distress cycle 0 -> 1 -> 2 -> 0. Reverberation (dynamical) must
    // capture at least as much distress as the single-pass original.
    ExposureNetwork net;
    net.n = 3;
    net.liabilities = Matrix(3, 3);
    net.liabilities(0, 1) = 10.0;
    net.liabilities(1, 2) = 10.0;
    net.liabilities(2, 0) = 10.0;
    net.external_assets = {100.0, 100.0, 100.0};
    net.equity = {20.0, 20.0, 20.0};
    const std::vector<double> shock{0.1, 0.0, 0.0};

    const ContagionResult orig = DebtRank{/*dynamical=*/false}.propagate(net, shock);
    const ContagionResult dyn = DebtRank{/*dynamical=*/true}.propagate(net, shock);

    EXPECT_GT(orig.system_loss, 0.0);
    EXPECT_GE(dyn.system_loss, orig.system_loss - 1e-9);
    EXPECT_GT(dyn.system_loss, orig.system_loss);  // strict on this topology
}

TEST(DebtRank, NoShockNoDistress) {
    ExposureNetwork net;
    net.n = 3;
    net.liabilities = Matrix(3, 3);
    net.liabilities(0, 1) = 10.0;
    net.external_assets = {100.0, 100.0, 100.0};
    net.equity = {20.0, 20.0, 20.0};
    const std::vector<double> no_shock{0.0, 0.0, 0.0};
    EXPECT_NEAR(DebtRank{true}.propagate(net, no_shock).system_loss, 0.0, 1e-12);
}
