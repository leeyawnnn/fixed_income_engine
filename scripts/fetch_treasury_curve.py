#!/usr/bin/env python3
"""Download a year of US Treasury Daily Par Yield Curve Rates (CMT) as CSV.

The series is published by the US Department of the Treasury at roughly
15:30 ET on each business day and is free to redistribute (US government work,
no copyright). This script pulls one calendar year, normalises the layout, and
writes a `.meta.json` sibling recording where the data came from and when.

    python3 scripts/fetch_treasury_curve.py --year 2025

What these rates are, and what they are not
-------------------------------------------
CMT rates are *par yields*: the coupon a hypothetical Treasury security would
need to trade at exactly 100. They are not observed transaction yields. Treasury
derives them from the secondary-market bid-side yields of the on-the-run
securities and then interpolates to the constant maturities below, using a
monotone convex spline since 2021-12-06 (a quasi-cubic Hermite spline before
that). Bootstrapping them therefore gives a zero curve consistent with
Treasury's own interpolation of the on-the-run points -- not one implied by the
full set of underlying bond prices.

The 1.5-month constant maturity begins on 2025-02-18; it is blank before then,
and the loader leaves those cells empty rather than filling them.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import io
import json
import pathlib
import ssl
import subprocess
import urllib.request

# Verified to resolve on 2026-09-18.
URL_TEMPLATE = (
    "https://home.treasury.gov/resource-center/data-chart-center/interest-rates/"
    "daily-treasury-rates.csv/{year}/all"
    "?type=daily_treasury_yield_curve&field_tdr_date_value={year}&page&_format=csv"
)

# Treasury's own column order, shortest first. The header text on the wire uses
# a mix of "Mo"/"Month"/"Yr"; these are the canonical names we write out.
TENORS: list[tuple[str, float]] = [
    ("1M", 1 / 12),
    ("1.5M", 1.5 / 12),
    ("2M", 2 / 12),
    ("3M", 3 / 12),
    ("4M", 4 / 12),
    ("6M", 0.5),
    ("1Y", 1.0),
    ("2Y", 2.0),
    ("3Y", 3.0),
    ("5Y", 5.0),
    ("7Y", 7.0),
    ("10Y", 10.0),
    ("20Y", 20.0),
    ("30Y", 30.0),
]

SOURCE_HEADERS = {
    "1M": ("1 Mo",),
    "1.5M": ("1.5 Month", "1.5 Mo"),
    "2M": ("2 Mo",),
    "3M": ("3 Mo",),
    "4M": ("4 Mo",),
    "6M": ("6 Mo",),
    "1Y": ("1 Yr",),
    "2Y": ("2 Yr",),
    "3Y": ("3 Yr",),
    "5Y": ("5 Yr",),
    "7Y": ("7 Yr",),
    "10Y": ("10 Yr",),
    "20Y": ("20 Yr",),
    "30Y": ("30 Yr",),
}

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent


def ssl_context() -> ssl.SSLContext:
    """Verified TLS, with certifi's bundle when the interpreter has no roots.

    A python.org build on macOS ships without wiring up a CA store, so
    urlopen fails with CERTIFICATE_VERIFY_FAILED on any https URL until
    `Install Certificates.command` is run. Falling back to certifi keeps the
    script working there. Verification is never disabled.
    """
    try:
        import certifi
    except ImportError:
        return ssl.create_default_context()
    return ssl.create_default_context(cafile=certifi.where())


def fetch(year: int, timeout: int = 60) -> str:
    url = URL_TEMPLATE.format(year=year)
    request = urllib.request.Request(
        url, headers={"User-Agent": "fixed-income-engine/0.1 (+data fetch)"}
    )
    with urllib.request.urlopen(
        request, timeout=timeout, context=ssl_context()
    ) as response:
        if response.status != 200:
            raise RuntimeError(f"Treasury returned HTTP {response.status} for {url}")
        return response.read().decode("utf-8-sig")


def normalise(raw_csv: str) -> list[dict[str, str]]:
    """Reorder to ascending date and ascending tenor, keeping blanks blank."""
    reader = csv.DictReader(io.StringIO(raw_csv))
    if reader.fieldnames is None:
        raise RuntimeError("Treasury CSV had no header row")

    available = {name.strip(): name for name in reader.fieldnames}
    column_for: dict[str, str] = {}
    for tenor, candidates in SOURCE_HEADERS.items():
        for candidate in candidates:
            if candidate in available:
                column_for[tenor] = available[candidate]
                break
    missing = [t for t, _ in TENORS if t not in column_for]
    if missing:
        raise RuntimeError(f"Treasury CSV is missing expected columns: {missing}")

    rows: list[dict[str, str]] = []
    for source_row in reader:
        month, day, year_s = source_row["Date"].strip().split("/")
        date = dt.date(int(year_s), int(month), int(day))
        row = {"date": date.isoformat()}
        for tenor, _ in TENORS:
            row[tenor] = source_row[column_for[tenor]].strip()
        rows.append(row)

    rows.sort(key=lambda r: r["date"])
    return rows


def git_commit() -> str:
    try:
        return subprocess.run(
            ["git", "-C", str(REPO_ROOT), "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, OSError):
        return "unknown"


def write_outputs(rows: list[dict[str, str]], year: int, out_path: pathlib.Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with out_path.open("w", newline="\n", encoding="utf-8") as handle:
        handle.write(
            "# US Treasury Daily Par Yield Curve Rates (CMT), percent per annum.\n"
            "# Par yields on hypothetical Treasury securities, interpolated by\n"
            "# Treasury from on-the-run bid-side yields. Not transaction yields.\n"
            f"# Source: {URL_TEMPLATE.format(year=year)}\n"
            "# Regenerate: python3 scripts/fetch_treasury_curve.py --year "
            f"{year}\n"
        )
        writer = csv.DictWriter(handle, fieldnames=["date", *(t for t, _ in TENORS)])
        writer.writeheader()
        writer.writerows(rows)

    digest = hashlib.sha256(out_path.read_bytes()).hexdigest()
    meta = {
        "dataset": "US Treasury Daily Par Yield Curve Rates (CMT)",
        "publisher": "US Department of the Treasury",
        "source_url": URL_TEMPLATE.format(year=year),
        "landing_page": (
            "https://home.treasury.gov/resource-center/data-chart-center/"
            "interest-rates/TextView?type=daily_treasury_yield_curve"
        ),
        "units": "percent per annum, par yield (CMT)",
        "year": year,
        "first_date": rows[0]["date"],
        "last_date": rows[-1]["date"],
        "observations": len(rows),
        "retrieved_utc": dt.datetime.now(dt.UTC).isoformat(timespec="seconds"),
        "produced_by": f"python3 scripts/fetch_treasury_curve.py --year {year}",
        "git_commit": git_commit(),
        "sha256": digest,
        "licence": "US Government work, not subject to domestic copyright (17 USC 105)",
        "notes": [
            "Par yields, not zero rates and not observed trade yields.",
            (
                "Interpolated by Treasury with a monotone convex spline from "
                "2021-12-06; a quasi-cubic Hermite spline before that date."
            ),
            (
                "The 1.5-month constant maturity series begins 2025-02-18; earlier "
                "cells are empty and are left empty here."
            ),
            "Published around 15:30 ET on US business days.",
        ],
    }
    out_path.with_suffix(".meta.json").write_text(
        json.dumps(meta, indent=2) + "\n", encoding="utf-8"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--year", type=int, required=True, help="calendar year to pull")
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        default=None,
        help="output CSV path (default data/treasury_par_yields_<year>.csv)",
    )
    args = parser.parse_args()

    out_path = args.out or REPO_ROOT / "data" / f"treasury_par_yields_{args.year}.csv"
    rows = normalise(fetch(args.year))
    if not rows:
        raise SystemExit(f"Treasury returned no observations for {args.year}")
    write_outputs(rows, args.year, out_path)
    span = f"{rows[0]['date']} to {rows[-1]['date']}"
    print(f"wrote {out_path} ({len(rows)} observations, {span})")
    print(f"wrote {out_path.with_suffix('.meta.json')}")


if __name__ == "__main__":
    main()
