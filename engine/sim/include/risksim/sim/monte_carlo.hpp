// Systemic-risk Monte Carlo: the orchestration layer.
//
// Each path draws a correlated cross-section of bank asset returns from a copula
// (Gaussian OR Student-t, SAME marginals), turns the downside into a per-bank
// external-asset shock, runs a contagion model, and records the system-wide
// loss. The distribution of that loss over paths yields systemic VaR/ES -- and
// swapping only the copula quantifies how much tail dependence matters.
//
// The engine holds NO financial formulas; it schedules the pure `risksim-core`
// kernels over disjoint Philox counter ranges (path index = stream). Losses are
// written to a path-indexed array, so the reduction is bit-identical regardless
// of thread count -- the promise the counter-based RNG exists to keep.
#ifndef RISKSIM_SIM_MONTE_CARLO_HPP
#define RISKSIM_SIM_MONTE_CARLO_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#include "risksim/contagion.hpp"
#include "risksim/distributions.hpp"
#include "risksim/network.hpp"
#include "risksim/risk.hpp"
#include "risksim/rng.hpp"

namespace risksim::sim {

struct Config {
    std::uint64_t num_paths = 100000;
    double alpha = 0.975;  // Basel FRTB ES level
    std::uint64_t seed = 0x0071715C0FFEEULL;
    bool antithetic = true;  // pair each draw with its radial reflection
    int threads = 0;         // 0 => hardware concurrency
    std::uint32_t hist_bins = 48;  // loss-distribution histogram resolution (0 => none)
};

struct Result {
    double var = 0.0;         // systemic loss VaR at alpha
    double es = 0.0;          // systemic loss Expected Shortfall at alpha
    double mean_loss = 0.0;   // plain MC mean
    double std_error = 0.0;   // standard error of the mean
    double loss_min = 0.0;
    double loss_max = 0.0;
    std::uint64_t samples = 0;  // paths * (antithetic ? 2 : 1)

    // Variance-reduction diagnostics (control variate = direct/pre-contagion loss).
    double cv_mean = 0.0;             // control-variate-adjusted mean
    double cv_std_error = 0.0;        // its (smaller) standard error
    double variance_reduction = 1.0;  // plain Var / cv Var (>1 means it helped)

    // Systemic-loss histogram over [0, hist_max] with hist_bins equal-width bins.
    // hist[k] = fraction of samples whose loss falls in bin k (sums to ~1). Empty
    // if cfg.hist_bins == 0. Feeds the frontend loss-distribution chart with the
    // VaR/ES cut-lines drawn on the same axis.
    std::vector<float> loss_hist;
    double hist_max = 0.0;
};

class SystemicMonteCarlo {
public:
    SystemicMonteCarlo(const ExposureNetwork& net, std::span<const double> node_vol)
        : net_(net), node_vol_(node_vol.begin(), node_vol.end()) {
        // E[direct loss] under the normal marginal (uncapped): for return
        // r_i = vol_i * z, E[max(0,-r_i)] = vol_i * phi(0) = vol_i / sqrt(2pi).
        constexpr double kInvSqrt2Pi = 0.3989422804014327;
        for (std::size_t i = 0; i < net_.n; ++i)
            expected_direct_loss_ += net_.external_assets[i] * node_vol_[i] * kInvSqrt2Pi;
    }

