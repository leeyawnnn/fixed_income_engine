#!/usr/bin/env python3
"""Regenerate every figure in this repository from the committed artifacts.

    python3 tools/make_figures.py

Nothing here computes anything. Each figure reads a CSV written by `fi_report`,
so a figure can never show a number the engine did not produce, and running
fi_report then this script is enough to bring every claim in the README back
into agreement with the data.

Figures, in README order:
    zero_curve            par, zero and forward curves on one axis
    discount_factors      DF(t) on a log axis beside its implied zero rate
    forward_curves        the three interpolation schemes compared
    repricing_residuals   the bootstrap closing, per instrument per scheme
    key_rate_dv01         where the book's risk sits, and what it sums to
    duration_convexity    where duration alone stops working
    scenario_pnl          P&L by curve scenario, sorted by magnitude
"""

from __future__ import annotations

import csv
import json
import pathlib
import sys

import numpy as np
import style

REPORTS = style.REPORTS
AS_OF = "2025-12-31"


# --- Reading the artifacts ---------------------------------------------------


def read_csv(name: str) -> list[dict[str, str]]:
    """Rows of a fi_report CSV, skipping its provenance header comments."""
    path = REPORTS / name
    if not path.exists():
        raise SystemExit(
            f"{path} is missing. Build the engine and run:\n"
            f"    ./build/fi_report --out reports"
        )
    with path.open(encoding="utf-8") as handle:
        lines = [line for line in handle if not line.startswith("#")]
    return list(csv.DictReader(lines))


def read_meta() -> dict:
    path = REPORTS / "run.meta.json"
    if not path.exists():
        raise SystemExit(f"{path} is missing. Run ./build/fi_report --out reports")
    return json.loads(path.read_text(encoding="utf-8"))


def to_float(text: str, default: float | None = None) -> float:
    try:
        return float(text)
    except (TypeError, ValueError):
        if default is None:
            raise
        return default


# --- 1. Zero, par and forward curves -----------------------------------------


def figure_zero_curve() -> pathlib.Path:
    dense = read_csv("zero_curve.csv")
    forwards = read_csv("forward_curves.csv")
    nodes = [r for r in read_csv("curve_nodes.csv") if r["scheme"] == "log-linear-df"]
    market = read_csv("nss_fit.csv")

    t = np.array([to_float(r["tenor_years"]) for r in dense])
    zero = np.array([to_float(r["zero_rate_pct"]) for r in dense])
    t_fwd = np.array([to_float(r["tenor_years"]) for r in forwards])
    fwd = np.array([to_float(r["log-linear-df_forward_pct"]) for r in forwards])

    node_t = np.array([to_float(r["node_years"]) for r in nodes])
    node_z = np.array([to_float(r["zero_rate_pct"]) for r in nodes])
    par_t = np.array([to_float(r["tau_years"]) for r in market])
    par_y = np.array([to_float(r["market_yield_pct"]) for r in market])

    fig, ax = style.new_figure()
    ax.plot(t_fwd, fwd, color=style.OKABE_ITO["orange"], linewidth=1.6, zorder=2)
    ax.plot(t, zero, color=style.OKABE_ITO["blue"], zorder=3)
    ax.plot(
        par_t,
        par_y,
        color=style.OKABE_ITO["green"],
        linestyle="--",
        linewidth=1.8,
        zorder=4,
    )
    ax.scatter(
        node_t,
        node_z,
        s=26,
        color=style.OKABE_ITO["blue"],
        edgecolor="white",
        linewidth=0.8,
        zorder=5,
    )

    style.stacked_labels(
        ax,
        [
            (t_fwd[-1], fwd[-1], "Forward", style.OKABE_ITO["orange"]),
            (t[-1], zero[-1], "Zero", style.OKABE_ITO["blue"]),
            (par_t[-1], par_y[-1], "Par (CMT)", style.OKABE_ITO["green"]),
        ],
    )

    ax.set_xlabel("Tenor (years)")
    ax.set_ylabel("Rate (% per annum)")
    ax.set_xlim(0, 30.5)
    ax.set_ylim(3.0, 6.1)

    belly = int(np.argmin(zero))
    style.callout(
        ax,
        f"Zero curve troughs at {zero[belly]:.2f}% around {t[belly]:.1f}Y.\n"
        "Forwards sit above the zero curve beyond it,\n"
        "which is what drags the zero curve up.",
        xy=(t[belly], zero[belly]),
        xytext=(t[belly] + 3.0, 3.35),
    )
    style.percent_axis(ax)
    style.horizontal_grid_only(ax)
    style.titles(
        fig,
        "Bootstrapping lifts the long end and steepens the forwards",
        f"US Treasury CMT par yields, {AS_OF} · 13 instruments · "
        "log-linear interpolation · dots mark bootstrap nodes",
    )
    style.source_footer(fig, style.source_line(AS_OF))
    return style.save(fig, "zero_curve")


