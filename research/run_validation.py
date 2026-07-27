#!/usr/bin/env python3
"""Run the RiskSim model-validation studies and emit a markdown report + figures.

Outputs:
    research/reports/model-validation.md
    research/reports/figures/loss_distribution.png
    research/reports/figures/gaussian_vs_t_tail.png
    research/reports/figures/var_exceedance_timeline.png

Everything numeric in the report comes from an actual run of the compiled
``risksim`` C++ binding and the pure-numpy backtests -- nothing is hard-coded.
"""

from __future__ import annotations

import sys
from datetime import date
from pathlib import Path

import numpy as np

_HERE = Path(__file__).resolve().parent
_REPO = _HERE.parent
for _p in (str(_HERE), str(_REPO / "engine" / "py")):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

import risksim as ok  # noqa: E402
from risksim_research import study  # noqa: E402

REPORTS = _HERE / "reports"
FIGURES = REPORTS / "figures"


def _fmt(x, nd=4):
    return f"{x:.{nd}f}"


def figure_loss_distribution(losses, var, es, alpha, path):
    fig, ax = plt.subplots(figsize=(8, 4.5))
    ax.hist(losses, bins=120, color="#4c72b0", alpha=0.8, density=True)
    ax.axvline(var, color="#dd8452", lw=2, label=f"VaR{int(alpha*1000)/10:g}% = {var:.2f}")
    ax.axvline(es, color="#c44e52", lw=2, ls="--", label=f"ES{int(alpha*1000)/10:g}% = {es:.2f}")
    ax.set_title("Systemic loss distribution (Student-t copula)")
    ax.set_xlabel("system loss")
    ax.set_ylabel("density")
    ax.legend()
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    plt.close(fig)


