// Scalar reference probability distributions.
//
// These are the deterministic, portable ground-truth implementations. The SIMD
// runtime approximates them (e.g. a polynomial erf) and is tested against these
// within a documented tolerance; the Python research bindings call these exact
// functions so the notebooks validate the shipping code.
#ifndef RISKSIM_DISTRIBUTIONS_HPP
#define RISKSIM_DISTRIBUTIONS_HPP

namespace risksim {

// ---- Standard normal N(0,1) ------------------------------------------------

// Probability density function.
[[nodiscard]] double norm_pdf(double x) noexcept;

// Cumulative distribution function, computed via std::erfc for full accuracy.
[[nodiscard]] double norm_cdf(double x) noexcept;

// Inverse CDF (quantile / probit). Acklam's rational approximation refined with
// one Halley step, accurate to full double precision for p in (0,1).
// Returns -inf at p<=0 and +inf at p>=1.
[[nodiscard]] double norm_inv_cdf(double p) noexcept;

// ---- Student's t with `nu` degrees of freedom ------------------------------

// CDF via the regularized incomplete beta function.
[[nodiscard]] double students_t_cdf(double x, double nu) noexcept;

// Inverse CDF via bracketed bisection/Newton on students_t_cdf.
[[nodiscard]] double students_t_inv_cdf(double p, double nu) noexcept;

// ---- Special functions (exposed for testing / reuse) -----------------------

// Regularized incomplete beta I_x(a,b). Numerical-Recipes continued fraction.
[[nodiscard]] double reg_incomplete_beta(double a, double b, double x) noexcept;

}  // namespace risksim

#endif  // RISKSIM_DISTRIBUTIONS_HPP
