# FIXES

A record of this pass, for Lyonn. Temporary: fold what is useful into the
README's Limitations section and delete the file.

## What was broken

**Generation scaffolding on `main`.** `reports/RECHECK.md` was a document
auditing the repo against the brief that produced it — constraints table,
code-quality checklist, twelve phases with tick marks. `src/placeholder.cpp`
described itself as existing "so the `fi` library has something to compile in
Phase 0", eleven real sources later. `include/fi/risk.hpp` referenced "the
Phase 2/3 discounting". All gone, and the one genuinely useful admission in
RECHECK (that the `int64_t` money constraint was unmet) became a real type.

**Numbers that came from nowhere checkable.** The README's worked example was a
console paste. `data/treasury_yields.csv` was ten invented numbers under a name
that reads as market data, and a test called "Fit to **real** Treasury yields"
read it. `data/swap_rates.csv` was hand-written and presented as market quotes.

**The build did not work anywhere restrictive.** Eigen and Catch2 were cloned by
git at configure time with no fallback. Now both resolve to a system copy first
and otherwise download a SHA-256-verified release tarball, and `-DFI_OFFLINE=ON`
fails configure with an actionable message rather than reaching for the network.

**No CI at all**, so nothing verified that a clean clone built, that the tests
passed, or that the README's claims were still true.

**Line 3 of the README was not a sentence.** The commit `docs: update README
punctuation` had not fixed it.

## Claims retracted

- **"RMSE 0.81 bp"** for the Nelson–Siegel–Svensson fit. That was fitted to ten
  yields that did not exist. On thirteen real CMT tenors the same code achieves
  **2.10 bp**, with a worst tenor of 4.21 bp at 20Y. The worse number is
  published and nothing was tuned to recover the nicer one.
- **"Earlier instruments stay repriced exactly as longer ones are added, because
  node reproduction plus the locality of the interpolation means a new long node
  never disturbs discount factors at shorter tenors."** True of the two linear
  schemes, false of monotone convex. See "Additional findings".
- **The entire worked example**, which was invented swap quotes valued on
  2024-01-02. Replaced by real Treasury CMT data for 2025-12-31.
- **"Dates done properly … Money discipline"** in the Design constraints section,
  which conceded in parentheses that the money constraint was not actually met.
  Now met; the concession is replaced by an explanation of the boundary.
- The instruction-shaped aside telling the reader that figures are regenerated
  and that `reports/RECHECK.md` embeds them.

One claim that was **not** false and is worth knowing: the README's "68 test
cases / 245 assertions" was accurate. I verified it against a real run before
changing anything. It now reads 96 / 3,472.

## Numbers that changed, and why

| Claim | Was | Now | Why |
|---|---|---|---|
| NSS fit RMSE | 0.81 bp | 2.10 bp | Fitted to real CMT yields instead of ten invented ones. Six parameters cannot interpolate thirteen real tenors. |
| Valuation date | 2024-01-02 | 2025-12-31 | Real published data, pinned to the last business day of a complete year. |
| Portfolio PV | −$203,004.53 | +$8,771.15 | Different curve, and the struck rates were re-chosen to sit 27bp and 32bp off the real par rates. |
| Portfolio DV01 | $564.92/bp | $449.51/bp | Same. |
| Test counts | 68 / 245 | 96 / 3,472 | New suites for Money, portfolio, interpolation and analytics. |
| Bootstrap repricing | not measured | worst 1.8×10⁻¹¹ bp | New; the table did not exist. |

## What was added

- `Money`: `int64_t` minor units, currency tag, per-currency minor exponent
  (JPY has none), banker's rounding stated at the call site, no implicit
  conversion in either direction, no `operator*(double)`, range-checked
  arithmetic. Applied at the portfolio definition and reporting layers only.
- Real data: `scripts/fetch_treasury_curve.py` and `scripts/fetch_sofr.py`, each
  writing a `.meta.json` with source URL, UTC retrieval time, git commit, row
  count, date span and SHA-256.
- Hagan–West monotone convex interpolation, plus an `Interpolation` selector so
  the scheme is chosen once and carried through bootstrapping and bumping.
- `ParBondQuote`, `repricing_residual()`, `instrument_label()` — enough to print
  a residual table for any bootstrap.
- Bond: accrued interest, clean vs dirty price, Z-spread.
- Swap: PV01 (and a comment on why it is not DV01), signed leg breakdown.
- Risk: portfolio aggregation, key-rate reconciliation against the parallel
  DV01, least-squares bucketed hedging via Eigen's column-pivoting QR, curve
  gamma, and duration-vs-convexity attribution.
- `fi_report`, which writes every artifact under `reports/`.
- `tools/style.py` + rewritten `tools/make_figures.py`: seven deterministic SVGs
  that read the artifacts and compute nothing.
- CI: build matrix, offline build, ASan/UBSan and TSan, clang-format,
  clang-tidy, and a job that regenerates the artifacts and figures and fails on
  any diff under `reports/`.

## Additional findings (not in the brief)

1. **`find_package` was resolving through CMake's user package registry.** After
   adding the system-copy fallback, configure reported "using system Eigen3
   3.4.0" on a machine with **no Eigen installed**. `~/.cmake/packages/Eigen3`
   held four entries pointing at `_deps` build trees belonging to three other
   projects, one of them a temp directory that no longer existed — written there
   by Eigen's own `export(PACKAGE)` when built as a subproject. Fixed with
   `NO_CMAKE_PACKAGE_REGISTRY`, and this repo now sets
   `CMAKE_EXPORT_NO_PACKAGE_REGISTRY` so it stops adding to the mess.
   **This affects your other C++ repos too** — the same stale entries point at
   `options_pricing_engine` and `risk_management_system-`.

