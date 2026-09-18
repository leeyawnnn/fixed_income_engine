#!/usr/bin/env python3
"""Generate annotated SVG figures for the fixed-income-engine worked example.

Pure standard library. Five figures:
  1 yield_curve       — bootstrapped zeros + NSS fit, belly/front/long callouts
  2 discount_factors  — DF(t), with the 30Y value called out
  3 forward_curve     — implied instantaneous forwards vs the spot/zero curve
  4 key_rate_dv01     — portfolio interest-rate risk by curve tenor ($/bp)
  5 scenario_pnl      — P&L by scenario, best/worst highlighted

Forwards and key-rate DV01 are computed here from the same curve/portfolio the
C++ engine uses (single-curve, self-discounted swaps), so the figures match the
demo. Run:  python3 tools/make_figures.py
"""
import datetime
import math
import os

OUT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "reports", "figures"))
os.makedirs(OUT, exist_ok=True)

# --- Curve from the verified curve_demo run (valuation 2024-01-02) -----------
REF = datetime.date(2024, 1, 2)
NT = [0.498630, 1.002740, 2.002740, 3.002740, 5.005479, 7.005479, 10.008219, 30.021918]
NZ = [0.053029, 0.051667, 0.050250, 0.049060, 0.047724, 0.047451, 0.047841, 0.049185]
TENORS = [0.5, 1, 2, 3, 5, 7, 10, 30]
NSS = (0.002828, 0.051346, -0.0, 0.132952, 3.915507, 17.218562)


def nss_yield(t, p=NSS):
    b0, b1, b2, b3, l1, l2 = p
    x1, x2 = t / l1, t / l2
    f1 = (1 - math.exp(-x1)) / x1
    f2 = f1 - math.exp(-x1)
    f3 = (1 - math.exp(-x2)) / x2 - math.exp(-x2)
    return b0 + b1 * f1 + b2 * f2 + b3 * f3


def disc(t, zs):
    """Log-linear discount factor (matches LogLinearCurve)."""
    if t <= 0:
        return 1.0
    ld = [-zs[i] * NT[i] for i in range(len(zs))]
    if t <= NT[0]:
        return math.exp(-zs[0] * t)
    if t >= NT[-1]:
        return math.exp(-zs[-1] * t)
    for i in range(1, len(NT)):
        if t <= NT[i]:
            w = (t - NT[i - 1]) / (NT[i] - NT[i - 1])
            return math.exp(ld[i - 1] + w * (ld[i] - ld[i - 1]))
    return math.exp(ld[-1])


# --- Implied instantaneous forwards (piecewise constant, log-linear curve) ----
def forward_segments(zs):
    segs = []
    prev_t, prev_ztt = 0.0, 0.0
    for i in range(len(NT)):
        ztt = zs[i] * NT[i]  # = -ln DF
        f = (ztt - prev_ztt) / (NT[i] - prev_t)
        segs.append((prev_t, NT[i], f))
        prev_t, prev_ztt = NT[i], ztt
    return segs


# --- Portfolio (single-curve self-discounted swaps) --------------------------
def add_months(d, m):
    y = d.year + (d.month - 1 + m) // 12
    mo = (d.month - 1 + m) % 12 + 1
    return datetime.date(y, mo, d.day)


def schedule(start, maturity, months):
    out, k = [], 0
    while True:
        d = add_months(maturity, -months * k)
        if d <= start:
            break
        out.append(d)
        k += 1
    return sorted(out)


def yf_act365(d):
    return (d - REF).days / 365.0


def yf_30360(d1, d2):
    dd1, dd2 = min(d1.day, 30), d2.day
    if dd2 == 31 and dd1 == 30:
        dd2 = 30
    return (360 * (d2.year - d1.year) + 30 * (d2.month - d1.month) + (dd2 - dd1)) / 360.0


def swap_pv(notional, rate, direction, maturity, zs):
    ann, prev = 0.0, REF
    for d in schedule(REF, maturity, 6):  # semi-annual fixed, 30/360
        ann += yf_30360(prev, d) * disc(yf_act365(d), zs)
        prev = d
    fixed = notional * rate * ann
    floating = notional * (disc(0.0, zs) - disc(yf_act365(maturity), zs))
    return (floating - fixed) if direction == "payer" else (fixed - floating)


