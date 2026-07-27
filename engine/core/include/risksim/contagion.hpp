// Financial-contagion models over an exposure network.
//
// Three models answer three different questions about the SAME shock, and we run
// all three to show they diverge in the tail:
//   * Eisenberg-Noe  -> "who cannot pay?"      (payment clearing, defaults)
//   * DebtRank        -> "how much value is impaired?" (distress before default)
//   * Furfine         -> "who topples?"        (threshold default cascade)
//
// Shock convention (unified across models): shock[i] in [0,1] is the fractional
// loss in the value of bank i's external (outside-the-network) assets. Each model
// translates that into its own state.
#ifndef RISKSIM_CONTAGION_HPP
#define RISKSIM_CONTAGION_HPP

#include <cstddef>
#include <span>
#include <vector>

#include "risksim/network.hpp"

namespace risksim {

struct ContagionResult {
    std::vector<double> node_loss;  // absolute equity/value loss per node
    std::vector<char> defaulted;    // 1 if bank i defaults (char, not vector<bool>)
    double system_loss = 0.0;       // total value destroyed
    int num_defaults = 0;
    int iterations = 0;
};

class ContagionModel {
public:
    virtual ~ContagionModel() = default;
    [[nodiscard]] virtual ContagionResult propagate(const ExposureNetwork& net,
                                                    std::span<const double> shock) const = 0;
    [[nodiscard]] virtual const char* name() const noexcept = 0;
};

// ---- Eisenberg-Noe (2001) clearing vector ----------------------------------

// Solves the clearing map p = min(p-bar, max(0, e + Pi^T p)) for the payment
// vector. `propagate` uses the Picard fixed-point iteration (monotone, robust).
class EisenbergNoe final : public ContagionModel {
public:
    explicit EisenbergNoe(double tol = 1e-11, int max_iter = 10000)
        : tol_(tol), max_iter_(max_iter) {}
    [[nodiscard]] ContagionResult propagate(const ExposureNetwork& net,
                                            std::span<const double> shock) const override;
    [[nodiscard]] const char* name() const noexcept override { return "eisenberg-noe"; }

private:
    double tol_;
    int max_iter_;
};

// Independent solver: the fictitious-default algorithm (solves a linear system on
// the growing default set). Cross-checked against Picard in the golden tests.
// Returns the clearing payment vector p*.
[[nodiscard]] std::vector<double> eisenberg_noe_fictitious(const ExposureNetwork& net,
                                                           std::span<const double> shocked_cash);

// ---- DebtRank (Battiston 2012 / Bardoscia 2015) ----------------------------

class DebtRank final : public ContagionModel {
public:
    // `dynamical=false` is the original two-state DebtRank (each node transmits
    // once); `dynamical=true` is the Bardoscia et al. reverberating dynamics that
    // fixes the original's underestimation.
    explicit DebtRank(bool dynamical = true, int max_iter = 1000, double tol = 1e-12)
        : dynamical_(dynamical), max_iter_(max_iter), tol_(tol) {}
    [[nodiscard]] ContagionResult propagate(const ExposureNetwork& net,
                                            std::span<const double> shock) const override;
    [[nodiscard]] const char* name() const noexcept override {
        return dynamical_ ? "debtrank-dynamical" : "debtrank-original";
    }

private:
    bool dynamical_;
    int max_iter_;
    double tol_;
};

// ---- Furfine (2003) / Gai-Kapadia threshold cascade ------------------------

class Furfine final : public ContagionModel {
public:
    explicit Furfine(double loss_given_default = 1.0) : lgd_(loss_given_default) {}
    [[nodiscard]] ContagionResult propagate(const ExposureNetwork& net,
                                            std::span<const double> shock) const override;
    [[nodiscard]] const char* name() const noexcept override { return "furfine"; }

private:
    double lgd_;
};

}  // namespace risksim

#endif  // RISKSIM_CONTAGION_HPP
