// Merton (1974) structural credit model.
//
// A firm's equity is a call option on its asset value V struck at the face value
// of debt D. Default occurs when V_T < D. This gives a physical-measure default
// probability from the "distance to default", and -- because V and its
// volatility are unobservable -- a calibration (KMV iteration) that backs them
// out of observed equity value and equity volatility.
#ifndef RISKSIM_MERTON_HPP
#define RISKSIM_MERTON_HPP

namespace risksim {

// Distance to default: number of asset-volatility standard deviations the firm
// is from the default point over horizon T, under the physical drift mu.
//   DD = [ln(V/D) + (mu - 0.5*sigma^2)*T] / (sigma*sqrt(T))
[[nodiscard]] double distance_to_default(double asset_value, double debt, double asset_vol,
                                         double mu, double horizon) noexcept;

// Physical-measure probability of default = Phi(-DD).
[[nodiscard]] double merton_pd(double asset_value, double debt, double asset_vol, double mu,
                               double horizon) noexcept;

// Equity value as a Black-Scholes call on assets (risk-neutral drift r):
//   E = V*Phi(d1) - D*exp(-r*T)*Phi(d2)
[[nodiscard]] double merton_equity_value(double asset_value, double debt, double asset_vol,
                                         double r, double horizon) noexcept;

// Hedge ratio dE/dV = Phi(d1); used both by KMV and to translate equity vol into
// asset vol via sigma_E * E = Phi(d1) * sigma_V * V.
[[nodiscard]] double merton_equity_delta(double asset_value, double debt, double asset_vol,
                                         double r, double horizon) noexcept;

struct KmvCalibration {
    double asset_value;
    double asset_vol;
    int iterations;
    bool converged;
};

// KMV/Moody's iterative calibration. Given observed equity value, equity
// volatility, debt face value, rate r and horizon T, solve the coupled system
//   E        = V*Phi(d1) - D*e^{-rT}*Phi(d2)          (equity as a call)
//   sigma_E  = (V/E)*Phi(d1)*sigma_V                  (vol linkage)
// for the latent (V, sigma_V) via fixed-point iteration.
[[nodiscard]] KmvCalibration kmv_calibrate(double equity_value, double equity_vol, double debt,
                                           double r, double horizon, double tol = 1e-10,
                                           int max_iter = 200) noexcept;

}  // namespace risksim

#endif  // RISKSIM_MERTON_HPP