PORTFOLIO = [
    (10e6, 0.05, "payer", datetime.date(2029, 1, 2)),
    (5e6, 0.045, "receiver", datetime.date(2034, 1, 2)),
]


def book_pv(zs):
    return sum(swap_pv(*s, zs) for s in PORTFOLIO)


def key_rate_dv01(bump=1e-4):
    krd = []
    for i in range(len(NZ)):
        zu, zd = NZ[:], NZ[:]
        zu[i] += bump
        zd[i] -= bump
        krd.append((book_pv(zu) - book_pv(zd)) / 2.0)
    return krd


def parallel_dv01(bump=1e-4):
    up = [z + bump for z in NZ]
    dn = [z - bump for z in NZ]
    return (book_pv(up) - book_pv(dn)) / 2.0


SCEN = [
    ("Parallel +25bp", 14583.13),
    ("Parallel +100bp", 63556.95),
    ("Parallel -25bp", -13650.14),
    ("Steepener -/+25bp", -39242.12),
    ("Flattener +/-25bp", 38933.18),
    ("Butterfly +25/-12.5bp", -62067.17),
]

# ----------------------------------------------------------------------------
W, H = 840, 760
ML, MR, MT, MB = 86, 46, 84, 84
PW, PH = W - ML - MR, H - MT - MB
FONT = "-apple-system,Segoe UI,Helvetica,Arial,sans-serif"
BLUE, RED, GREEN, GREY, AMBER = "#1f77b4", "#d62728", "#2ca02c", "#666", "#ff7f0e"


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def panel(title, subtitle):
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" font-family="{FONT}">',
        f'<rect width="{W}" height="{H}" fill="white"/>',
        f'<text x="{ML}" y="34" font-size="20" font-weight="700" fill="#111">{esc(title)}</text>',
        f'<text x="{ML}" y="56" font-size="13" fill="{GREY}">{esc(subtitle)}</text>',
    ]


def callout(s, px, py, text, dx, dy, anchor="start"):
    """A small leader line from (px,py) to a text label offset by (dx,dy)."""
    tx, ty = px + dx, py + dy
    s.append(f'<line x1="{px:.1f}" y1="{py:.1f}" x2="{tx:.1f}" y2="{ty:.1f}" '
             f'stroke="#888" stroke-width="1" stroke-dasharray="2,2"/>')
    s.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="3" fill="#111"/>')
    lines = text.split("\n")
    bw = 7.2 * max(len(ln) for ln in lines) + 12
    bh = 15 * len(lines) + 8
    bx = tx if anchor == "start" else tx - bw
    s.append(f'<rect x="{bx:.1f}" y="{ty - 13:.1f}" width="{bw:.1f}" height="{bh:.1f}" '
             f'rx="4" fill="#fffbe6" stroke="#e0c200"/>')
    for i, ln in enumerate(lines):
        s.append(f'<text x="{bx + 6:.1f}" y="{ty - 1 + i * 15:.1f}" font-size="11.5" '
                 f'fill="#5a4a00">{esc(ln)}</text>')


def x_axis(s, xm, xmin, xmax, ticks, label):
    for xt in ticks:
        px = xm(xt)
        s.append(f'<line x1="{px:.1f}" y1="{MT + PH}" x2="{px:.1f}" y2="{MT + PH + 5}" stroke="#333"/>')
        s.append(f'<text x="{px:.1f}" y="{MT + PH + 20}" text-anchor="middle" font-size="11" fill="#555">{xt}</text>')
    s.append(f'<text x="{ML + PW / 2}" y="{H - 18}" text-anchor="middle" font-size="12.5" fill="#333">{esc(label)}</text>')


def y_axis(s, ym, ymin, ymax, n, fmt, label):
    for i in range(n + 1):
        v = ymin + (ymax - ymin) * i / n
        py = ym(v)
        s.append(f'<line x1="{ML}" y1="{py:.1f}" x2="{ML + PW}" y2="{py:.1f}" stroke="#eee"/>')
        s.append(f'<text x="{ML - 8}" y="{py + 4:.1f}" text-anchor="end" font-size="11" fill="#555">{fmt(v)}</text>')
    s.append(f'<line x1="{ML}" y1="{MT}" x2="{ML}" y2="{MT + PH}" stroke="#333"/>')
    s.append(f'<line x1="{ML}" y1="{MT + PH}" x2="{ML + PW}" y2="{MT + PH}" stroke="#333"/>')
    s.append(f'<text x="24" y="{MT + PH / 2}" text-anchor="middle" font-size="12.5" fill="#333" '
             f'transform="rotate(-90 24 {MT + PH / 2})">{esc(label)}</text>')


