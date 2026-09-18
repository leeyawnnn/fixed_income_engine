"""Shared figure style for every chart in this repository.

One module so the figures read as one set rather than seven. It fixes the
canvas, the type scale, the palette and the provenance footer; `make_figures.py`
only decides what to draw.

Choices worth stating:

*SVG only.* Every figure here is lines, bars and text. A raster would be larger
and would blur on a high-density screen for no benefit. PNG earns its place for
dense scatters and heatmaps, and there are none here.

*Okabe-Ito.* A qualitative palette that stays distinguishable under the common
forms of colour blindness. Nothing in these figures is encoded by colour alone:
series carry direct labels and bars carry hatching where the sign matters.

*Direct labels, not legends.* With six or fewer series a legend makes the reader
bounce between the key and the line. The label goes at the end of the line.

*Deterministic output.* Fixed element ids and no date in the SVG metadata, so
regenerating on an unchanged input produces a byte-identical file and CI can
fail when a committed figure no longer matches its data.
"""

from __future__ import annotations

import pathlib
import textwrap

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
REPORTS = REPO_ROOT / "reports"
FIGURES = REPORTS / "figures"

# GitHub renders README images at roughly 880px, so anything wider is
# downsampled for nothing. 1200x750 is the widest size that still looks
# deliberate at that width.
FIG_WIDTH_IN = 12.0
FIG_HEIGHT_IN = 7.5
DPI = 100

# Okabe-Ito, minus the pure black, which is reserved for reference lines.
OKABE_ITO = {
    "orange": "#E69F00",
    "sky": "#56B4E9",
    "green": "#009E73",
    "yellow": "#F0E442",
    "blue": "#0072B2",
    "vermillion": "#D55E00",
    "purple": "#CC79A7",
}
INK = "#1A1A1A"
MUTED = "#6B6B6B"
GRID = "#D9D9D9"

# Sign convention used wherever a number can be a gain or a loss.
GAIN = OKABE_ITO["blue"]
LOSS = OKABE_ITO["vermillion"]

TITLE_SIZE = 15
SUBTITLE_SIZE = 11
LABEL_SIZE = 12
TICK_SIZE = 10
ANNOTATION_SIZE = 9
SOURCE_SIZE = 8

# One family, and deliberately the one matplotlib bundles.
#
# A nicer stack -- Helvetica Neue, then Helvetica, then Arial -- resolves to
# Helvetica Neue on macOS and to DejaVu Sans on a Linux CI runner, and because
# `svg.fonttype: none` writes text as text rather than outlines, the two
# machines lay it out with different metrics and produce different SVGs. CI
# checks that the committed figures regenerate byte for byte, so that check
# would fail for a reason that has nothing to do with the data. DejaVu Sans
# ships with matplotlib and therefore resolves identically everywhere.
FONT_STACK = ["DejaVu Sans"]


def apply_rcparams() -> None:
    plt.rcParams.update(
        {
            "figure.figsize": (FIG_WIDTH_IN, FIG_HEIGHT_IN),
            "figure.dpi": DPI,
            "figure.facecolor": "white",
            "savefig.facecolor": "white",
            "font.family": "sans-serif",
            "font.sans-serif": FONT_STACK,
            "font.size": TICK_SIZE,
            "axes.facecolor": "white",
            "axes.edgecolor": MUTED,
            "axes.labelcolor": INK,
            "axes.labelsize": LABEL_SIZE,
            "axes.titlesize": TITLE_SIZE,
            "axes.spines.top": False,
            "axes.spines.right": False,
            "axes.grid": True,
            "axes.axisbelow": True,
            "grid.color": GRID,
            "grid.linewidth": 0.7,
            "xtick.color": INK,
            "ytick.color": INK,
            "xtick.labelsize": TICK_SIZE,
            "ytick.labelsize": TICK_SIZE,
            "xtick.direction": "out",
            "ytick.direction": "out",
            "lines.linewidth": 2.0,
            "lines.solid_capstyle": "round",
            "legend.frameon": False,
            "svg.fonttype": "none",
            # Fixed salt keeps generated element ids stable between runs.
            "svg.hashsalt": "fixed_income_engine",
        }
    )


LEFT = 0.075
RIGHT = 0.80
TOP = 0.86
BOTTOM = 0.145


def new_figure(rows: int = 1, cols: int = 1, *, left: float = LEFT, **kwargs):
    """A figure with the standard canvas and consistent margins.

    Margins are set explicitly rather than left to bbox_inches='tight' so that
    successive figures line up when stacked in a README instead of each one
    cropping to its own content. `left` widens only for horizontal bar charts
    whose category names need the room.
    """
    apply_rcparams()
    fig, axes = plt.subplots(rows, cols, **kwargs)
    fig.subplots_adjust(left=left, right=RIGHT, top=TOP, bottom=BOTTOM)
    return fig, axes


SUBTITLE_WRAP = 104


def titles(fig, title: str, subtitle: str, *, left: float = LEFT) -> None:
    """Title states the finding; subtitle carries sample, period and units.

    The subtitle wraps for the same reason the source footer does: it usually
    carries the position and conventions, which do not fit one line in a font
    this wide.
    """
    fig.text(left, 0.955, title, fontsize=TITLE_SIZE, fontweight="semibold", color=INK)
    for index, line in enumerate(textwrap.wrap(subtitle, SUBTITLE_WRAP)):
        fig.text(left, 0.917 - index * 0.026, line, fontsize=SUBTITLE_SIZE, color=MUTED)


