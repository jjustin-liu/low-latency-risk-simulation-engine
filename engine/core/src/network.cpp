#include "risksim/network.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include "risksim/rng.hpp"

namespace risksim {

double ExposureNetwork::total_liabilities(std::size_t i) const {
    double s = 0.0;
    for (std::size_t j = 0; j < n; ++j) s += liabilities(i, j);
    return s;
}

double ExposureNetwork::total_interbank_assets(std::size_t i) const {
    double s = 0.0;
    for (std::size_t j = 0; j < n; ++j) s += liabilities(j, i);
    return s;
}

Matrix ExposureNetwork::relative_liabilities() const {
    Matrix pi(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        const double pbar = total_liabilities(i);
        if (pbar <= 0.0) continue;
        for (std::size_t j = 0; j < n; ++j) pi(i, j) = liabilities(i, j) / pbar;
    }
    return pi;
}

std::vector<double> ExposureNetwork::liability_marginals() const {
    std::vector<double> m(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) m[i] = total_liabilities(i);
    return m;
}

std::vector<double> ExposureNetwork::asset_marginals() const {
    std::vector<double> m(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) m[i] = total_interbank_assets(i);
    return m;
}

// ---- Reconstruction --------------------------------------------------------

Matrix reconstruct_max_entropy(std::span<const double> assets_in,
                               std::span<const double> liabilities_out, int max_iter, double tol) {
    const std::size_t n = liabilities_out.size();
    if (assets_in.size() != n) throw std::invalid_argument("reconstruct: marginal size mismatch");

    // Independence prior x_ij ~ l_i * a_j / T, zero diagonal.
    const double total = std::accumulate(liabilities_out.begin(), liabilities_out.end(), 0.0);
    Matrix x(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j || total <= 0.0) continue;
            x(i, j) = liabilities_out[i] * assets_in[j] / total;
        }
    }

    // Iterative proportional fitting: alternately rescale rows to l_i and
    // columns to a_j. Converges to the unique max-entropy matrix.
    for (int iter = 0; iter < max_iter; ++iter) {
        double max_err = 0.0;
        // Rows -> liabilities_out.
        for (std::size_t i = 0; i < n; ++i) {
            double rs = 0.0;
            for (std::size_t j = 0; j < n; ++j) rs += x(i, j);
            if (rs <= 0.0) continue;
            const double scale = liabilities_out[i] / rs;
            for (std::size_t j = 0; j < n; ++j) x(i, j) *= scale;
        }
        // Columns -> assets_in.
        for (std::size_t j = 0; j < n; ++j) {
            double cs = 0.0;
            for (std::size_t i = 0; i < n; ++i) cs += x(i, j);
            if (cs <= 0.0) continue;
            const double scale = assets_in[j] / cs;
            for (std::size_t i = 0; i < n; ++i) x(i, j) *= scale;
        }
        // Convergence: worst row-marginal residual.
        for (std::size_t i = 0; i < n; ++i) {
            double rs = 0.0;
            for (std::size_t j = 0; j < n; ++j) rs += x(i, j);
            max_err = std::max(max_err, std::fabs(rs - liabilities_out[i]));
        }
        if (max_err < tol) break;
    }
    return x;
}

Matrix reconstruct_min_density(std::span<const double> assets_in,
                               std::span<const double> liabilities_out) {
    const std::size_t n = liabilities_out.size();
    if (assets_in.size() != n) throw std::invalid_argument("reconstruct: marginal size mismatch");
    Matrix x(n, n);
    std::vector<double> supply(liabilities_out.begin(), liabilities_out.end());  // remaining l_i
    std::vector<double> demand(assets_in.begin(), assets_in.end());              // remaining a_j

    // Greedy: repeatedly match the largest remaining lender to the largest
    // remaining borrower (i != j), placing min(supply, demand). Each step zeroes
    // at least one side, so at most O(n) links -> sparse.
    constexpr double kEps = 1e-12;
    const std::size_t max_steps = 8 * n + 8;
    for (std::size_t step = 0; step < max_steps; ++step) {
        std::size_t bi = n;
        double bs = kEps;
        for (std::size_t i = 0; i < n; ++i)
            if (supply[i] > bs) {
                bs = supply[i];
                bi = i;
            }
        if (bi == n) break;  // all supply placed

        std::size_t bj = n;
        double bd = kEps;
        for (std::size_t j = 0; j < n; ++j)
            if (demand[j] > bd && j != bi) {
                bd = demand[j];
                bj = j;
            }

        if (bj != n) {
            const double amt = std::min(supply[bi], demand[bj]);
            x(bi, bj) += amt;
            supply[bi] -= amt;
            demand[bj] -= amt;
            continue;
        }

        // Stuck: the only remaining demand is bank bi's own column, which we
        // cannot fill without a self-loop. Reroute through an existing
        // off-diagonal edge (a,b): move flow so that `a` lends to bi and bi
        // lends to `b`, preserving every marginal and the diagonal ban.
        double rem = std::min(supply[bi], demand[bi]);
        bool routed = false;
        for (std::size_t a = 0; a < n && rem > kEps; ++a) {
            if (a == bi) continue;
            for (std::size_t b = 0; b < n && rem > kEps; ++b) {
                if (b == bi || x(a, b) <= kEps) continue;
                const double d = std::min(rem, x(a, b));
                x(a, b) -= d;
                x(a, bi) += d;
                x(bi, b) += d;
                supply[bi] -= d;
                demand[bi] -= d;
                rem -= d;
                routed = true;
            }
        }
        if (!routed) break;  // pathological (e.g. n < 3); leave residual
    }
    return x;
}

// ---- Generator -------------------------------------------------------------

ExposureNetwork generate_core_periphery(const CorePeripheryParams& p) {
    const std::size_t n = p.n_core + p.n_periphery;
    ExposureNetwork net;
    net.n = n;
    net.liabilities = Matrix(n, n);
    net.external_assets.assign(n, 0.0);
    net.equity.assign(n, 0.0);

    DrawCursor cursor(p.seed, 0xC0FFEE);
    auto is_core = [&](std::size_t idx) { return idx < p.n_core; };

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            double prob;
            if (is_core(i) && is_core(j)) {
                prob = p.p_core_core;
            } else if (is_core(i) != is_core(j)) {
                prob = p.p_core_periphery;
            } else {
                prob = p.p_periphery_periphery;
            }
            if (cursor.uniform() < prob) {
                const double w = std::exp(p.log_exposure_mean + p.log_exposure_sd * cursor.normal());
                net.liabilities(i, j) = w;
            }
        }
    }

    // Balance sheets: give each bank enough exogenous cash to cover its
    // obligations at baseline, and set equity to a capital fraction of assets.
    for (std::size_t i = 0; i < n; ++i) {
        const double pbar = net.total_liabilities(i);
        const double ib_assets = net.total_interbank_assets(i);
        net.external_assets[i] = pbar;  // baseline-solvent exogenous inflow
        const double total_assets = ib_assets + net.external_assets[i];
        net.equity[i] = p.capital_ratio * total_assets;
    }
    return net;
}

}  // namespace risksim