def save(name, s):
    s.append("</svg>")
    with open(os.path.join(OUT, name), "w") as f:
        f.write("\n".join(s))


# === Figure 1 — yield curve + NSS ============================================
def fig_yield():
    xmin, xmax, ymin, ymax = 0, 31, 4.5, 5.4
    xm = lambda x: ML + (x - xmin) / (xmax - xmin) * PW
    ym = lambda y: MT + (ymax - y) / (ymax - ymin) * PH
    s = panel("Bootstrapped zero curve & NSS fit",
              "Continuously-compounded zero rates implied by a 6M deposit + 1Y-30Y par swaps  -  NSS RMSE 0.81 bp")
    # belly shaded band
    s.append(f'<rect x="{xm(5):.1f}" y="{MT}" width="{xm(7) - xm(5):.1f}" height="{PH}" fill="#1f77b4" opacity="0.06"/>')
    y_axis(s, ym, ymin, ymax, 6, lambda v: f"{v:.1f}", "Zero rate (%)")
    x_axis(s, xm, xmin, xmax, [0, 5, 10, 15, 20, 25, 30], "Tenor (years)")
    grid = [i / 4 for i in range(2, 121)]
    s.append('<polyline points="' + " ".join(f"{xm(t):.1f},{ym(nss_yield(t)*100):.1f}" for t in grid) +
             f'" fill="none" stroke="{BLUE}" stroke-width="2.4"/>')
    for t, z in zip(TENORS, NZ):
        s.append(f'<circle cx="{xm(t):.1f}" cy="{ym(z*100):.1f}" r="4" fill="{RED}" stroke="white"/>')
    callout(s, xm(0.5), ym(5.303), "Front: 6M = 5.30%\n(cash / policy rate)", 26, -6)
    callout(s, xm(6), ym(4.745), "Belly: 5-7Y is the\nlowest point (~4.75%)", -40, 70, "start")
    callout(s, xm(30), ym(4.919), "Long end: 30Y = 4.92%", -150, 26, "start")
    # legend
    s.append(f'<rect x="{ML + PW - 168}" y="{MT + 6}" width="13" height="13" fill="{BLUE}"/>')
    s.append(f'<text x="{ML + PW - 150}" y="{MT + 17}" font-size="11.5">NSS fitted curve</text>')
    s.append(f'<circle cx="{ML + PW - 161}" cy="{MT + 30}" r="4" fill="{RED}"/>')
    s.append(f'<text x="{ML + PW - 150}" y="{MT + 34}" font-size="11.5">bootstrapped nodes</text>')
    save("yield_curve.svg", s)


# === Figure 2 — discount factors =============================================
def fig_discount():
    xmin, xmax, ymin, ymax = 0, 31, 0, 1
    xm = lambda x: ML + (x - xmin) / (xmax - xmin) * PW
    ym = lambda y: MT + (ymax - y) / (ymax - ymin) * PH
    s = panel("Discount factor curve",
              "Present value today of $1 received at each tenor  -  DF(t) = exp(-z*t)")
    y_axis(s, ym, ymin, ymax, 5, lambda v: f"{v:.2f}", "Discount factor")
    x_axis(s, xm, xmin, xmax, [0, 5, 10, 15, 20, 25, 30], "Tenor (years)")
    pts = [(0.0, 1.0)] + [(t, disc(NT[i], NZ)) for i, t in enumerate(TENORS)]
    s.append('<polyline points="' + " ".join(f"{xm(t):.1f},{ym(d):.1f}" for t, d in pts) +
             f'" fill="none" stroke="{GREEN}" stroke-width="2.4"/>')
    for t, d in pts[1:]:
        s.append(f'<circle cx="{xm(t):.1f}" cy="{ym(d):.1f}" r="4" fill="{GREEN}" stroke="white"/>')
    callout(s, xm(30), ym(0.2284), "$1 paid in 30Y is\nworth $0.23 today", -150, -36, "start")
    callout(s, xm(5), ym(0.7875), "$1 in 5Y -> $0.79", 20, -40)
    save("discount_factors.svg", s)


