"""RiskSim research / model-validation layer.

Pure-numpy/scipy backtests plus studies that drive the compiled ``risksim``
bindings (the shipping C++ core).

Importing this package makes the ``risksim`` extension importable even without
an editable install, by adding the in-tree binding directory
(``<repo>/engine/py``) to ``sys.path`` when ``risksim`` is not already
importable.
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


def _bootstrap_risksim_path() -> None:
    if importlib.util.find_spec("risksim") is not None:
        return
    repo_root = Path(__file__).resolve().parents[2]
    binding_dir = repo_root / "engine" / "py"
    if binding_dir.is_dir():
        p = str(binding_dir)
        if p not in sys.path:
            sys.path.insert(0, p)


_bootstrap_risksim_path()
