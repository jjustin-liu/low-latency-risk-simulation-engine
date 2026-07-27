"""Tests for the pure-numpy backtest statistics on constructed inputs."""

from __future__ import annotations

import numpy as np
import pytest
from scipy import stats

from risksim_research import backtests as bt


def test_kupiec_zero_when_rate_equals_alpha():
    # Exactly the expected number of exceptions -> LR_uc == 0, p-value == 1.
    n = 1000
    alpha = 0.99
    exceptions = int(round(n * (1.0 - alpha)))  # 10
    lr, p = bt.kupiec_pof(exceptions, n, alpha)
    assert lr == pytest.approx(0.0, abs=1e-9)
    assert p == pytest.approx(1.0, abs=1e-9)


def test_kupiec_rejects_gross_undercoverage():
    # Way too many exceptions -> large LR, tiny p-value.
    lr, p = bt.kupiec_pof(60, 1000, 0.99)  # expected 10, observed 60
    assert lr > 30.0
    assert p < 1e-6


def test_kupiec_matches_manual_formula():
    n, x, alpha = 500, 8, 0.99
    p = 1.0 - alpha
    pi = x / n
    ll_null = (n - x) * np.log(1 - p) + x * np.log(p)
    ll_alt = (n - x) * np.log(1 - pi) + x * np.log(pi)
    lr_ref = -2.0 * (ll_null - ll_alt)
    lr, pv = bt.kupiec_pof(x, n, alpha)
    assert lr == pytest.approx(lr_ref, rel=1e-10)
    assert pv == pytest.approx(stats.chi2.sf(lr_ref, 1), rel=1e-10)


def test_christoffersen_independence_no_clustering():
    # Independent Bernoulli hits -> should NOT reject independence (largish p).
    rng = np.random.default_rng(0)
    hits = (rng.random(5000) < 0.01).astype(int)
    res = bt.christoffersen(hits, alpha=0.99)
    assert res.lr_ind >= 0.0
    assert res.p_value_ind > 0.01
    # transition counts consistent with the input length.
    tr = res.transitions
    assert tr["n00"] + tr["n01"] + tr["n10"] + tr["n11"] == hits.size - 1


def test_christoffersen_detects_clustering():
    # Strong clustering: exceptions arrive in a contiguous block.
    hits = np.zeros(1000, dtype=int)
    hits[400:440] = 1  # 40 consecutive exceptions
    res = bt.christoffersen(hits, alpha=0.96)
    assert res.p_value_ind < 0.05  # independence rejected


def test_christoffersen_cc_equals_uc_plus_ind():
    rng = np.random.default_rng(1)
    hits = (rng.random(2000) < 0.02).astype(int)
    res = bt.christoffersen(hits, alpha=0.98)
    assert res.lr_cc == pytest.approx(res.lr_uc + res.lr_ind, rel=1e-12)


@pytest.mark.parametrize(
    "x,zone,mult",
    [
        (0, "green", 3.00),
        (4, "green", 3.00),
        (5, "yellow", 3.40),
        (6, "yellow", 3.50),
        (7, "yellow", 3.65),
        (8, "yellow", 3.75),
        (9, "yellow", 3.85),
        (10, "red", 4.00),
        (15, "red", 4.00),
    ],
)
def test_basel_zones_standard_table(x, zone, mult):
    res = bt.basel_traffic_light(x, alpha=0.99)
    assert res.zone == zone
    assert res.multiplier == pytest.approx(mult)


def test_acerbi_szekely_zero_under_null_on_average():
    # Under a correctly specified Normal null, E[Z1]~0 and E[Z2]~0.
    alpha = 0.975
    rng = np.random.default_rng(3)
    # Normal loss with sigma=1, mu=0; VaR/ES are the closed-form Normal values.
    z = stats.norm.ppf(alpha)
    var_v = z
    es_v = stats.norm.pdf(z) / (1.0 - alpha)

    z1s, z2s = [], []
    for _ in range(400):
        losses = rng.standard_normal(2000)
        z1s.append(bt.acerbi_szekely_z1(losses, var_v, es_v, alpha))
        z2s.append(bt.acerbi_szekely_z2(losses, var_v, es_v, alpha))
    assert np.nanmean(z1s) == pytest.approx(0.0, abs=0.05)
    assert np.nanmean(z2s) == pytest.approx(0.0, abs=0.05)


def test_acerbi_szekely_detects_underestimation():
    # The predictive (null) model is a standard Normal: its VaR/ES are the
    # closed-form Normal values, and the MC null samples standard Normal losses.
    # The *realised* losses instead come from a fatter distribution (scaled by
    # 1.6), so the model under-estimates the tail. Z1/Z2 should be strongly
    # positive and the MC p-value small (reject correct-ES).
    alpha = 0.975
    rng = np.random.default_rng(4)
    z = stats.norm.ppf(alpha)
    var_v = z
    es_v = stats.norm.pdf(z) / (1.0 - alpha)

    losses = 1.6 * rng.standard_normal(4000)  # realised tail heavier than predicted

    def null_rvs(r, size):
        return r.standard_normal(size)  # predictive model

    obs1, p1 = bt.acerbi_szekely_mc_pvalue(
        bt.acerbi_szekely_z1, losses, var_v, es_v, alpha, null_rvs, n_sims=2000, seed=1
    )
    obs2, p2 = bt.acerbi_szekely_mc_pvalue(
        bt.acerbi_szekely_z2, losses, var_v, es_v, alpha, null_rvs, n_sims=2000, seed=2
    )
    assert obs1 > 0.0 and obs2 > 0.0
    assert p1 < 0.05 and p2 < 0.05