def figure_gaussian_vs_t_tail(g_losses, t_losses, g_es, t_es, alpha, path):
    fig, ax = plt.subplots(figsize=(8, 4.5))
    lo = min(g_losses.min(), t_losses.min())
    hi = max(g_losses.max(), t_losses.max())
    bins = np.linspace(lo, hi, 140)
    ax.hist(g_losses, bins=bins, histtype="step", lw=1.8, color="#4c72b0",
            density=True, label="Gaussian copula")
    ax.hist(t_losses, bins=bins, histtype="step", lw=1.8, color="#c44e52",
            density=True, label="Student-t copula")
    ax.axvline(g_es, color="#4c72b0", ls="--", lw=1.5)
    ax.axvline(t_es, color="#c44e52", ls="--", lw=1.5)
    ax.set_yscale("log")
    ax.set_title(f"Tail of systemic loss: Gaussian vs Student-t copula "
                 f"(ES gap {(t_es-g_es)/g_es*100:.1f}%)")
    ax.set_xlabel("system loss")
    ax.set_ylabel("density (log)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    plt.close(fig)


def figure_var_timeline(res, path):
    losses = res.losses[res.window :]
    idx = np.arange(losses.size)
    vn = res.normal.var_series
    vt = res.student_t.var_series
    fig, ax = plt.subplots(figsize=(10, 4.5))
    ax.plot(idx, losses, color="#8c8c8c", lw=0.7, alpha=0.8, label="realised loss")
    ax.plot(idx, vn, color="#4c72b0", lw=1.2, label="Normal VaR")
    ax.plot(idx, vt, color="#c44e52", lw=1.2, label="Student-t VaR")
    exc_n = res.normal.hits.astype(bool)
    ax.scatter(idx[exc_n], losses[exc_n], s=18, color="#4c72b0",
               marker="v", label=f"Normal exceptions ({res.normal.exceptions})")
    ax.set_title(f"Rolling {int(res.alpha*100)}% VaR exceedances "
                 f"(GARCH-t returns, nu={res.nu:g})")
    ax.set_xlabel("trading day (out-of-sample)")
    ax.set_ylabel("one-day loss")
    ax.legend(loc="upper left", ncol=2, fontsize=8)
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    plt.close(fig)


def main():
    REPORTS.mkdir(parents=True, exist_ok=True)
    FIGURES.mkdir(parents=True, exist_ok=True)

    # ---- Study (a): copula tail --------------------------------------------
    net = ok.generate_network(n_core=6, n_periphery=44, seed=20260726)
    tail = study.copula_tail_study(
        net, node_vol=0.30, correlation=0.5, nu=3.0, num_paths=60_000,
        seed=20260726, model="furfine", alpha=0.975, antithetic=True,
    )

    figure_loss_distribution(tail.t_losses, tail.t_var, tail.t_es, 0.975,
                             FIGURES / "loss_distribution.png")
    figure_gaussian_vs_t_tail(tail.gaussian_losses, tail.t_losses, tail.gaussian_es,
                             tail.t_es, 0.975, FIGURES / "gaussian_vs_t_tail.png")

    # ---- Study (b): rolling VaR backtest -----------------------------------
    bt_res = study.var_backtest_driver(n=1500, window=250, alpha=0.99, nu=4.0, seed=7)
    figure_var_timeline(bt_res, FIGURES / "var_exceedance_timeline.png")

    # ---- Write report -------------------------------------------------------
    md = []
    md.append("# RiskSim model-validation report\n")
    md.append(f"_Generated {date.today().isoformat()} from an actual run of the "
              f"compiled `risksim` C++ binding._\n")

    md.append("## 1. Copula tail dependence: Gaussian vs Student-t\n")
    md.append(
        "Identical core-periphery network, per-node volatility, correlation and "
        "contagion model; **only the copula changes.** The Gaussian copula has "
        "zero tail dependence; the Student-t copula's tail dependence inflates "
        "systemic Expected Shortfall.\n"
    )
    md.append(
        f"- Network: {net.n_core} core + {net.n_periphery} periphery = {net.n} banks\n"
        f"- node_vol = {tail.node_vol}, correlation = {tail.correlation}, "
        f"nu = {tail.nu}, paths = {tail.num_paths} (antithetic), model = furfine, "
        f"alpha = 0.975\n"
    )
    md.append("\n| copula | VaR (97.5%) | ES (97.5%) |\n|---|---|---|\n"
              f"| Gaussian | {_fmt(tail.gaussian_var,3)} | {_fmt(tail.gaussian_es,3)} |\n"
              f"| Student-t | {_fmt(tail.t_var,3)} | {_fmt(tail.t_es,3)} |\n")
    md.append(f"\n**Headline: Student-t systemic ES is "
              f"{tail.es_gap_pct:.1f}% higher than Gaussian** "
              f"(VaR gap {tail.var_gap_pct:.1f}%). The bivariate t tail-dependence "
              f"coefficient at rho={tail.correlation}, nu={tail.nu:g} is "
              f"{ok.t_copula_tail_dependence(tail.correlation, tail.nu):.3f} "
              f"(vs 0 for the Gaussian).\n")
    md.append("\n![loss distribution](figures/loss_distribution.png)\n")
    md.append("\n![gaussian vs t tail](figures/gaussian_vs_t_tail.png)\n")

    md.append("\n## 2. VaR backtest: Normal vs Student-t model under fat tails\n")
    md.append(
        "A GARCH(1,1) return series with Student-t innovations "
        f"(nu={bt_res.nu:g}) supplies volatility clustering and fat tails. "
        f"Rolling {bt_res.window}-day one-day-ahead {int(bt_res.alpha*100)}% VaR "
        "forecasts are formed with a Normal model (core `norm_inv_cdf`) and a "
        "Student-t model (core `students_t_inv_cdf`), then backtested.\n"
    )

    def model_rows(m):
        return (
            f"| {m.name} | {m.exceptions} | {m.expected:.1f} | "
            f"{_fmt(m.kupiec_lr,3)} | {_fmt(m.kupiec_p,4)} | "
            f"{_fmt(m.christoffersen_lr_cc,3)} | {_fmt(m.christoffersen_p_cc,4)} | "
            f"**{m.basel_zone}** | {_fmt(m.basel_multiplier,2)} |\n"
        )

    md.append(
        "\n| model | exceptions | expected | Kupiec LR | Kupiec p | "
        "Christoffersen CC LR | CC p | Basel zone | capital mult |\n"
        "|---|---|---|---|---|---|---|---|---|\n"
    )
    md.append(model_rows(bt_res.normal))
    md.append(model_rows(bt_res.student_t))

    md.append(
        f"\n**Headline: the Normal model records {bt_res.normal.exceptions} "
        f"exceptions ({bt_res.normal.basel_zone} zone, capital multiplier "
        f"{bt_res.normal.basel_multiplier:.2f}) versus {bt_res.student_t.exceptions} "
        f"for the Student-t model ({bt_res.student_t.basel_zone} zone, multiplier "
        f"{bt_res.student_t.basel_multiplier:.2f}).** Under fat tails the Normal "
        "VaR under-covers and is penalised with a higher capital add-on.\n"
    )
    md.append("\n![VaR exceedance timeline](figures/var_exceedance_timeline.png)\n")

    md.append("\n## 3. Methodology / references\n")
    md.append(
        "- VaR/ES and the systemic loss sample are computed by the shipping C++ "
        "core via the nanobind `risksim` module (same kernels as production).\n"
        "- Kupiec (1995) POF unconditional coverage; Christoffersen (1998) "
        "independence + conditional coverage; Basel (1996/2006) traffic light; "
        "Acerbi-Szekely (2014) ES backtests. See `risksim_research/backtests.py` "
        "for formulae.\n"
    )

    report_path = REPORTS / "model-validation.md"
    report_path.write_text("".join(md))

    print(f"Wrote {report_path}")
    print(f"ES gap (t vs gaussian): {tail.es_gap_pct:.1f}%")
    print(f"Normal model: {bt_res.normal.exceptions} exc, zone={bt_res.normal.basel_zone}, "
          f"mult={bt_res.normal.basel_multiplier:.2f}")
    print(f"Student-t model: {bt_res.student_t.exceptions} exc, zone={bt_res.student_t.basel_zone}, "
          f"mult={bt_res.student_t.basel_multiplier:.2f}")


if __name__ == "__main__":
    main()