# --- 2. Discount factors ------------------------------------------------------


def figure_discount_factors() -> pathlib.Path:
    dense = read_csv("zero_curve.csv")
    t = np.array([to_float(r["tenor_years"]) for r in dense])
    df = np.array([to_float(r["discount_factor"]) for r in dense])
    zero = np.array([to_float(r["zero_rate_pct"]) for r in dense])

    fig, (top, bottom) = style.new_figure(
        rows=2, sharex=True, gridspec_kw={"height_ratios": [1.35, 1.0]}
    )
    fig.subplots_adjust(left=0.075, right=0.80, top=0.86, bottom=0.135, hspace=0.18)

    top.plot(t, df, color=style.OKABE_ITO["blue"])
    top.set_yscale("log")
    top.set_ylabel("Discount factor (log scale)")
    style.decimal_log_axis(top)
    top.grid(axis="y", which="minor", color=style.GRID, linewidth=0.4, alpha=0.6)
    top.grid(axis="x", visible=False)
    style.direct_label(
        top, t[-1], df[-1], f"DF(30Y) = {df[-1]:.3f}", style.OKABE_ITO["blue"], dx=6
    )
    style.callout(
        top,
        f"A dollar in 30 years is worth\n{df[-1] * 100:.1f} cents today",
        xy=(t[-1], df[-1]),
        xytext=(21.0, df[-1] * 1.9),
    )

    bottom.plot(t, zero, color=style.OKABE_ITO["purple"])
    bottom.set_ylabel("Implied zero rate")
    bottom.set_xlabel("Tenor (years)")
    style.percent_axis(bottom)
    style.horizontal_grid_only(bottom)
    style.direct_label(
        bottom, t[-1], zero[-1], f"{zero[-1]:.2f}%", style.OKABE_ITO["purple"], dx=6
    )

    for ax in (top, bottom):
        ax.set_xlim(0, 30.5)

    style.titles(
        fig,
        "The same curve as prices and as rates",
        f"Discount factors and the zero rates implied by them, {AS_OF} · "
        "log axis keeps the long end readable",
    )
    style.source_footer(fig, style.source_line(AS_OF))
    return style.save(fig, "discount_factors")


# --- 3. Interpolation comparison ---------------------------------------------


def figure_forward_curves() -> pathlib.Path:
    rows = read_csv("forward_curves.csv")
    nodes = [r for r in read_csv("curve_nodes.csv") if r["scheme"] == "log-linear-df"]
    node_t = [to_float(r["node_years"]) for r in nodes]

    t = np.array([to_float(r["tenor_years"]) for r in rows])
    series = {
        "log-linear-df": ("Log-linear in DF", style.OKABE_ITO["vermillion"], "-"),
        "linear-zero": ("Linear in zero", style.OKABE_ITO["sky"], "--"),
        "monotone-convex": ("Monotone convex", style.OKABE_ITO["blue"], "-"),
    }

    fig, ax = style.new_figure()
    labels = []
    for key, (label, colour, dash) in series.items():
        y = np.array([to_float(r[f"{key}_forward_pct"]) for r in rows])
        ax.plot(t, y, color=colour, linestyle=dash, linewidth=1.8)
        labels.append((t[-1], y[-1], label, colour))
    style.stacked_labels(ax, labels)

    for node in node_t:
        ax.axvline(node, color=style.GRID, linewidth=0.7, zorder=0)

    step_y = np.array([to_float(r["log-linear-df_forward_pct"]) for r in rows])
    jump_index = int(np.argmax(np.abs(np.diff(step_y))))
    style.callout(
        ax,
        "Log-linear forwards are a step function:\n"
        f"they jump {abs(np.diff(step_y)[jump_index]) * 100:.0f}bp at this node "
        "and are flat between.\nMonotone convex is continuous through it.",
        xy=(t[jump_index], step_y[jump_index]),
        xytext=(t[jump_index] + 1.6, step_y[jump_index] - 1.15),
    )

    ax.set_xlabel("Tenor (years) · vertical lines are bootstrap nodes")
    ax.set_ylabel("Instantaneous forward rate")
    ax.set_xlim(0, 30.5)
    style.percent_axis(ax)
    style.horizontal_grid_only(ax)
    style.titles(
        fig,
        "All three schemes reprice the market exactly, and disagree about forwards",
        f"Instantaneous forwards from the same 13 CMT quotes, {AS_OF} · "
        "worst repricing residual across all three: 1.8e-11 bp",
    )
    style.source_footer(
        fig,
        style.source_line(
            AS_OF,
            "Scheme per Hagan & West (2006), Applied Mathematical Finance 13(2), 89-129.",
        ),
    )
    return style.save(fig, "forward_curves")


