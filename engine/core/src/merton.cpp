#include "risksim/merton.hpp"

#include <cmath>

#include "risksim/distributions.hpp"

namespace risksim {

namespace {
struct D1D2 {
    double d1;
    double d2;
};
D1D2 bs_d1d2(double v, double d, double sigma, double r, double t) noexcept {
    const double vol_sqrt_t = sigma * std::sqrt(t);
    const double d1 = (std::log(v / d) + (r + 0.5 * sigma * sigma) * t) / vol_sqrt_t;
    return {d1, d1 - vol_sqrt_t};
}
}  // namespace

double distance_to_default(double v, double d, double sigma, double mu, double t) noexcept {
    return (std::log(v / d) + (mu - 0.5 * sigma * sigma) * t) / (sigma * std::sqrt(t));
}

double merton_pd(double v, double d, double sigma, double mu, double t) noexcept {
    return norm_cdf(-distance_to_default(v, d, sigma, mu, t));
}

double merton_equity_value(double v, double d, double sigma, double r, double t) noexcept {
    const auto [d1, d2] = bs_d1d2(v, d, sigma, r, t);
    return v * norm_cdf(d1) - d * std::exp(-r * t) * norm_cdf(d2);
}

double merton_equity_delta(double v, double d, double sigma, double r, double t) noexcept {
    return norm_cdf(bs_d1d2(v, d, sigma, r, t).d1);
}

KmvCalibration kmv_calibrate(double equity, double equity_vol, double debt, double r, double t,
                             double tol, int max_iter) noexcept {
    // Sensible starting point: assets ~ equity + debt, asset vol scaled by
    // leverage.
    double v = equity + debt;
    double sigma_v = equity_vol * equity / v;
    int it = 0;
    bool converged = false;

    for (; it < max_iter; ++it) {
        // Inner Newton solve: find V such that the call price equals observed
        // equity, holding sigma_v fixed. dE/dV = Phi(d1) > 0, so this is stable.
        for (int inner = 0; inner < 100; ++inner) {
            const double e = merton_equity_value(v, debt, sigma_v, r, t);
            const double delta = merton_equity_delta(v, debt, sigma_v, r, t);
            const double step = (e - equity) / delta;
            v -= step;
            if (v <= 0.0) v = 0.5 * (v + step);  // guard against overshoot
            if (std::fabs(step) < tol * v) break;
        }
        const double delta = merton_equity_delta(v, debt, sigma_v, r, t);
        const double sigma_v_new = equity_vol * equity / (delta * v);
        const bool done = std::fabs(sigma_v_new - sigma_v) < tol;
        sigma_v = sigma_v_new;
        if (done) {
            converged = true;
            ++it;
            break;
        }
    }
    return {v, sigma_v, it, converged};
}

}  // namespace risksim
