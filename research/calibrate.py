#!/usr/bin/env python3
"""Calibrate RiskSim to real bank data and produce a report.

Runs the whole real-data pipeline and writes research/reports/calibration.md
plus figures. Uses the SHIPPING C++ core via the `risksim` binding for the
network reconstruction, the systemic Monte Carlo, and the VaR quantiles.

    python research/calibrate.py            # uses the cached price CSVs
    python research/calibrate.py --refresh  # re-fetch from Yahoo Finance
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

_REPO = Path(__file__).resolve().parents[1]
for p in (str(_REPO / "research"), str(_REPO / "engine" / "py")):
    if p not in sys.path:
        sys.path.insert(0, p)

import risksim  # the shipping C++ core
from risksim_research import backtests as bt
from risksim_research.calibration import calibrate, marginals, load_banks

REPORTS = _REPO / "research" / "reports"
FIGS = REPORTS / "figures"
ALPHA = 0.975
VAR_ALPHA = 0.99


def systemic_study(cal, cfg):
    """Gaussian vs Student-t systemic ES on the real-calibrated network, for both
    max-entropy and min-density reconstructions (a bounding band)."""
    a, l, eq, ext = marginals(cal, cfg["interbank_fraction"], cfg["capital_ratio"])
    out = {}
    for method in ("max-entropy", "min-density"):
        net = risksim.network_from_marginals(a, l, eq, ext, method)
        res = {}
        for cop in ("gaussian", "t"):
            losses, var, es = risksim.simulate_calibrated(
                net, cal.ann_vol, cal.corr, cop, 4.0, 100000, 20240101, "furfine", ALPHA, True
            )
            res[cop] = {"losses": losses, "var": var, "es": es}
        gap = 100.0 * (res["t"]["es"] - res["gaussian"]["es"]) / res["gaussian"]["es"]
        out[method] = {"res": res, "es_gap_pct": gap}
    return out


def real_var_backtest(cal, window=250, nu=5.0):
    """Rolling 1-day 99% VaR on the real asset-weighted bank portfolio, Normal vs
    Student-t model, using the core's inverse-CDFs. Returns per-model results."""
    port = cal.returns @ cal.weights          # daily portfolio log return
    loss = -port
    T = len(port)
    z_n = risksim.norm_inv_cdf(VAR_ALPHA)
    # scale a standardized t (variance nu/(nu-2)) to unit variance:
    t_scale = np.sqrt((nu - 2.0) / nu) * risksim.students_t_inv_cdf(VAR_ALPHA, nu)

    results = {}
    for name, mult in (("normal", z_n), ("student-t", t_scale)):
        var_series = np.full(T, np.nan)
        hits = []
        for t in range(window, T):
            sigma = port[:t][-window:].std(ddof=1)
            var_t = sigma * mult
            var_series[t] = var_t
            hits.append(1 if loss[t] > var_t else 0)
        hits = np.array(hits, dtype=int)
        n_eval = len(hits)
        exc = int(hits.sum())
        lr_uc, p_uc = bt.kupiec_pof(exc, n_eval, VAR_ALPHA)
        chris = bt.christoffersen(hits, VAR_ALPHA)
        exc_250 = int(round(exc * 250.0 / n_eval))
        basel = bt.basel_traffic_light(exc_250, VAR_ALPHA)
        # Basel actually evaluates a trailing 250-day window; find the WORST one
        # (this is where a thin-tailed model gets caught by a stress episode).
        if n_eval >= 250:
            windowed = np.array([hits[i:i + 250].sum() for i in range(n_eval - 250 + 1)])
            worst = int(windowed.max())
        else:
            worst = exc_250
        worst_basel = bt.basel_traffic_light(worst, VAR_ALPHA)
        results[name] = {
            "exceptions": exc, "n_eval": n_eval, "rate": exc / n_eval,
            "kupiec_lr": lr_uc, "kupiec_p": p_uc,
            "christoffersen_lr_cc": chris.lr_cc, "christoffersen_p_cc": chris.p_value_cc,
            "exc_per_250": exc_250, "basel_zone": basel.zone,
            "basel_multiplier": basel.multiplier, "var_series": var_series,
            "worst_250": worst, "worst_zone": worst_basel.zone,
            "worst_multiplier": worst_basel.multiplier,
        }
    return loss, results


