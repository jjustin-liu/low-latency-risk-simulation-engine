"""Pytest bootstrap: make both the research package and the compiled
``risksim`` binding importable regardless of the working directory / install
state."""

from __future__ import annotations

import sys
from pathlib import Path

_here = Path(__file__).resolve()
_research_dir = _here.parents[1]  # <repo>/research
_repo_root = _here.parents[2]  # <repo>
_binding_dir = _repo_root / "engine" / "py"  # holds the `risksim` package

for p in (str(_research_dir), str(_binding_dir)):
    if p not in sys.path:
        sys.path.insert(0, p)