# === Figure 3 — implied forward curve (NEW) ==================================
def fig_forward():
    segs = forward_segments(NZ)
    xmin, xmax, ymin, ymax = 0, 31, 4.2, 5.5
    xm = lambda x: ML + (x - xmin) / (xmax - xmin) * PW
    ym = lambda y: MT + (ymax - y) / (ymax - ymin) * PH
    s = panel("Implied forward-rate curve (what the market expects rates to do)",
              "Instantaneous forward rates implied by the curve, vs the spot zero curve")
    y_axis(s, ym, ymin, ymax, 6, lambda v: f"{v:.1f}", "Rate (%)")
    x_axis(s, xm, xmin, xmax, [0, 5, 10, 15, 20, 25, 30], "Tenor (years)")
    # zero curve (reference)
    grid = [i / 4 for i in range(2, 121)]
    s.append('<polyline points="' + " ".join(f"{xm(t):.1f},{ym(nss_yield(t)*100):.1f}" for t in grid) +
             f'" fill="none" stroke="{GREY}" stroke-width="1.8" stroke-dasharray="5,4"/>')
    # forward step line
    fpts = []
    for a, b, f in segs:
        fpts.append((a, f * 100))
        fpts.append((b, f * 100))
    s.append('<polyline points="' + " ".join(f"{xm(t):.1f},{ym(v):.1f}" for t, v in fpts) +
             f'" fill="none" stroke="{AMBER}" stroke-width="2.6"/>')
    lo = min(segs, key=lambda x: x[2])
    callout(s, xm((lo[0] + lo[1]) / 2), ym(lo[2] * 100),
            "Forwards dip BELOW spot ->\nmarket prices near-term cuts", -10, 70)
    callout(s, xm(20), ym(segs[-1][2] * 100),
            "Forwards rise at the long end ->\nexpected normalisation", -250, -34, "start")
    s.append(f'<rect x="{ML + PW - 168}" y="{MT + 6}" width="16" height="4" fill="{AMBER}"/>')
    s.append(f'<text x="{ML + PW - 148}" y="{MT + 12}" font-size="11.5">implied forward</text>')
    s.append(f'<line x1="{ML + PW - 168}" y1="{MT + 28}" x2="{ML + PW - 152}" y2="{MT + 28}" stroke="{GREY}" stroke-width="2" stroke-dasharray="5,4"/>')
    s.append(f'<text x="{ML + PW - 148}" y="{MT + 32}" font-size="11.5">spot zero curve</text>')
    save("forward_curve.svg", s)


# === Figure 4 — key-rate DV01 (NEW) ==========================================
def fig_keyrate():
    krd = key_rate_dv01()
    total = parallel_dv01()
    labels = ["0.5y", "1y", "2y", "3y", "5y", "7y", "10y", "30y"]
    n = len(krd)
    xmin, xmax = 0, n
    vmax = max(abs(v) for v in krd) * 1.35
    xm = lambda i: ML + (i + 0.5) / n * PW
    ym = lambda v: MT + (vmax - v) / (2 * vmax) * PH
    s = panel("Portfolio key-rate DV01 (where the interest-rate risk sits)",
              "P&L per +1bp shift of each curve tenor  -  $10mm 5Y payer + $5mm 10Y receiver")
    # y axis ($/bp), zero line emphasised
    for i in range(5):
        v = -vmax + 2 * vmax * i / 4
        py = ym(v)
        s.append(f'<line x1="{ML}" y1="{py:.1f}" x2="{ML + PW}" y2="{py:.1f}" stroke="#eee"/>')
        s.append(f'<text x="{ML - 8}" y="{py + 4:.1f}" text-anchor="end" font-size="11" fill="#555">{v:,.0f}</text>')
    s.append(f'<line x1="{ML}" y1="{MT}" x2="{ML}" y2="{MT + PH}" stroke="#333"/>')
    zy = ym(0)
    s.append(f'<line x1="{ML}" y1="{zy:.1f}" x2="{ML + PW}" y2="{zy:.1f}" stroke="#333" stroke-width="1.5"/>')
    s.append(f'<text x="24" y="{MT + PH/2}" text-anchor="middle" font-size="12.5" fill="#333" transform="rotate(-90 24 {MT + PH/2})">DV01 ($ per 1bp)</text>')
    bw = PW / n * 0.5
    for i, v in enumerate(krd):
        cx = xm(i)
        top = ym(max(v, 0))
        h = abs(ym(v) - ym(0))
        col = GREEN if v >= 0 else RED
        s.append(f'<rect x="{cx - bw/2:.1f}" y="{top:.1f}" width="{bw:.1f}" height="{h:.1f}" rx="2" fill="{col}" opacity="0.85"/>')
        lv = ym(v) + (-6 if v >= 0 else 14)
        s.append(f'<text x="{cx:.1f}" y="{lv:.1f}" text-anchor="middle" font-size="10.5" fill="#333">{v:,.0f}</text>')
        s.append(f'<text x="{cx:.1f}" y="{MT + PH + 18:.1f}" text-anchor="middle" font-size="11" fill="#555">{labels[i]}</text>')
    callout(s, xm(4), ym(krd[4]), "Long the front\n(the 5Y payer leg)", 12, -54)
    callout(s, xm(7), ym(krd[7]), "Short the back\n(the 10Y receiver)", -150, 36, "start")
    s.append(f'<rect x="{ML + PW - 196}" y="{MT + 4}" width="190" height="26" rx="5" fill="#eef6ee" stroke="{GREEN}"/>')
    s.append(f'<text x="{ML + PW - 186}" y="{MT + 21}" font-size="12.5" fill="#1a5">Net parallel DV01: {total:,.0f} $/bp</text>')
    save("key_rate_dv01.svg", s)