# DejaVu Sans is wide, and a one-line provenance footer runs off the canvas.
# Wrapping at a fixed character count keeps it inside the figure without having
# to measure text.
SOURCE_WRAP = 118


def source_footer(fig, source: str, *, left: float = LEFT) -> None:
    lines = textwrap.wrap(source, SOURCE_WRAP)
    for index, line in enumerate(reversed(lines)):
        fig.text(
            left,
            0.020 + index * 0.020,
            line,
            fontsize=SOURCE_SIZE,
            style="italic",
            color=MUTED,
        )


def horizontal_grid_only(ax) -> None:
    ax.grid(axis="y", visible=True)
    ax.grid(axis="x", visible=False)


def percent_axis(ax, axis: str = "y", places: int = 1) -> None:
    """Format an axis as 12.3%, never 0.123."""
    formatter = FuncFormatter(lambda v, _: f"{v:.{places}f}%")
    (ax.yaxis if axis == "y" else ax.xaxis).set_major_formatter(formatter)


def format_currency(value: float) -> str:
    """-$3,000 rather than $-3,000, which is how money is actually written."""
    sign = "-" if value < 0 else ""
    # The dollar sign is escaped because matplotlib reads $...$ as mathtext.
    return f"{sign}\\${abs(value):,.0f}"


def currency_axis(ax, axis: str = "y") -> None:
    """Format an axis as USD with thousands separators."""
    formatter = FuncFormatter(lambda v, _: format_currency(v))
    (ax.yaxis if axis == "y" else ax.xaxis).set_major_formatter(formatter)


def decimal_log_axis(ax, axis: str = "y", places: int = 2) -> None:
    """Plain decimals on a log axis: 0.25 reads better than 3x10^-1 for a DF."""
    formatter = FuncFormatter(lambda v, _: f"{v:.{places}f}")
    target = ax.yaxis if axis == "y" else ax.xaxis
    target.set_major_formatter(formatter)
    target.set_minor_formatter(formatter)


def direct_label(ax, x, y, text, color, *, dx=0.01, va="center", size=None) -> None:
    """Label a series at its end instead of adding a legend entry."""
    ax.annotate(
        text,
        xy=(x, y),
        xytext=(dx, 0),
        textcoords="offset points",
        color=color,
        fontsize=size or ANNOTATION_SIZE + 1,
        fontweight="semibold",
        va=va,
        ha="left",
        annotation_clip=False,
    )


def stacked_labels(ax, items, *, dx: int = 6, min_gap_px: float = 13.0) -> None:
    """Direct-label several series at their right-hand ends without collisions.

    `items` is a sequence of (x, y, text, colour). Labels are pushed apart in
    display space until each clears the next, so two series that end a basis
    point apart still get readable labels.
    """
    # Axis limits are only settled at draw time, and the data transform depends
    # on them, so force a draw before measuring where the labels would land.
    ax.figure.canvas.draw()

    ordered = sorted(items, key=lambda item: item[1])
    positions = [ax.transData.transform((item[0], item[1]))[1] for item in ordered]
    for index in range(1, len(positions)):
        positions[index] = max(positions[index], positions[index - 1] + min_gap_px)

    for (x, y, text, colour), display_y in zip(ordered, positions, strict=True):
        anchor_y = ax.transData.transform((x, y))[1]
        ax.annotate(
            text,
            xy=(x, y),
            xytext=(dx, display_y - anchor_y),
            textcoords="offset points",
            color=colour,
            fontsize=ANNOTATION_SIZE + 1,
            fontweight="semibold",
            va="center",
            ha="left",
            annotation_clip=False,
        )


def callout(ax, text, xy, xytext, *, color=INK) -> None:
    """A reader's-takeaway note with a leader line."""
    ax.annotate(
        text,
        xy=xy,
        xytext=xytext,
        fontsize=ANNOTATION_SIZE,
        color=color,
        ha="left",
        va="center",
        arrowprops={
            "arrowstyle": "-",
            "color": MUTED,
            "linewidth": 0.9,
            "shrinkA": 2,
            "shrinkB": 4,
        },
        bbox={
            "boxstyle": "round,pad=0.35",
            "facecolor": "white",
            "edgecolor": GRID,
            "linewidth": 0.7,
        },
    )


def save(fig, name: str) -> pathlib.Path:
    """Write the figure as deterministic SVG and close it."""
    FIGURES.mkdir(parents=True, exist_ok=True)
    path = FIGURES / f"{name}.svg"
    # metadata Date=None suppresses matplotlib's <dc:date>, which would
    # otherwise change on every run and make the committed file churn.
    fig.savefig(path, format="svg", metadata={"Date": None})
    plt.close(fig)
    return path


def source_line(as_of: str, extra: str = "") -> str:
    tail = f" {extra}" if extra else ""
    return (
        f"Source: US Treasury par yield curve rates (CMT), {as_of}, from "
        f"data/treasury_par_yields_2025.csv. Computed by fi_report.{tail}"
    )