2. **Sequential bootstrapping is wrong for non-local interpolation.** The
   one-pass bootstrap assumes a long node does not disturb shorter tenors. Under
   monotone convex it does. Measured: holds at 10⁻¹² bp through the 2Y node,
   0.009 bp by 3Y, 0.033 bp by 7Y, **1.58 bp** once 30Y is added. Fixed by
   treating the sequential pass as a starting guess and re-solving every node
   against the whole curve in Gauss–Seidel sweeps — log-linear needs none,
   linear-in-zero two, monotone convex four.

3. **A region-selection bug in my own monotone convex implementation.** The
   condition for the plain-quadratic branch had the two boundaries the wrong way
   round for both signs of `g0`, so that branch was unreachable and pairs fell
   into a degenerate case with a near-singular denominator. It showed up as a
   65bp discontinuity in the forward curve at the 5Y node. Caught by the
   continuity test, not by inspection.
   **Correction to an earlier commit message:** `feat(bootstrap): add par bond
   quotes, residuals, and global node refinement` quotes "394 bp by 20Y, and the
   30Y solve then fails outright". Those were measured against the buggy
   implementation. The corrected figures are in finding 2 above and in the
   README. The refinement is still necessary; the magnitude was overstated.

4. **`Swap::float_day_count_` was a dead private field** — written by the
   constructor, never read. Rather than delete it, the class now exposes all
   four leg conventions and documents why the float day count does not enter
   `pv()`: the projected coupon and its accrual are on the same basis, so
   `L_j·τ_j` collapses to `DF(t_{j-1})/DF(t_j) − 1` and the day count cancels.

5. **`library_version()` was declared only inside `tests/test_smoke.cpp`.**
   Definition and sole declaration in different translation units, so a
   signature change would have surfaced as a link error. Declaration moved to
   `fi/version.hpp`.

6. **Both fetch scripts wrote CRLF.** `csv.writer` defaults to CRLF regardless of
   how the file is opened, while `.gitattributes` normalises CSVs to LF — so the
   SHA-256 recorded in each `.meta.json` described a file nobody would ever
   check out. Caught by verifying the checksums rather than trusting them.

7. **A test of mine asserted a number I had guessed.** The convexity test
   originally required duration's relative error to be under 1% at 25bp. It is
   1.17% on that book. Replaced with an assertion about the *shape* of the error
   — quadratic absolute error against a linear move, so relative error scales
   with the shift — which is what the data actually shows.

## Deviations from the brief

- **§0.4 "Prefer a GitHub-hosted mirror where an official one exists."** Eigen
  has no GitHub mirror: `https://github.com/libeigen/eigen` returns 404 and so
  does the API (checked 2026-09-18). The GitLab release tarball is the only
  pinnable official source, so it stays, pinned by hash.
- **§3.2 SOFR/OIS curve.** There is no free redistributable USD SOFR swap curve.
  FRED's SOFR averages are backward-looking realised compounds and cannot give a
  forward curve, the ICE Swap Rate series (`ICERATES1100USD*`) return 404, and
  CME Term SOFR is licensed. Discounting stays on the Treasury curve, stated as
  a simplification in the README and `data/README.md`, with the overnight SOFR
  series committed as evidence for the claim.
- **§5 "replace the current chart with a tornado/waterfall".** A waterfall
  implies the bars accumulate. These scenarios are alternatives, not
  contributions to one total, so a waterfall would be misleading. It is a sorted
  signed bar chart with the base PV called out instead.

## Not done — needs you

**The CI badge.** §0.9 requires CI green on a real runner with a live badge. I
cannot push: `gh` is not authenticated here and pushing is yours to approve. The
workflow is written and every job's command has been run locally and passes. To
finish:

```sh
git push origin main
gh run watch          # or check the Actions tab
```

Then add this line under the README's title, once the run is green:

```markdown
[![ci](https://github.com/leeyawnnn/fixed_income_engine/actions/workflows/ci.yml/badge.svg)](https://github.com/leeyawnnn/fixed_income_engine/actions/workflows/ci.yml)
```

**The test count in the README is hand-verified, not CI-rendered.** §2.2 asked
for it to come from the CI run. It currently comes from a local
`./build/fi_tests` run (96 cases / 3,472 assertions) and the README names the
command. Wiring the actual count into the README needs a job that edits and
commits it, which trades one kind of staleness for another; the artifact
staleness check under `reports/` is the stronger guarantee and it is in place.

**Repo metadata**, which needs the web UI or an authenticated `gh`:

```sh
gh repo edit leeyawnnn/fixed_income_engine \
  --description "C++20 fixed income analytics: yield curve bootstrapping, Nelson-Siegel-Svensson fitting, OIS-discounted swap valuation, and key-rate risk." \
  --add-topic cpp20 --add-topic fixed-income --add-topic yield-curve \
  --add-topic bootstrapping --add-topic interest-rate-swaps \
  --add-topic quantitative-finance --add-topic nelson-siegel
```

Note the repository is `fixed_income_engine` and the README title is now
`fixed-income-engine`. The brief asked for hyphen-case on both; renaming the
repo breaks existing clone URLs, so I left that to you.