# === Figure 5 — scenario P&L (highlighted) ===================================
def fig_scen():
    LL = 250
    s = panel("Scenario P&L (one-glance portfolio risk)",
              "Change in portfolio value under standard curve moves  -  green = gain, red = loss")
    n = len(SCEN)
    row = PH / n
    vmax = max(abs(v) for _, v in SCEN) * 1.2
    barfull = W - MR - LL
    cx = LL + barfull / 2
    half = barfull / 2 - 10
    xm = lambda v: cx + v / vmax * half
    best = max(range(n), key=lambda i: SCEN[i][1])
    worst = min(range(n), key=lambda i: SCEN[i][1])
    s.append(f'<line x1="{cx}" y1="{MT}" x2="{cx}" y2="{MT + PH}" stroke="#333"/>')
    s.append(f'<text x="{cx}" y="{MT + PH + 18}" text-anchor="middle" font-size="11" fill="#555">0</text>')
    for i, (name, v) in enumerate(SCEN):
        y = MT + i * row + row * 0.2
        h = row * 0.6
        x2 = xm(v)
        x = min(cx, x2)
        w = abs(x2 - cx)
        col = GREEN if v >= 0 else RED
        s.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="2" fill="{col}" opacity="0.88"/>')
        if i in (best, worst):
            s.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="2" fill="none" stroke="#111" stroke-width="1.5"/>')
        s.append(f'<text x="{LL - 12}" y="{y + h/2 + 4:.1f}" text-anchor="end" font-size="12.5">{esc(name)}</text>')
        lx = x2 + (7 if v >= 0 else -7)
        anc = "start" if v >= 0 else "end"
        tag = "  (best)" if i == best else ("  (worst)" if i == worst else "")
        s.append(f'<text x="{lx:.1f}" y="{y + h/2 + 4:.1f}" text-anchor="{anc}" font-size="11.5" fill="#333">{v:+,.0f}{tag}</text>')
    s.append(f'<text x="{cx}" y="{H - 18}" text-anchor="middle" font-size="12.5" fill="#333">P&amp;L (USD)  -  black outline = best / worst case</text>')
    save("scenario_pnl.svg", s)


fig_yield(); fig_discount(); fig_forward(); fig_keyrate(); fig_scen()

# Report the computed numbers (for the README prose).
krd = key_rate_dv01()
print("PV check  swap1=%.2f swap2=%.2f total=%.2f" % (
    swap_pv(*PORTFOLIO[0], NZ), swap_pv(*PORTFOLIO[1], NZ), book_pv(NZ)))
print("parallel DV01 = %.2f $/bp" % parallel_dv01())
print("key-rate DV01 =", ", ".join(f"{l}:{v:,.0f}" for l, v in zip(TENORS, krd)))
print("forwards (%) =", ", ".join(f"{f*100:.3f}" for _, _, f in forward_segments(NZ)))
print("figures written to", OUT)
