"""Tests for the real-data calibration pipeline. These run offline against the
committed price cache in research/data/prices/ (no network required)."""
from __future__ import annotations

import numpy as np
import pytest

import risksim
from risksim_research.calibration import (
    calibrate,
    ledoit_wolf_correlation,
    load_banks,
    marginals,
)


@pytest.fixture(scope="module")
def cal():
    # Uses the committed CSV cache; refresh=False so no network access.
    return calibrate(refresh=False)


def test_ledoit_wolf_is_spd_correlation():
    rng = np.random.default_rng(0)
    returns = rng.standard_normal((300, 8)) * 0.01
    corr = ledoit_wolf_correlation(returns)
    assert np.allclose(np.diag(corr), 1.0)
    assert np.allclose(corr, corr.T)
    # Positive definite (Cholesky succeeds / all eigenvalues > 0).
    assert np.min(np.linalg.eigvalsh(corr)) > 0.0


def test_calibration_shapes_and_vols(cal):
    n = len(cal.tickers)
    assert cal.corr.shape == (n, n)
    assert cal.ann_vol.shape == (n,)
    # Bank equity vols are plausible (10%–80% annualized).
    assert np.all(cal.ann_vol > 0.10) and np.all(cal.ann_vol < 0.80)
    assert np.isclose(cal.weights.sum(), 1.0)


def test_marginals_are_consistent(cal):
    cfg = load_banks()
    a, l, eq, ext = marginals(cal, cfg["interbank_fraction"], cfg["capital_ratio"])
    # Sinkhorn requires equal total assets in / liabilities out.
    assert np.isclose(a.sum(), l.sum())
    assert np.all(eq > 0) and np.all(ext > 0)


def test_network_from_real_marginals_runs(cal):
    cfg = load_banks()
    a, l, eq, ext = marginals(cal, cfg["interbank_fraction"], cfg["capital_ratio"])
    net = risksim.network_from_marginals(a, l, eq, ext, "max-entropy")
    assert net.n == len(cal.tickers)
    losses, var, es = risksim.simulate_calibrated(
        net, cal.ann_vol, cal.corr, "t", 4.0, 3000, 1, "furfine", 0.975, True
    )
    assert len(losses) == 6000
    assert es >= var > 0.0


def test_student_t_es_exceeds_gaussian_on_real_data(cal):
    cfg = load_banks()
    a, l, eq, ext = marginals(cal, cfg["interbank_fraction"], cfg["capital_ratio"])
    net = risksim.network_from_marginals(a, l, eq, ext, "max-entropy")
    common = dict(num_paths=20000, seed=7, model="furfine", alpha=0.99, antithetic=True)
    _, _, es_g = risksim.simulate_calibrated(net, cal.ann_vol, cal.corr, "gaussian", 4.0, **common)
    _, _, es_t = risksim.simulate_calibrated(net, cal.ann_vol, cal.corr, "t", 4.0, **common)
    assert es_t > es_g  # tail dependence raises systemic ES even on real correlations
