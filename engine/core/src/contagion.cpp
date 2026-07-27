#include "risksim/contagion.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <vector>

namespace risksim {

namespace {
// Post-shock exogenous cash: fractional loss `shock[i]` applied to external assets.
std::vector<double> shocked_cash(const ExposureNetwork& net, std::span<const double> shock) {
    std::vector<double> e(net.n);
    for (std::size_t i = 0; i < net.n; ++i) {
        const double s = i < shock.size() ? shock[i] : 0.0;
        e[i] = net.external_assets[i] * (1.0 - s);
    }
    return e;
}
}  // namespace

// ---- Eisenberg-Noe: Picard fixed point -------------------------------------

ContagionResult EisenbergNoe::propagate(const ExposureNetwork& net,
                                        std::span<const double> shock) const {
    const std::size_t n = net.n;
    const Matrix pi = net.relative_liabilities();
    const std::vector<double> pbar = net.liability_marginals();
    const std::vector<double> e = shocked_cash(net, shock);

    std::vector<double> p = pbar;  // start at full payment (greatest clearing vector)
    int iters = 0;
    for (; iters < max_iter_; ++iters) {
        double max_change = 0.0;
        std::vector<double> received(n, 0.0);
        // received_i = sum_j Pi(j,i) * p_j
        for (std::size_t j = 0; j < n; ++j) {
            const double pj = p[j];
            if (pj == 0.0) continue;
            for (std::size_t i = 0; i < n; ++i) received[i] += pi(j, i) * pj;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const double target = std::clamp(e[i] + received[i], 0.0, pbar[i]);
            max_change = std::max(max_change, std::fabs(target - p[i]));
            p[i] = target;
        }
        if (max_change < tol_) break;
    }

    ContagionResult r;
    r.node_loss.assign(n, 0.0);
    r.defaulted.assign(n, 0);
    r.iterations = iters;
    for (std::size_t i = 0; i < n; ++i) {
        const double shortfall = pbar[i] - p[i];
        r.node_loss[i] = shortfall;
        if (shortfall > 1e-9 * std::max(1.0, pbar[i])) {
            r.defaulted[i] = 1;
            ++r.num_defaults;
        }
        r.system_loss += shortfall;
    }
    return r;
}

// ---- Eisenberg-Noe: fictitious-default algorithm (independent solver) -------

std::vector<double> eisenberg_noe_fictitious(const ExposureNetwork& net,
                                             std::span<const double> shocked_cash_in) {
    const std::size_t n = net.n;
    const Matrix pi = net.relative_liabilities();
    const std::vector<double> pbar = net.liability_marginals();
    std::vector<double> e(n);
    for (std::size_t i = 0; i < n; ++i) e[i] = i < shocked_cash_in.size() ? shocked_cash_in[i] : 0.0;

    std::vector<char> defaulted(n, 0);
    std::vector<double> p = pbar;

    for (std::size_t round = 0; round <= n; ++round) {
        // Collect current default set.
        std::vector<std::size_t> d;
        for (std::size_t i = 0; i < n; ++i)
            if (defaulted[i]) d.push_back(i);

        // Solvent nodes pay p-bar; solve the linear system for defaulters.
        for (std::size_t i = 0; i < n; ++i)
            if (!defaulted[i]) p[i] = pbar[i];

        if (!d.empty()) {
            const std::size_t dn = d.size();
            Matrix a(dn, dn);
            std::vector<double> rhs(dn, 0.0);
            for (std::size_t ai = 0; ai < dn; ++ai) {
                const std::size_t i = d[ai];
                double c = e[i];
                for (std::size_t j = 0; j < n; ++j) {
                    if (!defaulted[j]) c += pi(j, i) * pbar[j];  // received from solvent
                }
                rhs[ai] = c;
                for (std::size_t bi = 0; bi < dn; ++bi) {
                    const std::size_t j = d[bi];
                    a(ai, bi) = (ai == bi ? 1.0 : 0.0) - pi(j, i);  // (I - M)
                }
            }
            const std::vector<double> x = solve(std::move(a), rhs);
            for (std::size_t ai = 0; ai < dn; ++ai) {
                p[d[ai]] = std::clamp(x[ai], 0.0, pbar[d[ai]]);
            }
        }

        // Recompute payments/received, detect new defaulters among the solvent.
        bool changed = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (defaulted[i]) continue;
            double received = 0.0;
            for (std::size_t j = 0; j < n; ++j) received += pi(j, i) * p[j];
            if (e[i] + received < pbar[i] - 1e-9 * std::max(1.0, pbar[i])) {
                defaulted[i] = 1;
                changed = true;
            }
        }
        if (!changed) break;
    }
    return p;
}

// ---- DebtRank --------------------------------------------------------------

