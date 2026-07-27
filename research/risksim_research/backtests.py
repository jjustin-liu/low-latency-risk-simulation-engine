"""Standard VaR / ES backtests, implemented as pure numpy/scipy.

These are the regulatory and academic reference tests used to validate a
risk-model's out-of-sample coverage. They are deliberately independent of the
C++ engine so they can be applied to *any* forecast/realisation series.

References
----------
* Kupiec, P. (1995). "Techniques for Verifying the Accuracy of Risk
  Measurement Models." Journal of Derivatives 3(2), 73-84.
* Christoffersen, P. (1998). "Evaluating Interval Forecasts." International
  Economic Review 39(4), 841-862.
* Basel Committee on Banking Supervision (1996, rev. 2006). "Supervisory
  Framework for the Use of 'Backtesting' in Conjunction with the Internal
  Models Approach to Market Risk Capital Requirements."
* Acerbi, C. and Szekely, B. (2014). "Backtesting Expected Shortfall." Risk
  Magazine.

Loss / VaR convention (matches the RiskSim engine): losses are positive with
larger = worse; VaR_alpha and ES_alpha are upper-tail statistics; a VaR
*exception* (hit) on day t is the event ``loss_t > VaR_t``. The expected
exception probability under a correctly-specified model is ``p = 1 - alpha``.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

import numpy as np
from scipy import stats

__all__ = [
    "kupiec_pof",
    "christoffersen",
    "ChristoffersenResult",
    "basel_traffic_light",
    "BaselResult",
    "acerbi_szekely_z1",
    "acerbi_szekely_z2",
    "acerbi_szekely_mc_pvalue",
]


def _xlogx_ratio(count: float, p: float) -> float:
    """count * log(p) with the convention 0 * log(0) = 0."""
    if count <= 0.0:
        return 0.0
    if p <= 0.0:
        # count > 0 but modelled probability 0 -> log-likelihood -> -inf.
        return -np.inf
    return count * np.log(p)


def kupiec_pof(exceptions: int, n: int, alpha: float) -> tuple[float, float]:
    """Kupiec (1995) proportion-of-failures unconditional-coverage LR test.

    Tests H0: the true exception probability equals ``p = 1 - alpha``.

    The likelihood-ratio statistic is

        LR_uc = -2 ln[ (1-p)^(n-x) p^x / ( (1 - x/n)^(n-x) (x/n)^x ) ]

    where ``x`` = number of exceptions and ``n`` = number of observations. Under
    H0, ``LR_uc`` is asymptotically chi-squared with 1 degree of freedom.

    Parameters
    ----------
    exceptions : int
        Observed number of VaR exceptions (``x``).
    n : int
        Number of observations.
    alpha : float
        VaR confidence level (e.g. 0.99); expected failure rate is ``1 - alpha``.

    Returns
    -------
    (LR_uc, p_value)
    """
    if n <= 0:
        raise ValueError("n must be positive")
    x = int(exceptions)
    p = 1.0 - alpha
    pi_hat = x / n

    ll_null = _xlogx_ratio(n - x, 1.0 - p) + _xlogx_ratio(x, p)
    ll_alt = _xlogx_ratio(n - x, 1.0 - pi_hat) + _xlogx_ratio(x, pi_hat)

    lr_uc = -2.0 * (ll_null - ll_alt)
    lr_uc = max(lr_uc, 0.0)
    p_value = float(stats.chi2.sf(lr_uc, df=1))
    return float(lr_uc), p_value


@dataclass
class ChristoffersenResult:
    lr_ind: float
    p_value_ind: float
    lr_cc: float
    p_value_cc: float
    lr_uc: float
    p_value_uc: float
    transitions: dict[str, int]


def christoffersen(hits, alpha: float) -> ChristoffersenResult:
    """Christoffersen (1998) independence + conditional-coverage LR tests.

    ``hits`` is a 0/1 sequence of VaR exceptions in time order. The independence
    test targets H0: exceptions are serially independent (no clustering),
    against a first-order Markov alternative. With transition counts n_ij (from
    state i to state j, i,j in {0,1}):

        pi01 = n01/(n00+n01),  pi11 = n11/(n10+n11),  pi = (n01+n11)/N

        LR_ind = -2 ln[ (1-pi)^(n00+n10) pi^(n01+n11)
                        / ( (1-pi01)^n00 pi01^n01 (1-pi11)^n10 pi11^n11 ) ]

    ``LR_ind`` ~ chi2(1). The conditional-coverage test combines coverage and
    independence:

        LR_cc = LR_uc + LR_ind ~ chi2(2)

    where ``LR_uc`` is the Kupiec statistic at ``alpha``.

    Returns a :class:`ChristoffersenResult`.
    """
    hits = np.asarray(hits, dtype=int).ravel()
    if hits.size < 2:
        raise ValueError("need at least 2 observations")
    if not np.all((hits == 0) | (hits == 1)):
        raise ValueError("hits must be a 0/1 sequence")

    prev = hits[:-1]
    cur = hits[1:]
    n00 = int(np.sum((prev == 0) & (cur == 0)))
    n01 = int(np.sum((prev == 0) & (cur == 1)))
    n10 = int(np.sum((prev == 1) & (cur == 0)))
    n11 = int(np.sum((prev == 1) & (cur == 1)))

    denom0 = n00 + n01
    denom1 = n10 + n11
    total = n00 + n01 + n10 + n11
    pi01 = n01 / denom0 if denom0 else 0.0
    pi11 = n11 / denom1 if denom1 else 0.0
    pi = (n01 + n11) / total if total else 0.0

    ll_alt = (
        _xlogx_ratio(n00, 1.0 - pi01)
        + _xlogx_ratio(n01, pi01)
        + _xlogx_ratio(n10, 1.0 - pi11)
        + _xlogx_ratio(n11, pi11)
    )
    ll_null = _xlogx_ratio(n00 + n10, 1.0 - pi) + _xlogx_ratio(n01 + n11, pi)

    lr_ind = -2.0 * (ll_null - ll_alt)
    lr_ind = max(lr_ind, 0.0)
    p_ind = float(stats.chi2.sf(lr_ind, df=1))

    exceptions = int(hits.sum())
    lr_uc, p_uc = kupiec_pof(exceptions, hits.size, alpha)

    lr_cc = lr_uc + lr_ind
    p_cc = float(stats.chi2.sf(lr_cc, df=2))

    return ChristoffersenResult(
        lr_ind=float(lr_ind),
        p_value_ind=p_ind,
        lr_cc=float(lr_cc),
        p_value_cc=p_cc,
        lr_uc=float(lr_uc),
        p_value_uc=p_uc,
        transitions={"n00": n00, "n01": n01, "n10": n10, "n11": n11},
    )


@dataclass
class BaselResult:
    zone: str  # 'green' | 'yellow' | 'red'
    exceptions: int
    multiplier: float  # total capital multiplier (base 3.0 + plus factor)
    plus_factor: float


# Official Basel plus factors for the 99% / 250-day window, keyed by the number
# of exceptions in the yellow zone. Green = 0.00, Red = 1.00.
_BASEL_YELLOW_PLUS = {5: 0.40, 6: 0.50, 7: 0.65, 8: 0.75, 9: 0.85}


def basel_traffic_light(exceptions_in_250: int, alpha: float = 0.99) -> BaselResult:
    """Basel Committee traffic-light backtest zone + capital multiplier.

    Zones are defined by the cumulative binomial probability of observing up to
    ``x`` exceptions in ``n = 250`` days under the model (``p = 1 - alpha``):

        green  : P(X <= x) < 0.95         (multiplier 3.00)
        yellow : 0.95 <= P(X <= x) < 0.9999
        red    : P(X <= x) >= 0.9999       (multiplier 4.00)

    For the standard 99% / 250-day case this reproduces the well-known table:
    0-4 exceptions green, 5-9 yellow, 10+ red. The capital multiplier is the
    base factor 3.0 plus the Basel plus factor (0.40..0.85 across the yellow
    zone, 1.00 in the red zone).

    Returns a :class:`BaselResult`.
    """
    n = 250
    x = int(exceptions_in_250)
    p = 1.0 - alpha
    cum = float(stats.binom.cdf(x, n, p))

    if cum < 0.95:
        zone, plus = "green", 0.0
    elif cum < 0.9999:
        zone = "yellow"
        # Interpolate/clamp the plus factor by exception count using the
        # official table; counts outside 5..9 clamp to the nearest tabulated.
        plus = _BASEL_YELLOW_PLUS.get(x, 0.85 if x > 9 else 0.40)
    else:
        zone, plus = "red", 1.0

    return BaselResult(zone=zone, exceptions=x, multiplier=3.0 + plus, plus_factor=plus)


def _exceedance_ratio_sum(losses, var, es) -> tuple[float, np.ndarray]:
    """Sum of (loss/ES) over exception days and the 0/1 indicator vector."""
    losses = np.asarray(losses, dtype=float).ravel()
    var = np.broadcast_to(np.asarray(var, dtype=float), losses.shape)
    es = np.broadcast_to(np.asarray(es, dtype=float), losses.shape)
    indicator = (losses > var).astype(float)
    ratio = np.where(indicator > 0, losses / es, 0.0)
    return float(np.sum(ratio)), indicator


def acerbi_szekely_z1(losses, var, es, alpha: float) -> float:
    """Acerbi-Szekely (2014) Test 1 statistic (conditional on exceptions).

        Z1 = ( 1/N_T * sum_t (loss_t / ES_t) * I_t ) - 1,   I_t = 1{loss_t > VaR_t}

    where ``N_T`` is the realised number of exceptions. Under H0 (ES correctly
    specified) ``E[Z1] = 0``; ``Z1 > 0`` signals ES *underestimation* (realised
    tail losses exceed the predicted ES). ``var``/``es`` may be scalars or
    per-observation arrays. Returns ``nan`` if there are no exceptions.
    """
    ratio_sum, indicator = _exceedance_ratio_sum(losses, var, es)
    n_t = float(indicator.sum())
    if n_t == 0.0:
        return float("nan")
    return ratio_sum / n_t - 1.0


def acerbi_szekely_z2(losses, var, es, alpha: float) -> float:
    """Acerbi-Szekely (2014) Test 2 statistic (uses unconditional exceedance).

        Z2 = ( 1 / (T * (1 - alpha)) * sum_t (loss_t / ES_t) * I_t ) - 1

    The denominator uses the *expected* number of exceptions ``T*(1-alpha)``
    rather than the realised count, so Z2 jointly tests frequency and magnitude.
    Under H0, ``E[Z2] = 0``; ``Z2 > 0`` signals ES underestimation.
    """
    ratio_sum, indicator = _exceedance_ratio_sum(losses, var, es)
    t = float(indicator.size)
    expected = t * (1.0 - alpha)
    if expected <= 0.0:
        raise ValueError("T*(1-alpha) must be positive")
    return ratio_sum / expected - 1.0


def acerbi_szekely_mc_pvalue(
    stat_fn: Callable,
    losses,
    var,
    es,
    alpha: float,
    null_rvs: Callable[[np.random.Generator, int], np.ndarray],
    n_sims: int = 10_000,
    seed: int = 0,
) -> tuple[float, float]:
    """Monte-Carlo p-value for an Acerbi-Szekely statistic under a stated null.

    ``null_rvs(rng, size)`` must return a length-``size`` array of losses drawn
    from the null (predictive) distribution that generated ``var``/``es``. The
    observed statistic is compared against its simulated null distribution; the
    one-sided p-value is ``P_H0(Z >= Z_obs)`` (rejection region = large positive
    Z = ES underestimation).

    Returns ``(observed_statistic, p_value)``.
    """
    losses = np.asarray(losses, dtype=float).ravel()
    observed = stat_fn(losses, var, es, alpha)
    rng = np.random.default_rng(seed)
    size = losses.size
    sims = np.empty(n_sims)
    for i in range(n_sims):
        sample = null_rvs(rng, size)
        sims[i] = stat_fn(sample, var, es, alpha)
    sims = sims[np.isfinite(sims)]
    if sims.size == 0 or not np.isfinite(observed):
        return float(observed), float("nan")
    p_value = float(np.mean(sims >= observed))
    return float(observed), p_value
