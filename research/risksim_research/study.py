"""Model-validation studies that drive the compiled ``risksim`` C++ bindings.

Two headline studies:

1. :func:`copula_tail_study` -- swap only the copula (Gaussian vs Student-t) on
   an identical network / volatility / correlation and measure how much systemic
   Expected Shortfall the tail dependence adds.

2. :func:`var_backtest_driver` -- generate a fat-tailed, volatility-clustered
   return series, form rolling one-day VaR forecasts with (a) a Gaussian model
   using the core ``norm_inv_cdf`` and (b) a Student-t model using the core
   ``students_t_inv_cdf``, then backtest both with Kupiec / Christoffersen /
   Basel. The Gaussian model is expected to land in a worse Basel zone.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

# `risksim_research.__init__` puts the in-tree binding dir on sys.path.
import risksim as ok

from . import backtests as bt

__all__ = [
    "CopulaTailResult",
    "copula_tail_study",
    "BacktestModelResult",
    "VarBacktestResult",
    "simulate_fat_tailed_returns",
    "var_backtest_driver",
]


# --------------------------------------------------------------------------- #
# Study (a): Gaussian vs Student-t copula systemic tail                        #
# --------------------------------------------------------------------------- #
@dataclass
class CopulaTailResult:
    gaussian_var: float
    gaussian_es: float
    t_var: float
    t_es: float
    es_gap_pct: float
    var_gap_pct: float
    nu: float
    correlation: float
    node_vol: float
    num_paths: int
    gaussian_losses: np.ndarray = field(repr=False, default=None)
    t_losses: np.ndarray = field(repr=False, default=None)


def copula_tail_study(
    net_handle,
    node_vol: float = 0.30,
    correlation: float = 0.5,
    nu: float = 3.0,
    num_paths: int = 40_000,
    seed: int = 20260726,
    model: str = "furfine",
    alpha: float = 0.975,
    antithetic: bool = True,
) -> CopulaTailResult:
    """Run the systemic MC with a Gaussian vs a Student-t copula, same everything.

    Returns both VaR/ES and the percentage ES gap ``(ES_t - ES_g) / ES_g``.
    """
    g_loss, g_var, g_es = ok.simulate_system_losses(
        net_handle, node_vol, correlation, "gaussian", nu, num_paths, seed, model, alpha, antithetic
    )
    t_loss, t_var, t_es = ok.simulate_system_losses(
        net_handle, node_vol, correlation, "t", nu, num_paths, seed, model, alpha, antithetic
    )
    es_gap = 100.0 * (t_es - g_es) / g_es if g_es != 0 else float("nan")
    var_gap = 100.0 * (t_var - g_var) / g_var if g_var != 0 else float("nan")
    return CopulaTailResult(
        gaussian_var=g_var,
        gaussian_es=g_es,
        t_var=t_var,
        t_es=t_es,
        es_gap_pct=es_gap,
        var_gap_pct=var_gap,
        nu=nu,
        correlation=correlation,
        node_vol=node_vol,
        num_paths=num_paths,
        gaussian_losses=np.asarray(g_loss),
        t_losses=np.asarray(t_loss),
    )


# --------------------------------------------------------------------------- #
# Study (b): rolling VaR backtest, normal vs t model under fat tails           #
# --------------------------------------------------------------------------- #
def simulate_fat_tailed_returns(
    n: int = 1500,
    nu: float = 4.0,
    omega: float = 2.0e-6,
    arch: float = 0.10,
    garch: float = 0.88,
    seed: int = 7,
) -> np.ndarray:
    """GARCH(1,1) returns with Student-t innovations (volatility clustering +
    fat tails).

        sigma_t^2 = omega + arch * r_{t-1}^2 + garch * sigma_{t-1}^2
        r_t       = sigma_t * eps_t,   eps_t ~ standardised t(nu)

    Innovations are scaled to unit variance so ``sigma_t`` is the conditional
    standard deviation. Persistence ``arch + garch`` is < 1 (stationary).
    """
    rng = np.random.default_rng(seed)
    # Standardised t: divide by sqrt(nu/(nu-2)) so Var[eps] = 1.
    scale = np.sqrt((nu - 2.0) / nu)
    eps = rng.standard_t(nu, size=n) * scale

    r = np.empty(n)
    sigma2 = omega / max(1.0 - arch - garch, 1e-6)  # unconditional variance
    prev_r2 = sigma2
    for t in range(n):
        sigma2 = omega + arch * prev_r2 + garch * sigma2
        r[t] = np.sqrt(sigma2) * eps[t]
        prev_r2 = r[t] * r[t]
    return r


@dataclass
class BacktestModelResult:
    name: str
    exceptions: int
    n: int
    expected: float
    kupiec_lr: float
    kupiec_p: float
    christoffersen_lr_ind: float
    christoffersen_p_ind: float
    christoffersen_lr_cc: float
    christoffersen_p_cc: float
    basel_zone: str
    basel_multiplier: float
    var_series: np.ndarray = field(repr=False, default=None)
    hits: np.ndarray = field(repr=False, default=None)


@dataclass
class VarBacktestResult:
    alpha: float
    window: int
    nu: float
    returns: np.ndarray = field(repr=False, default=None)
    losses: np.ndarray = field(repr=False, default=None)
    dates_index: np.ndarray = field(repr=False, default=None)
    normal: BacktestModelResult = None
    student_t: BacktestModelResult = None


def _rolling_forecasts(returns: np.ndarray, window: int, alpha: float, nu: float):
    """Rolling one-day-ahead VaR forecasts for the normal and t models.

    At each t the conditional mean/std are estimated from the trailing
    ``window`` returns. Losses are ``L = -return``. VaR is the ``alpha`` quantile
    of the loss:

        normal : VaR = -mu + sigma * norm_inv_cdf(alpha)                (core fn)
        t      : VaR = -mu + sigma * t_std_quantile(alpha, nu)          (core fn)

    where ``t_std_quantile`` uses the core ``students_t_inv_cdf`` rescaled to a
    unit-variance t so the two models share the same sigma.
    """
    n = returns.size
    losses = -returns
    var_norm = np.full(n, np.nan)
    var_t = np.full(n, np.nan)

    t_std_scale = np.sqrt((nu - 2.0) / nu)  # standardise t(nu) to unit variance
    z_norm = ok.norm_inv_cdf(alpha)
    q_t = ok.students_t_inv_cdf(alpha, nu) * t_std_scale

    for t in range(window, n):
        win = returns[t - window : t]
        mu = float(win.mean())
        sigma = float(win.std(ddof=1))
        var_norm[t] = -mu + sigma * z_norm
        var_t[t] = -mu + sigma * q_t
    return losses, var_norm, var_t


def _score_model(name, losses, var_series, alpha, eval_start) -> BacktestModelResult:
    idx = np.arange(eval_start, losses.size)
    L = losses[idx]
    V = var_series[idx]
    valid = ~np.isnan(V)
    L = L[valid]
    V = V[valid]
    hits = (L > V).astype(int)
    exceptions = int(hits.sum())
    n = hits.size

    kupiec_lr, kupiec_p = bt.kupiec_pof(exceptions, n, alpha)
    ch = bt.christoffersen(hits, alpha)

    # Basel traffic light uses the most recent 250 observations.
    last250 = hits[-250:] if n >= 250 else hits
    basel = bt.basel_traffic_light(int(last250.sum()), alpha)

    return BacktestModelResult(
        name=name,
        exceptions=exceptions,
        n=n,
        expected=n * (1.0 - alpha),
        kupiec_lr=kupiec_lr,
        kupiec_p=kupiec_p,
        christoffersen_lr_ind=ch.lr_ind,
        christoffersen_p_ind=ch.p_value_ind,
        christoffersen_lr_cc=ch.lr_cc,
        christoffersen_p_cc=ch.p_value_cc,
        basel_zone=basel.zone,
        basel_multiplier=basel.multiplier,
        var_series=V,
        hits=hits,
    )


def var_backtest_driver(
    n: int = 1500,
    window: int = 250,
    alpha: float = 0.99,
    nu: float = 4.0,
    seed: int = 7,
) -> VarBacktestResult:
    """Generate fat-tailed returns and backtest a normal vs a t VaR model.

    Demonstrates that under fat tails the Gaussian VaR model under-covers and
    lands in a worse Basel zone than the Student-t model that matches the
    innovation tails.
    """
    returns = simulate_fat_tailed_returns(n=n, nu=nu, seed=seed)
    losses, var_norm, var_t = _rolling_forecasts(returns, window, alpha, nu)

    normal = _score_model("normal", losses, var_norm, alpha, window)
    student_t = _score_model("student-t", losses, var_t, alpha, window)

    return VarBacktestResult(
        alpha=alpha,
        window=window,
        nu=nu,
        returns=returns,
        losses=losses,
        dates_index=np.arange(returns.size),
        normal=normal,
        student_t=student_t,
    )