ContagionResult DebtRank::propagate(const ExposureNetwork& net,
                                    std::span<const double> shock) const {
    const std::size_t n = net.n;
    // Impact matrix W(i,j) = min(1, A_ij / E_i), A_ij = i's claim on j = L(j,i).
    Matrix w(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ei = net.equity[i];
        if (ei <= 0.0) continue;
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            w(i, j) = std::min(1.0, net.liabilities(j, i) / ei);
        }
    }

    // Initial relative equity loss from the external-asset shock.
    std::vector<double> h(n, 0.0);
    std::vector<double> h0(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double s = i < shock.size() ? shock[i] : 0.0;
        const double loss = s * net.external_assets[i];
        h0[i] = net.equity[i] > 0.0 ? std::min(1.0, loss / net.equity[i]) : (s > 0.0 ? 1.0 : 0.0);
        h[i] = h0[i];
    }

    int iters = 0;
    if (dynamical_) {
        // Bardoscia et al.: increments reverberate until they die out.
        std::vector<double> delta = h0;
        for (; iters < max_iter_; ++iters) {
            std::vector<double> newh = h;
            for (std::size_t i = 0; i < n; ++i) {
                double add = 0.0;
                for (std::size_t j = 0; j < n; ++j) add += w(i, j) * delta[j];
                newh[i] = std::min(1.0, h[i] + add);
            }
            double max_delta = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                delta[i] = newh[i] - h[i];
                max_delta = std::max(max_delta, delta[i]);
            }
            h = newh;
            if (max_delta < tol_) break;
        }
    } else {
        // Original two-state DebtRank: each node transmits exactly once.
        std::vector<int> state(n, 0);  // 0=undistressed, 1=distressed, 2=inactive
        for (std::size_t i = 0; i < n; ++i)
            if (h0[i] > 0.0) state[i] = 1;
        for (; iters < max_iter_; ++iters) {
            std::vector<double> newh = h;
            for (std::size_t i = 0; i < n; ++i) {
                double add = 0.0;
                for (std::size_t j = 0; j < n; ++j)
                    if (state[j] == 1) add += w(i, j) * h[j];
                newh[i] = std::min(1.0, h[i] + add);
            }
            bool any_distressed = false;
            std::vector<int> newstate = state;
            for (std::size_t i = 0; i < n; ++i) {
                if (state[i] == 1) {
                    newstate[i] = 2;  // transmitted once -> inactive
                } else if (state[i] == 0 && newh[i] > h[i] + 1e-15) {
                    newstate[i] = 1;  // newly distressed
                    any_distressed = true;
                }
            }
            h = newh;
            state = newstate;
            if (!any_distressed) break;
        }
    }

    ContagionResult r;
    r.node_loss.assign(n, 0.0);
    r.defaulted.assign(n, 0);
    r.iterations = iters;
    for (std::size_t i = 0; i < n; ++i) {
        r.node_loss[i] = h[i] * net.equity[i];
        if (h[i] >= 1.0 - 1e-9) {
            r.defaulted[i] = 1;
            ++r.num_defaults;
        }
        r.system_loss += r.node_loss[i];
    }
    return r;
}

// ---- Furfine / Gai-Kapadia threshold cascade -------------------------------

ContagionResult Furfine::propagate(const ExposureNetwork& net,
                                   std::span<const double> shock) const {
    const std::size_t n = net.n;
    std::vector<double> capital = net.equity;
    std::vector<char> defaulted(n, 0);
    std::queue<std::size_t> newly;

    // Apply the initial external shock; anyone driven below zero defaults now.
    for (std::size_t i = 0; i < n; ++i) {
        const double s = i < shock.size() ? shock[i] : 0.0;
        capital[i] -= s * net.external_assets[i];
        if (capital[i] < 0.0) {
            defaulted[i] = 1;
            newly.push(i);
        }
    }

    int rounds = 0;
    int processed = 0;
    while (!newly.empty()) {
        const std::size_t j = newly.front();
        newly.pop();
        ++processed;
        // Creditors of j (banks i with a claim L(j,i) on j) take LGD * exposure.
        for (std::size_t i = 0; i < n; ++i) {
            if (defaulted[i]) continue;
            const double exposure = net.liabilities(j, i);
            if (exposure == 0.0) continue;
            capital[i] -= lgd_ * exposure;
            if (capital[i] < 0.0) {
                defaulted[i] = 1;
                newly.push(i);
            }
        }
        rounds = processed;
    }

    ContagionResult r;
    r.node_loss.assign(n, 0.0);
    r.defaulted.assign(n, 0);
    r.iterations = rounds;
    for (std::size_t i = 0; i < n; ++i) {
        r.node_loss[i] = net.equity[i] - capital[i];
        r.defaulted[i] = defaulted[i];
        if (defaulted[i]) ++r.num_defaults;
        r.system_loss += r.node_loss[i];
    }
    return r;
}

}  // namespace risksim
