// Copulas: dependence structure separated from marginals (Sklar's theorem).
//
// Both copulas produce correlated uniforms u in (0,1)^d that a caller maps
// through per-asset marginals. The Gaussian copula has ZERO tail dependence
// (extreme joint events are asymptotically independent) -- the model blamed for
// underpricing 2008 CDO tail risk. The Student-t copula has positive, symmetric
// tail dependence controlled by the degrees of freedom nu, which is what makes
// systemic co-crashes materially more likely. Quantifying that gap is a headline
// result of the project.
#ifndef RISKSIM_COPULA_HPP
#define RISKSIM_COPULA_HPP

#include <cstddef>
#include <span>

#include "risksim/linalg.hpp"
#include "risksim/rng.hpp"

namespace risksim {

// Marsaglia-Tsang gamma(shape, scale=1) sampler; consumes variates from the
// cursor. Exposed for the t-copula and for testing.
[[nodiscard]] double sample_gamma(DrawCursor& cursor, double shape) noexcept;

// Chi-squared with `df` degrees of freedom = 2 * Gamma(df/2, 1).
[[nodiscard]] double sample_chi_squared(DrawCursor& cursor, double df) noexcept;

class GaussianCopula {
public:
    // `corr` must be a valid (SPD) correlation matrix; its Cholesky factor is
    // precomputed and reused across all samples.
    explicit GaussianCopula(const Matrix& corr);

    void sample(DrawCursor& cursor, std::span<double> u) const;
    [[nodiscard]] std::size_t dim() const noexcept { return l_.rows(); }

private:
    Matrix l_;  // lower Cholesky factor of the correlation matrix
};

class StudentTCopula {
public:
    StudentTCopula(const Matrix& corr, double nu);

    void sample(DrawCursor& cursor, std::span<double> u) const;
    [[nodiscard]] std::size_t dim() const noexcept { return l_.rows(); }
    [[nodiscard]] double nu() const noexcept { return nu_; }

private:
    Matrix l_;
    double nu_;
};

// ---- Calibration helpers ---------------------------------------------------

// Pearson correlation implied by Kendall's tau for an elliptical copula:
//   rho = sin(pi * tau / 2)
// Robust to the marginals, unlike a raw Pearson estimate on returns.
[[nodiscard]] double kendall_tau_to_pearson(double tau) noexcept;

// Lower = upper tail-dependence coefficient of a bivariate t-copula:
//   lambda = 2 * T_{nu+1}( -sqrt( (nu+1)(1-rho) / (1+rho) ) )
// Strictly positive for finite nu; -> 0 as nu -> infinity (the Gaussian limit).
[[nodiscard]] double t_copula_tail_dependence(double rho, double nu) noexcept;

}  // namespace risksim

#endif  // RISKSIM_COPULA_HPP
