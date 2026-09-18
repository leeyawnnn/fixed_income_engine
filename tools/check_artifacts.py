#!/usr/bin/env python3
"""Verify the committed artifacts still match what the engine produces.

    ./build/fi_report --out /tmp/fresh
    python3 tools/check_artifacts.py /tmp/fresh

Why this is not `git diff`
--------------------------
The obvious check is to regenerate into `reports/` and fail on any diff. That
cannot work, and CI proved it: the committed artifacts are produced on macOS,
where `exp`, `log` and `pow` come from Apple's libm, and CI regenerates them on
Linux against glibc. The two agree to about fifteen significant figures and
disagree in the sixteenth. That is enough to move the last printed digit of a
fitted parameter, and enough to make the Levenberg-Marquardt fit take 42
iterations instead of 44.

Those differences are real but meaningless, and a check that fails on them
teaches you to ignore it. So this compares numerically, with a tolerance far
tighter than the precision the README quotes, and separately asserts the
headline claims outright. That is a stronger check than byte equality in the
way that matters: it fails when a number moves enough to change what the
README says, and only then.
"""

from __future__ import annotations

import csv
import json
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
COMMITTED = REPO_ROOT / "reports"

# Absolute tolerance per file, in the units that file prints.
TOLERANCE = {
    "curve_nodes.csv": 1e-6,
    "forward_curves.csv": 1e-6,
    "zero_curve.csv": 1e-6,
    "nss_fit.csv": 1e-4,
    "nss_params.csv": 1e-4,
    "key_rate_dv01.csv": 1e-3,
    "bucket_hedge.csv": 1e-2,
    "portfolio_valuation.csv": 1e-2,
    "scenario_pnl.csv": 1e-2,
    "shift_attribution.csv": 1e-2,
    "repricing_residuals.csv": 1e-6,
}

# Values that legitimately differ between platforms and carry no claim.
# `iterations` is a solver step count; the fit it produces is compared anyway.
SKIP_VALUES = {"iterations"}

# The README's headline numbers, asserted directly rather than by comparison.
CLAIMS = {
    "worst_repricing_residual_bp": ("<", 1e-8),
    "nss_rmse_bp": ("~", 2.1015, 0.01),
    "portfolio_dv01_per_bp": ("~", 449.51, 0.01),
}


def read_rows(path: pathlib.Path) -> list[list[str]]:
    with path.open(encoding="utf-8") as handle:
        lines = [line for line in handle if not line.startswith("#")]
    return [row for row in csv.reader(lines) if row]


def compare_cell(expected: str, actual: str, tol: float) -> str | None:
    if expected == actual:
        return None
    try:
        a, b = float(expected), float(actual)
    except ValueError:
        return f"{expected!r} != {actual!r}"
    if abs(a - b) <= tol:
        return None
    return f"{expected} != {actual} (differs by {abs(a - b):.3g}, tolerance {tol:g})"


def compare_file(name: str, fresh_dir: pathlib.Path) -> list[str]:
    committed, fresh = COMMITTED / name, fresh_dir / name
    if not fresh.exists():
        return [f"{name}: not produced by fi_report"]

    want, got = read_rows(committed), read_rows(fresh)
    if len(want) != len(got):
        return [f"{name}: {len(want)} rows committed, {len(got)} regenerated"]

    tol = TOLERANCE.get(name, 1e-6)
    problems = []
    for index, (wrow, grow) in enumerate(zip(want, got, strict=True), start=1):
        if len(wrow) != len(grow):
            problems.append(f"{name} row {index}: column count differs")
            continue
        if wrow and wrow[0] in SKIP_VALUES:
            continue
        for wcell, gcell in zip(wrow, grow, strict=True):
            if problem := compare_cell(wcell, gcell, tol):
                problems.append(f"{name} row {index}: {problem}")
    return problems


def check_claims(fresh_dir: pathlib.Path) -> list[str]:
    meta = json.loads((fresh_dir / "run.meta.json").read_text(encoding="utf-8"))
    problems = []
    for key, rule in CLAIMS.items():
        value = meta[key]
        if rule[0] == "<" and not value < rule[1]:
            problems.append(f"claim failed: {key} = {value}, expected < {rule[1]:g}")
        elif rule[0] == "~" and abs(value - rule[1]) > rule[2]:
            problems.append(
                f"claim failed: {key} = {value}, expected {rule[1]} +/- {rule[2]}"
            )
    return problems


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    fresh_dir = pathlib.Path(sys.argv[1])

    problems = check_claims(fresh_dir)
    for name in sorted(TOLERANCE):
        problems.extend(compare_file(name, fresh_dir))

    if problems:
        print("Committed artifacts no longer match what the engine produces:\n")
        for problem in problems[:40]:
            print(f"  {problem}")
        if len(problems) > 40:
            print(f"  ... and {len(problems) - 40} more")
        print("\nIf the change is intended, regenerate and commit:")
        print("  ./build/fi_report --out reports && python3 tools/make_figures.py")
        return 1

    print(f"All {len(TOLERANCE)} artifacts match within tolerance; headline claims hold.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
