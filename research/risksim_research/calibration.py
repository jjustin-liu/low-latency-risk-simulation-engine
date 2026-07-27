"""Calibrate the systemic-risk model to REAL market data.

Pipeline
--------
1. Fetch daily equity closes for a basket of large US banks (Yahoo Finance chart
   API, cached to ``research/data/prices/`` so runs are reproducible/offline).
2. Estimate per-bank annualized volatility and a Ledoit-Wolf-shrunk correlation
   matrix from log returns (shrinkage keeps it well-conditioned and SPD so the
   copula's Cholesky factorization succeeds).
3. Turn approximate public balance-sheet sizes into interbank marginals + equity,
   from which the confidential bilateral matrix is reconstructed (max-entropy /
   min-density) inside the C++ core.

Everything downstream of step 1 is deterministic. Network access is only needed
to (re)build the price cache.
"""
from __future__ import annotations

import csv
import json
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path

import numpy as np

_HERE = Path(__file__).resolve().parent
_DATA = _HERE.parents[0] / "data"
_PRICES = _DATA / "prices"
TRADING_DAYS = 252


@dataclass
class Calibration:
    tickers: list[str]
    names: list[str]
    total_assets: np.ndarray      # USD bn, per bank
    ann_vol: np.ndarray           # annualized volatility, per bank
    corr: np.ndarray              # Ledoit-Wolf-shrunk correlation matrix (SPD)
    returns: np.ndarray           # aligned daily log returns, shape (T, n)
    weights: np.ndarray           # asset-weighted portfolio weights
    start: str
    end: str


# --------------------------------------------------------------------------- IO
def _yahoo_url(ticker: str, p1: int, p2: int) -> str:
    return (
        f"https://query1.finance.yahoo.com/v8/finance/chart/{ticker}"
        f"?period1={p1}&period2={p2}&interval=1d"
    )


def fetch_prices(
    tickers: list[str],
    start: str = "2018-01-01",
    end: str = "2024-12-31",
    cache_dir: Path = _PRICES,
    refresh: bool = False,
) -> dict[str, tuple[list[int], list[float]]]:
    """Return {ticker: (unix_days, closes)}. Cached CSVs are used when present."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    p1 = int(time.mktime(time.strptime(start, "%Y-%m-%d")))
    p2 = int(time.mktime(time.strptime(end, "%Y-%m-%d")))
    out: dict[str, tuple[list[int], list[float]]] = {}
    for t in tickers:
        cache = cache_dir / f"{t}.csv"
        if cache.exists() and not refresh:
            ts, cl = [], []
            with cache.open() as f:
                for row in csv.reader(f):
                    if row and row[0] != "unixday":
                        ts.append(int(row[0]))
                        cl.append(float(row[1]))
            out[t] = (ts, cl)
            continue
        doc = None
        last_err: Exception | None = None
        for host in ("query1", "query2"):
            url = _yahoo_url(t, p1, p2).replace("query1", host)
            req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (Macintosh)"})
            try:
                with urllib.request.urlopen(req, timeout=30) as resp:
                    doc = json.load(resp)
                break
            except Exception as e:  # noqa: BLE001
                last_err = e
        if doc is None:
            raise RuntimeError(f"failed to fetch {t} from Yahoo: {last_err}")
        res = doc["chart"]["result"][0]
        ts = list(res["timestamp"])
        cl = list(res["indicators"]["quote"][0]["close"])
        # Drop any None closes (holidays / bad ticks).
        pairs = [(d, c) for d, c in zip(ts, cl) if c is not None]
        ts, cl = [p[0] for p in pairs], [p[1] for p in pairs]
        with cache.open("w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["unixday", "close"])
            w.writerows(zip(ts, cl))
        out[t] = (ts, cl)
    return out


def _align_returns(prices: dict[str, tuple[list[int], list[float]]], tickers: list[str]):
    """Align on common trading days, return (T, n) log-return matrix."""
    day_sets = [set(prices[t][0]) for t in tickers]
    common = sorted(set.intersection(*day_sets))
    idx = {t: {d: i for i, d in enumerate(prices[t][0])} for t in tickers}
    closes = np.array([[prices[t][1][idx[t][d]] for t in tickers] for d in common])
    rets = np.diff(np.log(closes), axis=0)
    return rets


# ----------------------------------------------------------- Ledoit-Wolf shrink
def ledoit_wolf_correlation(returns: np.ndarray) -> np.ndarray:
    """Ledoit-Wolf (2004) linear shrinkage of the sample covariance toward a
    scaled identity, converted to a correlation matrix. Guarantees a
    well-conditioned SPD result."""
    n, p = returns.shape
    x = returns - returns.mean(axis=0, keepdims=True)
    s = (x.T @ x) / n                       # MLE covariance
    mu = np.trace(s) / p
    d2 = np.sum((s - mu * np.eye(p)) ** 2) / p
    # Average squared Frobenius distance of each rank-1 obs to S.
    b_bar2 = 0.0
    for k in range(n):
        xk = x[k][:, None]
        rk = xk @ xk.T - s
        b_bar2 += np.sum(rk ** 2) / p
    b_bar2 /= n ** 2
    b2 = min(b_bar2, d2)
    shrink = b2 / d2 if d2 > 0 else 1.0
    cov = shrink * mu * np.eye(p) + (1.0 - shrink) * s
    dinv = 1.0 / np.sqrt(np.diag(cov))
    corr = cov * np.outer(dinv, dinv)
    np.fill_diagonal(corr, 1.0)
    return corr


# --------------------------------------------------------------------- assemble
def load_banks(path: Path = _DATA / "banks.json") -> dict:
    return json.loads(path.read_text())


def calibrate(start: str = "2018-01-01", end: str = "2024-12-31",
              refresh: bool = False) -> Calibration:
    cfg = load_banks()
    banks = cfg["banks"]
    tickers = [b["ticker"] for b in banks]
    names = [b["name"] for b in banks]
    ta = np.array([float(b["total_assets_busd"]) for b in banks])

    prices = fetch_prices(tickers, start, end, refresh=refresh)
    rets = _align_returns(prices, tickers)
    ann_vol = rets.std(axis=0, ddof=1) * np.sqrt(TRADING_DAYS)
    corr = ledoit_wolf_correlation(rets)
    weights = ta / ta.sum()
    return Calibration(tickers, names, ta, ann_vol, corr, rets, weights, start, end)


def marginals(cal: Calibration, interbank_fraction: float = 0.15,
              capital_ratio: float = 0.08):
    """Balance-sheet marginals for network reconstruction.

    interbank assets = interbank liabilities = fraction * total assets (so the
    marginals are automatically consistent for Sinkhorn); equity = capital_ratio
    * total assets; external (shock-exposed) assets = interbank fraction of total
    assets (mirrors the engine's baseline-solvent convention).
    """
    ib = interbank_fraction * cal.total_assets
    assets_in = ib.copy()          # column sums (what others owe bank i)
    liabilities_out = ib.copy()    # row sums (what bank i owes others)
    equity = capital_ratio * cal.total_assets
    # A systemic shock marks down the whole balance sheet, not just the interbank
    # slice, so the shock-exposed base is total assets (default point when the
    # fractional shock exceeds the capital ratio).
    external_assets = cal.total_assets.copy()
    return assets_in, liabilities_out, equity, external_assets