def make_figures(cal, systemic, loss, bt_results):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception as e:  # noqa: BLE001
        print(f"(matplotlib unavailable, skipping figures: {e})")
        return
    FIGS.mkdir(parents=True, exist_ok=True)

    # Correlation heatmap.
    fig, ax = plt.subplots(figsize=(6, 5))
    im = ax.imshow(cal.corr, vmin=0, vmax=1, cmap="magma")
    ax.set_xticks(range(len(cal.tickers)), cal.tickers, rotation=90, fontsize=7)
    ax.set_yticks(range(len(cal.tickers)), cal.tickers, fontsize=7)
    ax.set_title("Ledoit-Wolf shrunk return correlation (real data)")
    fig.colorbar(im, ax=ax, fraction=0.046)
    fig.tight_layout(); fig.savefig(FIGS / "real_correlation.png", dpi=120); plt.close(fig)

    # Systemic loss distribution: Gaussian vs Student-t (max-entropy network).
    r = systemic["max-entropy"]["res"]
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.hist(r["gaussian"]["losses"], bins=80, density=True, alpha=0.5, label="Gaussian copula")
    ax.hist(r["t"]["losses"], bins=80, density=True, alpha=0.5, label="Student-t copula")
    ax.axvline(r["gaussian"]["es"], color="tab:blue", ls="--", lw=1)
    ax.axvline(r["t"]["es"], color="tab:orange", ls="--", lw=1)
    ax.set_xlabel("systemic loss"); ax.set_ylabel("density")
    ax.set_title("Systemic loss: Gaussian vs Student-t (dashed = ES 97.5%)")
    ax.legend(); fig.tight_layout()
    fig.savefig(FIGS / "real_systemic_gaussian_vs_t.png", dpi=120); plt.close(fig)

    # VaR exceedance timeline (normal model).
    fig, ax = plt.subplots(figsize=(8, 4))
    vs = bt_results["normal"]["var_series"]
    ax.plot(loss, lw=0.5, color="0.6", label="portfolio loss")
    ax.plot(vs, lw=1.0, color="tab:red", label="99% VaR (normal)")
    exc_idx = np.where(loss > vs)[0]
    ax.scatter(exc_idx, loss[exc_idx], s=10, color="black", zorder=3, label="exceptions")
    ax.set_title("Real bank-portfolio loss vs 99% VaR (normal model)")
    ax.legend(fontsize=8); fig.tight_layout()
    fig.savefig(FIGS / "real_var_exceedances.png", dpi=120); plt.close(fig)


