// Interbank exposure network + bilateral-matrix reconstruction + generators.
//
// The bilateral "who owes whom" matrix is confidential in reality; only the
// marginals (each bank's total interbank assets and liabilities) are public. We
// reconstruct a plausible matrix from the marginals two ways that bracket true
// contagion: maximum-entropy (Sinkhorn-Knopp) UNDER-estimates contagion (it
// spreads exposures maximally), minimum-density OVER-estimates it (sparse,
// concentrated). Reporting a MaxEnt--MinDensity band is the honest answer.
#ifndef RISKSIM_NETWORK_HPP
#define RISKSIM_NETWORK_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "risksim/linalg.hpp"

namespace risksim {

// A bank's balance sheet lives across three arrays plus the liability matrix.
//   liabilities(i, j) = amount bank i owes bank j        (diagonal is zero)
//   external_assets[i] = net exogenous cash inflow e_i   (outside the network)
//   equity[i]          = capital buffer E_i              (loss-absorbing)
struct ExposureNetwork {
    std::size_t n = 0;
    Matrix liabilities;                  // L(i,j): i owes j
    std::vector<double> external_assets;  // e_i
    std::vector<double> equity;           // E_i

    // Total interbank obligations of i (row sum) = p-bar_i in Eisenberg-Noe.
    [[nodiscard]] double total_liabilities(std::size_t i) const;
    // Total interbank claims of i (column sum): what others owe i.
    [[nodiscard]] double total_interbank_assets(std::size_t i) const;
    // Relative liability matrix Pi(i,j) = L(i,j) / p-bar_i (row-stochastic where
    // p-bar_i > 0). The clearing map is defined in terms of Pi.
    [[nodiscard]] Matrix relative_liabilities() const;

    [[nodiscard]] std::vector<double> liability_marginals() const;  // row sums
    [[nodiscard]] std::vector<double> asset_marginals() const;      // column sums
};

// ---- Reconstruction from marginals -----------------------------------------

// Maximum-entropy reconstruction via Sinkhorn-Knopp / iterative proportional
// fitting. Row sums match `liabilities_out`, column sums match `assets_in`, the
// diagonal is forced to zero (no self-lending). Returns the L matrix.
[[nodiscard]] Matrix reconstruct_max_entropy(std::span<const double> assets_in,
                                             std::span<const double> liabilities_out,
                                             int max_iter = 2000, double tol = 1e-10);

// Minimum-density reconstruction (Anand et al. 2015 style greedy): the sparsest
// matrix consistent with the marginals. Concentrated exposures -> upper bound on
// contagion. Pairs with max-entropy to form a band.
[[nodiscard]] Matrix reconstruct_min_density(std::span<const double> assets_in,
                                             std::span<const double> liabilities_out);

// ---- Synthetic network generators ------------------------------------------

struct CorePeripheryParams {
    std::size_t n_core = 8;
    std::size_t n_periphery = 92;
    double p_core_core = 0.7;        // dense core
    double p_core_periphery = 0.25;  // periphery links mostly to core
    double p_periphery_periphery = 0.02;
    double log_exposure_mean = 0.0;  // lognormal edge weights
    double log_exposure_sd = 1.0;
    double capital_ratio = 0.08;  // equity / total assets (Basel-ish)
    std::uint64_t seed = 1;
};

// Core-periphery interbank network (a dense dealer core, sparse periphery), the
// empirically dominant topology. Baseline is solvent (no defaults at zero shock).
[[nodiscard]] ExposureNetwork generate_core_periphery(const CorePeripheryParams& params);

}  // namespace risksim

#endif  // RISKSIM_NETWORK_HPP
