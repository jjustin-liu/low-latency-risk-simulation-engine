#include "risksim/copula.hpp"

#include <cmath>
#include <vector>

#include "risksim/distributions.hpp"

namespace risksim {

double sample_gamma(DrawCursor& cursor, double shape) noexcept {
    // Marsaglia & Tsang (2000). For shape < 1, use the boosting trick
    // Gamma(a) = Gamma(a+1) * U^(1/a).
    if (shape < 1.0) {
        const double u = cursor.uniform();
        return sample_gamma(cursor, shape + 1.0) * std::pow(u, 1.0 / shape);
    }
    const double d = shape - 1.0 / 3.0;
    const double c = 1.0 / std::sqrt(9.0 * d);
    for (;;) {
        double x;
        double v;
        do {
            x = cursor.normal();
            v = 1.0 + c * x;
        } while (v <= 0.0);
        v = v * v * v;
        const double u = cursor.uniform();
        const double x2 = x * x;
        if (u < 1.0 - 0.0331 * x2 * x2) return d * v;
        if (std::log(u) < 0.5 * x2 + d * (1.0 - v + std::log(v))) return d * v;
    }
}

double sample_chi_squared(DrawCursor& cursor, double df) noexcept {
    return 2.0 * sample_gamma(cursor, 0.5 * df);
}

// ---- Gaussian copula -------------------------------------------------------

GaussianCopula::GaussianCopula(const Matrix& corr) : l_(cholesky(corr)) {}

void GaussianCopula::sample(DrawCursor& cursor, std::span<double> u) const {
    const std::size_t d = dim();
    std::vector<double> z(d);
    for (std::size_t i = 0; i < d; ++i) z[i] = cursor.normal();
    // Correlated normals x = L z (L lower-triangular), then map to uniforms.
    for (std::size_t i = 0; i < d; ++i) {
        double xi = 0.0;
        for (std::size_t j = 0; j <= i; ++j) xi += l_(i, j) * z[j];
        u[i] = norm_cdf(xi);
    }
}

// ---- Student-t copula ------------------------------------------------------

StudentTCopula::StudentTCopula(const Matrix& corr, double nu) : l_(cholesky(corr)), nu_(nu) {}

void StudentTCopula::sample(DrawCursor& cursor, std::span<double> u) const {
    const std::size_t d = dim();
    std::vector<double> z(d);
    for (std::size_t i = 0; i < d; ++i) z[i] = cursor.normal();
    // Common chi-squared mixing variable creates the tail dependence: a small w
    // inflates every coordinate together -> joint extremes.
    const double w = sample_chi_squared(cursor, nu_);
    const double scale = std::sqrt(w / nu_);
    for (std::size_t i = 0; i < d; ++i) {
        double xi = 0.0;
        for (std::size_t j = 0; j <= i; ++j) xi += l_(i, j) * z[j];
        u[i] = students_t_cdf(xi / scale, nu_);
    }
}

// ---- Calibration -----------------------------------------------------------

double kendall_tau_to_pearson(double tau) noexcept {
    constexpr double kPi = 3.141592653589793238462643383279502884;
    return std::sin(0.5 * kPi * tau);
}

double t_copula_tail_dependence(double rho, double nu) noexcept {
    const double arg = -std::sqrt((nu + 1.0) * (1.0 - rho) / (1.0 + rho));
    return 2.0 * students_t_cdf(arg, nu + 1.0);
}

}  // namespace risksim
