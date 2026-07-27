// Streaming risk estimators.
//
// Loss convention: we track *losses*, where a larger value is worse, and VaR/ES
// are upper-tail statistics. VaR_alpha is the alpha-quantile of the loss
// distribution; ES_alpha (Expected Shortfall / CVaR) is the mean loss in the
// worst (1 - alpha) fraction. ES is coherent (subadditive) where VaR is not,
// and 97.5% ES is the Basel FRTB market-risk standard.
#ifndef RISKSIM_RISK_HPP
#define RISKSIM_RISK_HPP

#include <cstddef>
#include <span>
#include <vector>

namespace risksim {

// Welford's online algorithm for numerically stable streaming moments. One pass,
// no catastrophic cancellation (unlike naive sum-of-squares). Used for the loss
// distribution's mean/variance and Monte-Carlo standard-error convergence bands.
class Welford {
public:
    void add(double x) noexcept;

    [[nodiscard]] std::size_t count() const noexcept { return n_; }
    [[nodiscard]] double mean() const noexcept { return n_ ? mean_ : 0.0; }
    [[nodiscard]] double variance() const noexcept;   // sample variance (n-1)
    [[nodiscard]] double stddev() const noexcept;
    // Standard error of the mean estimate = stddev / sqrt(n). The Monte-Carlo
    // convergence metric.
    [[nodiscard]] double standard_error() const noexcept;
    [[nodiscard]] double skewness() const noexcept;
    [[nodiscard]] double excess_kurtosis() const noexcept;

    // Combine two independently-accumulated Welford states (Terriberry's
    // parallel formulas). Lets each Monte-Carlo worker accumulate locally, then
    // merge -- the basis of the multi-threaded engine.
    void merge(const Welford& other) noexcept;

private:
    std::size_t n_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;  // sum of squared deviations
    double m3_ = 0.0;
    double m4_ = 0.0;
};

// Exact streaming VaR + ES via a fixed-capacity min-heap of the worst-k losses.
//
// For n total samples and level alpha, the tail size is k = ceil((1-alpha)*n).
// We retain exactly the k largest losses in a min-heap (so the smallest retained
// loss sits at the root). Then:
//   VaR_alpha = the k-th largest loss          = heap root (min of the tail)
//   ES_alpha  = mean of the k largest losses   = mean of the heap
// Memory is O(k), independent of n, and the result equals the empirical
// (order-statistic) VaR/ES exactly -- no interpolation, no full sort of n.
class TailEstimator {
public:
    // `expected_samples` sizes the tail; `alpha` in (0,1), e.g. 0.975.
    TailEstimator(std::size_t expected_samples, double alpha);

    void add(double loss);

    [[nodiscard]] std::size_t tail_capacity() const noexcept { return k_; }
    [[nodiscard]] std::size_t tail_size() const noexcept { return heap_.size(); }
    [[nodiscard]] double alpha() const noexcept { return alpha_; }
    // The retained worst-k losses (heap order). Used to merge per-thread tails.
    [[nodiscard]] std::span<const double> tail_values() const noexcept { return heap_; }

    // Valid once at least `k` losses have been observed.
    [[nodiscard]] double value_at_risk() const;
    [[nodiscard]] double expected_shortfall() const;

private:
    double alpha_;
    std::size_t k_;
    std::vector<double> heap_;  // min-heap of the k largest losses seen
};

// ---- Batch (non-streaming) reference + closed forms ------------------------

struct VarEs {
    double var;
    double es;
};

// Empirical VaR/ES from a full sample using nth_element (O(n), no full sort).
// The reference the streaming TailEstimator is checked against.
[[nodiscard]] VarEs empirical_var_es(std::span<const double> losses, double alpha);

// Closed-form VaR/ES for a Normal(mu, sigma) loss. Golden-test oracle:
//   VaR = mu + sigma * Phi^{-1}(alpha)
//   ES  = mu + sigma * phi(Phi^{-1}(alpha)) / (1 - alpha)
[[nodiscard]] VarEs gaussian_var_es(double mu, double sigma, double alpha) noexcept;

}  // namespace risksim

#endif  // RISKSIM_RISK_HPP
