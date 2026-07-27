#include "risksim/distributions.hpp"

#include <cmath>
#include <limits>

namespace risksim {

namespace {
constexpr double kInvSqrt2Pi = 0.398942280401432677939946059934;  // 1/sqrt(2*pi)
constexpr double kSqrt1_2 = 0.707106781186547524400844362105;     // 1/sqrt(2)
constexpr double kSqrt2Pi = 2.506628274631000502415765284811;     // sqrt(2*pi)
}  // namespace

double norm_pdf(double x) noexcept { return kInvSqrt2Pi * std::exp(-0.5 * x * x); }

double norm_cdf(double x) noexcept { return 0.5 * std::erfc(-x * kSqrt1_2); }

double norm_inv_cdf(double p) noexcept {
    if (p <= 0.0) return -std::numeric_limits<double>::infinity();
    if (p >= 1.0) return std::numeric_limits<double>::infinity();

    // Peter Acklam's rational approximation.
    static constexpr double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
                                   -2.759285104469687e+02, 1.383577518672690e+02,
                                   -3.066479806614716e+01, 2.506628277459239e+00};
    static constexpr double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
                                   -1.556989798598866e+02, 6.680131188771972e+01,
                                   -1.328068155288572e+01};
    static constexpr double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                                   -2.400758277161838e+00, -2.549732539343734e+00,
                                   4.374664141464968e+00,  2.938163982698783e+00};
    static constexpr double d[] = {7.784695709041462e-03, 3.224671290700398e-01,
                                   2.445134137142996e+00, 3.754408661907416e+00};
    constexpr double p_low = 0.02425;
    constexpr double p_high = 1.0 - p_low;

    double x;
    if (p < p_low) {
        const double q = std::sqrt(-2.0 * std::log(p));
        x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    } else if (p <= p_high) {
        const double q = p - 0.5;
        const double r = q * q;
        x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
            (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
    } else {
        const double q = std::sqrt(-2.0 * std::log(1.0 - p));
        x = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }

    // One Halley refinement step: full double precision.
    const double e = norm_cdf(x) - p;
    const double u = e * kSqrt2Pi * std::exp(0.5 * x * x);
    x = x - u / (1.0 + 0.5 * x * u);
    return x;
}

// Continued-fraction evaluation of the incomplete beta (Numerical Recipes).
static double betacf(double a, double b, double x) noexcept {
    constexpr int kMaxIter = 300;
    constexpr double kEps = 3.0e-14;
    constexpr double kTiny = 1.0e-300;

    const double qab = a + b;
    const double qap = a + 1.0;
    const double qam = a - 1.0;
    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (std::fabs(d) < kTiny) d = kTiny;
    d = 1.0 / d;
    double h = d;

    for (int m = 1; m <= kMaxIter; ++m) {
        const double m2 = 2.0 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < kTiny) d = kTiny;
        c = 1.0 + aa / c;
        if (std::fabs(c) < kTiny) c = kTiny;
        d = 1.0 / d;
        h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < kTiny) d = kTiny;
        c = 1.0 + aa / c;
        if (std::fabs(c) < kTiny) c = kTiny;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < kEps) break;
    }
    return h;
}

double reg_incomplete_beta(double a, double b, double x) noexcept {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    const double lbeta =
        std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) + a * std::log(x) + b * std::log1p(-x);
    const double front = std::exp(lbeta);
    if (x < (a + 1.0) / (a + b + 2.0)) {
        return front * betacf(a, b, x) / a;
    }
    return 1.0 - front * betacf(b, a, 1.0 - x) / b;
}

double students_t_cdf(double x, double nu) noexcept {
    const double xx = nu / (nu + x * x);
    const double tail = 0.5 * reg_incomplete_beta(0.5 * nu, 0.5, xx);
    return x > 0.0 ? 1.0 - tail : tail;
}

double students_t_inv_cdf(double p, double nu) noexcept {
    if (p <= 0.0) return -std::numeric_limits<double>::infinity();
    if (p >= 1.0) return std::numeric_limits<double>::infinity();
    if (p == 0.5) return 0.0;

    // Bracket using the normal quantile scaled for heavier tails, then bisect
    // with a Newton assist (t pdf is available in closed form).
    const double sign = p < 0.5 ? -1.0 : 1.0;
    double lo = 0.0;
    double hi = 1.0;
    // Expand hi until it brackets the (upper-tail) target.
    const double target = p < 0.5 ? 1.0 - p : p;
    while (students_t_cdf(hi, nu) < target && hi < 1.0e12) hi *= 2.0;

    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double c = students_t_cdf(mid, nu);
        if (c < target) {
            lo = mid;
        } else {
            hi = mid;
        }
        if (hi - lo < 1.0e-14 * (1.0 + hi)) break;
    }
    return sign * 0.5 * (lo + hi);
}

}  // namespace risksim