    template <class Copula>
    [[nodiscard]] Result run(const Copula& copula, const ContagionModel& model,
                             const Config& cfg) const {
        const std::uint64_t n_paths = cfg.num_paths;
        const std::size_t per_path = cfg.antithetic ? 2u : 1u;
        const std::size_t samples = static_cast<std::size_t>(n_paths) * per_path;
        const std::size_t dim = copula.dim();

        std::vector<double> losses(samples);   // system loss per sample
        std::vector<double> directs(samples);  // direct (pre-contagion) loss per sample

        int nthreads = cfg.threads > 0 ? cfg.threads
                                       : static_cast<int>(std::thread::hardware_concurrency());
        if (nthreads < 1) nthreads = 1;
        nthreads = std::min<int>(nthreads, static_cast<int>(std::max<std::uint64_t>(1, n_paths)));

        auto worker = [&](std::uint64_t lo, std::uint64_t hi) {
            std::vector<double> u(dim);
            std::vector<double> shock(net_.n);
            for (std::uint64_t p = lo; p < hi; ++p) {
                DrawCursor cursor(cfg.seed, p);
                copula.sample(cursor, u);
                const std::size_t base = static_cast<std::size_t>(p) * per_path;
                evaluate(u, /*reflect=*/false, model, shock, losses[base], directs[base]);
                if (cfg.antithetic)
                    evaluate(u, /*reflect=*/true, model, shock, losses[base + 1],
                             directs[base + 1]);
            }
        };

        if (nthreads == 1) {
            worker(0, n_paths);
        } else {
            std::vector<std::thread> pool;
            const std::uint64_t chunk = (n_paths + static_cast<std::uint64_t>(nthreads) - 1) /
                                        static_cast<std::uint64_t>(nthreads);
            for (int t = 0; t < nthreads; ++t) {
                const std::uint64_t lo = std::min(n_paths, static_cast<std::uint64_t>(t) * chunk);
                const std::uint64_t hi = std::min(n_paths, lo + chunk);
                if (lo < hi) pool.emplace_back(worker, lo, hi);
            }
            for (auto& th : pool) th.join();
        }

        return reduce(losses, directs, cfg.alpha, cfg.hist_bins);
    }

private:
    // Map copula uniforms -> per-bank shocks (via the shared normal marginal),
    // run the contagion model, and return (system loss, direct loss).
    void evaluate(std::span<const double> u, bool reflect, const ContagionModel& model,
                  std::vector<double>& shock, double& out_loss, double& out_direct) const {
        double direct = 0.0;
        for (std::size_t i = 0; i < net_.n; ++i) {
            const double ui = reflect ? (1.0 - u[i]) : u[i];  // radial antithetic
            const double z = norm_inv_cdf(ui);
            const double ret = node_vol_[i] * z;            // asset return
            const double s = std::clamp(-ret, 0.0, 1.0);    // downside as a fractional shock
            shock[i] = s;
            direct += s * net_.external_assets[i];
        }
        out_direct = direct;
        out_loss = model.propagate(net_, shock).system_loss;
    }

    Result reduce(std::span<const double> losses, std::span<const double> directs, double alpha,
                  std::uint32_t hist_bins) const {
        Result r;
        r.samples = losses.size();
        if (losses.empty()) return r;

        Welford w;
        double sum_c = 0.0, sum_cc = 0.0, sum_lc = 0.0, sum_l = 0.0;
        double lo = losses[0], hi = losses[0];
        for (std::size_t k = 0; k < losses.size(); ++k) {
            const double l = losses[k];
            const double c = directs[k];
            w.add(l);
            sum_l += l;
            sum_c += c;
            sum_cc += c * c;
            sum_lc += l * c;
            lo = std::min(lo, l);
            hi = std::max(hi, l);
        }
        const double n = static_cast<double>(losses.size());
        r.mean_loss = w.mean();
        r.std_error = w.standard_error();
        r.loss_min = lo;
        r.loss_max = hi;

        const VarEs ve = empirical_var_es(losses, alpha);
        r.var = ve.var;
        r.es = ve.es;

        // Control variate: L* = L - beta*(C - E[C]), beta = Cov(L,C)/Var(C).
        const double mean_c = sum_c / n;
        const double var_c = std::max(0.0, sum_cc / n - mean_c * mean_c);
        const double cov_lc = sum_lc / n - (sum_l / n) * mean_c;
        if (var_c > 0.0) {
            const double beta = cov_lc / var_c;
            r.cv_mean = r.mean_loss - beta * (mean_c - expected_direct_loss_);
            const double var_l = w.variance();
            const double resid_var = std::max(0.0, var_l - beta * beta * var_c);
            r.cv_std_error = std::sqrt(resid_var / n);
            r.variance_reduction = resid_var > 0.0 ? var_l / resid_var : 1.0;
        } else {
            r.cv_mean = r.mean_loss;
            r.cv_std_error = r.std_error;
        }

        // Loss-distribution histogram over [0, hi]. Losses are non-negative, so
        // the left edge is 0; the right edge is the worst observed loss. Stored as
        // per-bin sample fractions so the chart is scale-free and the bars sum to 1.
        if (hist_bins > 0) {
            const std::size_t bins = hist_bins;
            r.loss_hist.assign(bins, 0.0f);
            r.hist_max = hi > 0.0 ? hi : 1.0;
            const double inv_width = static_cast<double>(bins) / r.hist_max;
            for (double l : losses) {
                auto b = static_cast<std::size_t>(std::max(0.0, l) * inv_width);
                if (b >= bins) b = bins - 1;  // the max lands in the last bin
                r.loss_hist[b] += 1.0f;
            }
            const float inv_n = 1.0f / static_cast<float>(losses.size());
            for (float& c : r.loss_hist) c *= inv_n;
        }
        return r;
    }

    const ExposureNetwork& net_;
    std::vector<double> node_vol_;
    double expected_direct_loss_ = 0.0;
};

}  // namespace risksim::sim

#endif  // RISKSIM_SIM_MONTE_CARLO_HPP
