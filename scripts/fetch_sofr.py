#!/usr/bin/env python3
"""Download the SOFR overnight fixing series from FRED as CSV.

    python3 scripts/fetch_sofr.py --year 2025

Why only the overnight rate
---------------------------
This repository discounts on the Treasury curve, not on an OIS curve, because
no free, redistributable USD SOFR *swap* curve exists. FRED carries the
overnight fixing (SOFR) and its backward-looking compounded averages
(SOFR30DAYAVG, SOFR90DAYAVG, SOFR180DAYAVG, SOFRINDEX), and all of those
resolve, but none of them is a term structure of OIS swap rates:

  * the averages are realised compounded overnight rates over a window that has
    already happened, so they say nothing about rates after today;
  * the ICE Swap Rate series FRED used to carry (ICERATES1100USD*) return 404;
  * CME Term SOFR is licensed and cannot be redistributed here.

So the overnight fixing is committed as evidence for that statement and as a
reference point for the short end, not as an input to any curve.
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

# Verified to resolve on 2026-09-18. No API key required for this endpoint.
URL_TEMPLATE = (
    "https://fred.stlouisfed.org/graph/fredgraph.csv?id={series}&cosd={start}&coed={end}"
)

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent


def ssl_context() -> ssl.SSLContext:
    """Verified TLS, with certifi's bundle when the interpreter has no roots."""
    try:
        import certifi
    except ImportError:
        return ssl.create_default_context()
    return ssl.create_default_context(cafile=certifi.where())


def fetch(series: str, start: str, end: str, timeout: int = 60) -> str:
    url = URL_TEMPLATE.format(series=series, start=start, end=end)
    request = urllib.request.Request(
        url, headers={"User-Agent": "fixed-income-engine/0.1 (+data fetch)"}
    )
    with urllib.request.urlopen(
        request, timeout=timeout, context=ssl_context()
    ) as response:
        if response.status != 200:
            raise RuntimeError(f"FRED returned HTTP {response.status} for {url}")
        return response.read().decode("utf-8-sig")


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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--year", type=int, required=True)
    parser.add_argument("--series", default="SOFR")
    parser.add_argument("--out", type=pathlib.Path, default=None)
    args = parser.parse_args()

    start, end = f"{args.year}-01-01", f"{args.year}-12-31"
    out_path = args.out or REPO_ROOT / "data" / f"sofr_overnight_{args.year}.csv"

    reader = csv.DictReader(io.StringIO(fetch(args.series, start, end)))
    rows = [
        {"date": r["observation_date"], "sofr": r[args.series]}
        for r in reader
        if r[args.series] not in ("", ".")
    ]
    if not rows:
        raise SystemExit(
            f"FRED returned no observations for {args.series} in {args.year}"
        )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="\n", encoding="utf-8") as handle:
        handle.write(
            "# SOFR overnight fixing, percent per annum.\n"
            "# Reference data only. This repository does NOT build a curve from it;\n"
            "# see data/README.md for why no free SOFR term curve is available.\n"
            f"# Source: {URL_TEMPLATE.format(series=args.series, start=start, end=end)}\n"
            f"# Regenerate: python3 scripts/fetch_sofr.py --year {args.year}\n"
        )
        writer = csv.DictWriter(handle, fieldnames=["date", "sofr"])
        writer.writeheader()
        writer.writerows(rows)

    meta = {
        "dataset": f"{args.series} (Secured Overnight Financing Rate), overnight fixing",
        "publisher": "Federal Reserve Bank of New York, via FRED (St. Louis Fed)",
        "source_url": URL_TEMPLATE.format(series=args.series, start=start, end=end),
        "landing_page": f"https://fred.stlouisfed.org/series/{args.series}",
        "units": "percent per annum",
        "year": args.year,
        "first_date": rows[0]["date"],
        "last_date": rows[-1]["date"],
        "observations": len(rows),
        "retrieved_utc": dt.datetime.now(dt.UTC).isoformat(timespec="seconds"),
        "produced_by": f"python3 scripts/fetch_sofr.py --year {args.year}",
        "git_commit": git_commit(),
        "sha256": hashlib.sha256(out_path.read_bytes()).hexdigest(),
        "licence": (
            "FRED terms of use; SOFR is published by the New York Fed under its "
            "own terms of use for reference rates."
        ),
        "notes": [
            (
                "Overnight fixing only. Not a term structure and not an input to any "
                "curve in this repository."
            ),
            (
                "No free redistributable USD SOFR swap curve exists; the FRED ICE Swap "
                "Rate series return 404 and CME Term SOFR is licensed."
            ),
        ],
    }
    out_path.with_suffix(".meta.json").write_text(
        json.dumps(meta, indent=2) + "\n", encoding="utf-8"
    )
    print(f"wrote {out_path} ({len(rows)} observations)")


if __name__ == "__main__":
    main()