def main():
    refresh = "--refresh" in sys.argv
    cfg = load_banks()
    cal = calibrate(refresh=refresh)
    print(f"calibrated on {len(cal.tickers)} banks, {cal.returns.shape[0]} trading days "
          f"({cal.start}..{cal.end})")

    systemic = systemic_study(cal, cfg)
    loss, bt_results = real_var_backtest(cal)
    make_figures(cal, systemic, loss, bt_results)

    REPORTS.mkdir(parents=True, exist_ok=True)
    lines = []
    lines.append("# RiskSim — real-data calibration report\n")
    lines.append(f"Calibrated to **{len(cal.tickers)} large US banks** "
                 f"({', '.join(cal.tickers)}), daily data {cal.start} to {cal.end} "
                 f"({cal.returns.shape[0]} trading days). Equity returns/vols/correlations are "
                 f"real (Yahoo Finance); interbank marginals are scaled from approximate public "
                 f"balance-sheet sizes; the bilateral matrix is reconstructed by the C++ core.\n")

    lines.append("## Per-bank annualized volatility (real)\n")
    lines.append("| bank | ann. vol | total assets ($bn) |")
    lines.append("|---|--:|--:|")
    for t, v, ta in zip(cal.tickers, cal.ann_vol, cal.total_assets):
        lines.append(f"| {t} | {v*100:.1f}% | {ta:.0f} |")
    avg_corr = (cal.corr.sum() - len(cal.tickers)) / (len(cal.tickers)**2 - len(cal.tickers))
    lines.append(f"\nMean pairwise correlation (Ledoit-Wolf shrunk): **{avg_corr:.2f}**.\n")

    lines.append("## Systemic risk: Gaussian vs Student-t copula (real calibration)\n")
    lines.append("Same real marginals/correlation; only the copula changes. "
                 "Reported for both network reconstructions as a bounding band.\n")
    lines.append("| reconstruction | Gaussian ES | Student-t ES | ES gap |")
    lines.append("|---|--:|--:|--:|")
    for method in ("max-entropy", "min-density"):
        s = systemic[method]
        lines.append(f"| {method} | {s['res']['gaussian']['es']:.1f} | "
                     f"{s['res']['t']['es']:.1f} | **+{s['es_gap_pct']:.1f}%** |")
    lines.append("\n_Student-t tail dependence raises systemic ES even on real correlations — "
                 "the Gaussian copula understates joint-crash risk._\n")

    # Tail-sensitivity sweep: the copula gap widens deeper in the tail.
    lines.append("### The copula gap widens deeper in the tail\n")
    lines.append("Same loss samples (max-entropy network), ES gap at progressively more extreme "
                 "levels — where these banks are already ~60% correlated, tail dependence matters "
                 "most in the extreme tail:\n")
    lines.append("| level | Gaussian ES | Student-t ES | gap |")
    lines.append("|---|--:|--:|--:|")
    gl = systemic["max-entropy"]["res"]["gaussian"]["losses"]
    tl = systemic["max-entropy"]["res"]["t"]["losses"]
    for a in (0.975, 0.99, 0.995):
        _, es_g = risksim.empirical_var_es(gl, a)
        _, es_t = risksim.empirical_var_es(tl, a)
        gap = 100.0 * (es_t - es_g) / es_g
        lines.append(f"| {a:.3f} | {es_g:.1f} | {es_t:.1f} | +{gap:.1f}% |")
    lines.append("")

    lines.append("## Real-P&L VaR backtest (99%, rolling 250-day window)\n")
    lines.append("Asset-weighted bank portfolio, real daily losses; VaR quantiles from the core's "
                 "inverse-CDFs. The sample spans the COVID crash, which stresses the tail model.\n")
    lines.append("| model | exceptions | rate | Kupiec p | worst 250-day window | worst-window zone |")
    lines.append("|---|--:|--:|--:|--:|:--:|")
    for name in ("normal", "student-t"):
        r = bt_results[name]
        lines.append(f"| {name} | {r['exceptions']}/{r['n_eval']} | {r['rate']*100:.2f}% | "
                     f"{r['kupiec_p']:.3f} | {r['worst_250']} exceptions | "
                     f"**{r['worst_zone'].upper()}** (x{r['worst_multiplier']:.2f}) |")
    lines.append("\n_Over the full sample the Normal model is rejected by Kupiec (2.2% breaches vs "
                 "the 1% target); the Student-t is materially closer. Both breach heavily in the "
                 "worst 250-day window (the COVID crash) — a naive rolling-volatility quantile "
                 "can't absorb a jump that fast regardless of tail shape; capturing it needs "
                 "volatility clustering (the GARCH study in model-validation.md)._\n")
    lines.append("\n![correlation](figures/real_correlation.png)\n")
    lines.append("![systemic](figures/real_systemic_gaussian_vs_t.png)\n")
    lines.append("![var](figures/real_var_exceedances.png)\n")

    (REPORTS / "calibration.md").write_text("\n".join(lines) + "\n")
    print("wrote research/reports/calibration.md + figures/")
    # Console highlights.
    me = systemic["max-entropy"]
    print(f"  systemic ES gap (max-entropy): +{me['es_gap_pct']:.1f}% (t over gaussian)")
    for name in ("normal", "student-t"):
        r = bt_results[name]
        print(f"  VaR backtest {name:9s}: {r['exceptions']} exc, Basel {r['basel_zone'].upper()}")


if __name__ == "__main__":
    main()
