"""Tests that the compiled `risksim` binding matches independent numpy/scipy
computations, and that the copula tail properties hold."""

from __future__ import annotations

import numpy as np
import pytest
from scipy import stats

import risksim as ok


def _numpy_empirical_var_es(losses, alpha):
    """Reference empirical VaR/ES matching the engine's order-statistic
    definition: k = ceil((1-alpha)*n) worst losses; VaR = k-th largest,
    ES = mean of the worst k."""
    x = np.sort(np.asarray(losses, dtype=float))
    n = x.size
    k = int(np.ceil((1.0 - alpha) * n))
    k = max(k, 1)
    tail = x[n - k :]
    var = tail[0]  # smallest of the worst-k = k-th largest overall
    es = tail.mean()
    return var, es


def test_empirical_var_es_matches_numpy():
    rng = np.random.default_rng(123)
    losses = rng.standard_normal(200_000)
    for alpha in (0.95, 0.975, 0.99):
        v_c, e_c = ok.empirical_var_es(losses, alpha)
        v_n, e_n = _numpy_empirical_var_es(losses, alpha)
        assert v_c == pytest.approx(v_n, rel=0, abs=1e-9)
        assert e_c == pytest.approx(e_n, rel=0, abs=1e-9)


def test_gaussian_var_es_matches_closed_form():
    mu, sigma, alpha = 0.5, 2.0, 0.975
    v, e = ok.gaussian_var_es(mu, sigma, alpha)
    z = stats.norm.ppf(alpha)
    v_ref = mu + sigma * z
    e_ref = mu + sigma * stats.norm.pdf(z) / (1.0 - alpha)
    assert v == pytest.approx(v_ref, rel=1e-10)
    assert e == pytest.approx(e_ref, rel=1e-10)


def test_norm_and_t_cdf_match_scipy():
    for x in (-2.5, -0.3, 0.0, 1.1, 3.0):
        assert ok.norm_cdf(x) == pytest.approx(stats.norm.cdf(x), abs=1e-12)
    for p in (0.01, 0.5, 0.975, 0.999):
        assert ok.norm_inv_cdf(p) == pytest.approx(stats.norm.ppf(p), abs=1e-9)
    for nu in (3.0, 8.0):
        for x in (-2.0, 0.4, 2.5):
            assert ok.students_t_cdf(x, nu) == pytest.approx(stats.t.cdf(x, nu), abs=1e-8)
        for p in (0.05, 0.5, 0.99):
            assert ok.students_t_inv_cdf(p, nu) == pytest.approx(stats.t.ppf(p, nu), abs=1e-6)


def test_t_copula_tail_dependence_positive_and_vanishes():
    rho = 0.5
    lam_small_nu = ok.t_copula_tail_dependence(rho, 3.0)
    lam_large_nu = ok.t_copula_tail_dependence(rho, 200.0)
    assert lam_small_nu > 0.0
    assert lam_large_nu >= 0.0
    # Monotone decreasing towards the Gaussian (zero tail-dependence) limit.
    assert lam_small_nu > lam_large_nu
    assert ok.t_copula_tail_dependence(rho, 1e7) == pytest.approx(0.0, abs=1e-6)


def test_t_copula_systemic_es_exceeds_gaussian():
    net = ok.generate_network(4, 20, seed=11)
    common = dict(node_vol=0.30, correlation=0.5, nu=3.0, num_paths=8000, seed=99,
                  model="furfine", alpha=0.975, antithetic=True)
    _, g_var, g_es = ok.simulate_system_losses(net, common["node_vol"], common["correlation"],
                                               "gaussian", common["nu"], common["num_paths"],
                                               common["seed"], common["model"], common["alpha"],
                                               common["antithetic"])
    _, t_var, t_es = ok.simulate_system_losses(net, common["node_vol"], common["correlation"],
                                              "t", common["nu"], common["num_paths"],
                                              common["seed"], common["model"], common["alpha"],
                                              common["antithetic"])
    assert t_es > g_es
    assert t_var >= g_var


def test_simulate_returns_consistent_var_es():
    net = ok.generate_network(3, 12, seed=5)
    losses, var, es = ok.simulate_system_losses(net, 0.25, 0.4, "gaussian", 5.0,
                                                5000, 7, "eisenberg-noe", 0.975, False)
    v_ref, e_ref = ok.empirical_var_es(losses, 0.975)
    assert var == pytest.approx(v_ref, abs=1e-9)
    assert es == pytest.approx(e_ref, abs=1e-9)
    assert es >= var
