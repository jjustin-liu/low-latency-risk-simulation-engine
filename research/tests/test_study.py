"""Tests for the studies that drive the compiled binding."""

from __future__ import annotations

import risksim as ok
from risksim_research import study


def test_copula_tail_study_gap_positive():
    net = ok.generate_network(4, 20, seed=13)
    res = study.copula_tail_study(net, num_paths=8000, seed=101, model="furfine")
    assert res.t_es > res.gaussian_es
    assert res.es_gap_pct > 0.0
    assert res.gaussian_losses.shape[0] == res.t_losses.shape[0]


def test_var_backtest_driver_normal_worse_than_t():
    res = study.var_backtest_driver(n=1500, window=250, alpha=0.99, nu=4.0, seed=7)
    # Under fat tails the normal model under-covers -> more exceptions.
    assert res.normal.exceptions >= res.student_t.exceptions
    # Basel zone ordering: green < yellow < red.
    order = {"green": 0, "yellow": 1, "red": 2}
    assert order[res.normal.basel_zone] >= order[res.student_t.basel_zone]
    assert res.normal.basel_multiplier >= res.student_t.basel_multiplier