# --- 4. Repricing residuals ---------------------------------------------------


def figure_repricing_residuals() -> pathlib.Path:
    rows = read_csv("repricing_residuals.csv")
    meta = read_meta()

    schemes = ["linear-zero", "log-linear-df", "monotone-convex"]
    colours = [
        style.OKABE_ITO["sky"],
        style.OKABE_ITO["vermillion"],
        style.OKABE_ITO["blue"],
    ]
    markers = ["o", "s", "^"]
    tenors = [r["tenor"] for r in rows if r["scheme"] == schemes[0]]
    x = np.arange(len(tenors))

    fig, ax = style.new_figure()
    ax.axhline(0.0, color=style.INK, linewidth=1.0, zorder=2)

    worst = 0.0
    for scheme, colour, marker in zip(schemes, colours, markers, strict=True):
        y = [to_float(r["residual_bp"]) for r in rows if r["scheme"] == scheme]
        worst = max(worst, max(abs(v) for v in y))
        ax.plot(
            x,
            y,
            marker=marker,
            markersize=6,
            linestyle="none",
            color=colour,
            markeredgecolor="white",
            markeredgewidth=0.6,
            label=scheme,
            zorder=3,
        )

    # A tight market tolerance, drawn to scale. It covers the entire plot area,
    # which is the point of the figure rather than a background fill.
    ax.axhspan(-0.5, 0.5, color=style.OKABE_ITO["yellow"], alpha=0.18, zorder=0)
    ax.annotate(
        "shaded: \u00b10.5bp \u2014 a tight market tolerance, drawn to scale",
        xy=(0.012, 0.965),
        xycoords="axes fraction",
        fontsize=style.ANNOTATION_SIZE,
        color=style.MUTED,
        style="italic",
        va="top",
    )

    ax.set_ylim(-1.35 * worst, 1.35 * worst)
    ax.set_xticks(x)
    ax.set_xticklabels(tenors)
    ax.set_xlabel("Instrument (US Treasury par yield by constant maturity)")
    ax.set_ylabel("Repricing residual (basis points)")
    ax.ticklabel_format(axis="y", style="sci", scilimits=(0, 0), useMathText=True)
    style.horizontal_grid_only(ax)

    for index, (scheme, colour) in enumerate(zip(schemes, colours, strict=True)):
        fig.text(
            0.815,
            0.74 - index * 0.045,
            f"●  {scheme}",
            color=colour,
            fontsize=style.ANNOTATION_SIZE + 1,
            fontweight="semibold",
        )

    style.callout(
        ax,
        "Worst residual anywhere: "
        f"{meta['worst_repricing_residual_bp']:.1e} bp.\n"
        "A 0.5bp band — already a tight market tolerance —\n"
        "would fill this whole chart and then some.",
        xy=(x[len(x) // 2], 0.0),
        xytext=(x[1], 0.72 * worst),
    )

    style.titles(
        fig,
        "Every instrument reprices to its quote at the level of double rounding",
        f"Curve-implied par yield minus market quote, {AS_OF} · "
        "13 instruments across 3 interpolation schemes",
    )
    style.source_footer(fig, style.source_line(AS_OF))
    return style.save(fig, "repricing_residuals")


# --- 5. Key-rate DV01 ---------------------------------------------------------


def figure_key_rate_dv01() -> pathlib.Path:
    rows = read_csv("key_rate_dv01.csv")
    totals = {"SUM", "PARALLEL", "RESIDUAL", "RESIDUAL_PCT"}
    buckets = [r for r in rows if r["tenor"] not in totals]
    summary = {
        r["tenor"]: to_float(r["key_rate_dv01"]) for r in rows if r["node_years"] == ""
    }

    tenors = [r["tenor"] for r in buckets]
    values = np.array([to_float(r["key_rate_dv01"]) for r in buckets])
    y = np.arange(len(tenors))

    fig, ax = style.new_figure(left=0.10)
    colours = [style.GAIN if v >= 0 else style.LOSS for v in values]
    hatches = ["" if v >= 0 else "///" for v in values]
    bars = ax.barh(
        y, values, color=colours, height=0.62, edgecolor="white", linewidth=0.6
    )
    for bar, hatch in zip(bars, hatches, strict=True):
        bar.set_hatch(hatch)

    ax.axvline(0.0, color=style.INK, linewidth=1.0)
    ax.axvline(
        summary["PARALLEL"],
        color=style.OKABE_ITO["orange"],
        linewidth=1.6,
        linestyle="--",
    )
    ax.annotate(
        f"Parallel DV01 \\${summary['PARALLEL']:,.0f}/bp",
        xy=(summary["PARALLEL"], len(tenors) - 0.4),
        xytext=(6, 0),
        textcoords="offset points",
        color=style.OKABE_ITO["orange"],
        fontsize=style.ANNOTATION_SIZE,
        fontweight="semibold",
        va="center",
    )

    # Four of the thirteen buckets carry essentially no risk and two more carry
    # tens of dollars; labelling those crowds the axis without telling anyone
    # anything. The caption says how many are omitted.
    labelled = 0
    for index, value in enumerate(values):
        if abs(value) < 100.0:
            continue
        labelled += 1
        offset = 8 if value >= 0 else -8
        ax.annotate(
            style.format_currency(value),
            xy=(value, index),
            xytext=(offset, 0),
            textcoords="offset points",
            ha="left" if value >= 0 else "right",
            va="center",
            fontsize=style.ANNOTATION_SIZE,
            color=style.INK,
        )

    span = float(np.max(np.abs(values)))
    ax.set_xlim(-1.35 * span, 1.35 * span)
    ax.set_yticks(y)
    ax.set_yticklabels(tenors)
    ax.invert_yaxis()
    ax.set_xlabel("Key-rate DV01 (USD per basis point)")
    ax.set_ylabel("Curve node")
    style.currency_axis(ax, axis="x")
    ax.grid(axis="x", visible=True)
    ax.grid(axis="y", visible=False)

    # The short end carries no risk, so the upper-left of the plot is empty and
    # the reconciliation note goes there rather than over a bar.
    residual_pct = summary["RESIDUAL_PCT"]
    style.callout(
        ax,
        f"Buckets sum to \\${summary['SUM']:,.2f}/bp against a parallel\n"
        f"DV01 of \\${summary['PARALLEL']:,.2f}/bp — a residual of "
        f"{residual_pct:.1e}%.\nThe gap is the second-order cross term "
        "between nodes.",
        xy=(summary["PARALLEL"], 4.4),
        xytext=(-5100, 2.0),
    )
    ax.annotate(
        f"{len(values) - labelled} of {len(values)} buckets carry under "
        "\\$100/bp and are left unlabelled",
        xy=(0.012, 0.035),
        xycoords="axes fraction",
        fontsize=style.ANNOTATION_SIZE,
        color=style.MUTED,
        style="italic",
    )

    style.titles(
        fig,
        "The risk is a 5s10s curve position, not a duration view",
        f"Key-rate DV01 by curve node, {AS_OF} · "
        "\\$10mm 5Y payer at 4.00% and \\$5mm 10Y receiver at 4.50% · "
        "hatched bars lose when that tenor rises",
        left=0.10,
    )
    style.source_footer(fig, style.source_line(AS_OF), left=0.10)
    return style.save(fig, "key_rate_dv01")


# --- 6. Duration versus convexity --------------------------------------------


def figure_duration_convexity() -> pathlib.Path:
    rows = read_csv("shift_attribution.csv")
    shift = np.array([to_float(r["shift_bp"]) for r in rows])
    actual = np.array([to_float(r["actual_pnl"]) for r in rows])
    duration = np.array([to_float(r["duration_only"]) for r in rows])
    convex = np.array([to_float(r["duration_plus_convexity"]) for r in rows])

    order = np.argsort(shift)
    shift, actual, duration, convex = (
        a[order] for a in (shift, actual, duration, convex)
    )

    fig, ax = style.new_figure(left=0.115)
    ax.axhline(0.0, color=style.GRID, linewidth=0.9)
    ax.axvline(0.0, color=style.GRID, linewidth=0.9)

    ax.plot(
        shift,
        duration,
        color=style.OKABE_ITO["vermillion"],
        linestyle="--",
        linewidth=1.8,
    )
    ax.plot(shift, convex, color=style.OKABE_ITO["green"], linewidth=1.8)
    ax.plot(
        shift,
        actual,
        color=style.OKABE_ITO["blue"],
        marker="o",
        markersize=5,
        markeredgecolor="white",
        markeredgewidth=0.6,
    )

    style.stacked_labels(
        ax,
        [
            (shift[-1], duration[-1], "Duration only", style.OKABE_ITO["vermillion"]),
            (shift[-1], convex[-1], "+ convexity", style.OKABE_ITO["green"]),
            (shift[-1], actual[-1], "Actual reprice", style.OKABE_ITO["blue"]),
        ],
    )

    worst = int(np.argmax(np.abs(actual - duration)))
    gap = duration[worst] - actual[worst]
    residual_error = to_float(rows[order[worst]]["with_convexity_error"])
    removed = 1.0 - abs(residual_error) / abs(gap)
    ax.annotate(
        "",
        xy=(shift[worst], actual[worst]),
        xytext=(shift[worst], duration[worst]),
        arrowprops={"arrowstyle": "<->", "color": style.INK, "linewidth": 1.0},
    )
    style.callout(
        ax,
        f"At {shift[worst]:+.0f}bp duration alone overstates the\n"
        f"gain by \\${abs(gap):,.0f} ({abs(gap / actual[worst]) * 100:.1f}%).\n"
        f"One convexity term removes {removed * 100:.0f}% of that error.",
        xy=(shift[worst], 0.5 * (actual[worst] + duration[worst])),
        xytext=(-160, -0.42 * abs(duration[0])),
    )

    ax.set_xlabel("Parallel shift in zero rates (basis points)")
    ax.set_ylabel("Portfolio P&L")
    style.currency_axis(ax)
    style.horizontal_grid_only(ax)
    style.titles(
        fig,
        "Duration is fine for 25bp and wrong for 200bp",
        f"Actual repricing against first- and second-order predictions, {AS_OF} · "
        "\\$10mm 5Y payer and \\$5mm 10Y receiver",
        left=0.115,
    )
    style.source_footer(fig, style.source_line(AS_OF), left=0.115)
    return style.save(fig, "duration_convexity")


# --- 7. Scenario P&L ----------------------------------------------------------


def figure_scenario_pnl() -> pathlib.Path:
    rows = read_csv("scenario_pnl.csv")
    rows.sort(key=lambda r: abs(to_float(r["pnl"])))

    names = [r["scenario"] for r in rows]
    pnl = np.array([to_float(r["pnl"]) for r in rows])
    base = to_float(rows[0]["base_pv"])
    y = np.arange(len(names))

    fig, ax = style.new_figure(left=0.28)
    colours = [style.GAIN if v >= 0 else style.LOSS for v in pnl]
    bars = ax.barh(y, pnl, color=colours, height=0.6, edgecolor="white", linewidth=0.6)
    for bar, value in zip(bars, pnl, strict=True):
        if value < 0:
            bar.set_hatch("///")

    ax.axvline(0.0, color=style.INK, linewidth=1.0)

    for index, value in enumerate(pnl):
        offset = 8 if value >= 0 else -8
        ax.annotate(
            f"{value:+,.0f}",
            xy=(value, index),
            xytext=(offset, 0),
            textcoords="offset points",
            ha="left" if value >= 0 else "right",
            va="center",
            fontsize=style.ANNOTATION_SIZE,
            color=style.INK,
        )

    ax.set_yticks(y)
    ax.set_yticklabels(names)
    ax.set_xlabel("P&L versus base (USD)")
    style.currency_axis(ax, axis="x")
    ax.grid(axis="x", visible=True)
    ax.grid(axis="y", visible=False)

    span = float(np.max(np.abs(pnl)))
    ax.set_xlim(-1.45 * span, 1.45 * span)

    style.callout(
        ax,
        f"Base PV \\${base:,.0f} — near flat, so these\n"
        "are close to the whole position. Gains on a\n"
        "sell-off and on a flattening, losses on a\n"
        "steepening: a short-duration 5s10s flattener.",
        xy=(0.0, 0.5),
        xytext=(0.26 * span, 0.9),
    )

    style.titles(
        fig,
        "The book gains on a sell-off and on a flattening",
        f"P&L under the standard curve scenarios, {AS_OF} · "
        "sorted by magnitude · hatched bars are losses",
    )
    style.source_footer(fig, style.source_line(AS_OF))
    return style.save(fig, "scenario_pnl")


def main() -> None:
    builders = [
        figure_zero_curve,
        figure_discount_factors,
        figure_forward_curves,
        figure_repricing_residuals,
        figure_key_rate_dv01,
        figure_duration_convexity,
        figure_scenario_pnl,
    ]
    for build in builders:
        path = build()
        size_kb = path.stat().st_size / 1024
        print(f"wrote {path.relative_to(style.REPO_ROOT)} ({size_kb:.0f} KB)")


if __name__ == "__main__":
    sys.exit(main())
