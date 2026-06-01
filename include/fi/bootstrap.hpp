#pragma once

#include <memory>
#include <variant>
#include <vector>

#include "fi/bond.hpp"  // Frequency, per_year
#include "fi/curve.hpp"
#include "fi/date.hpp"
#include "fi/day_count.hpp"
#include "fi/solver.hpp"

namespace fi {

// --- Market instrument quotes used to bootstrap the short/middle/long end. ---

// Cash deposit: a single simple-interest money-market rate.
//   DF(maturity) = 1 / (1 + rate · τ),  τ on the deposit's day count.
struct DepositQuote {
    Date maturity;
    double rate;
    DayCount day_count;  // typically Act/360
};

// Interest-rate future / FRA: an implied forward (simple) rate over [start,end].
//   DF(end) = DF(start) / (1 + rate · τ),  τ on the future's day count.
// (A price quote P converts to a rate via (100 − P)/100 by the caller; no
//  convexity adjustment is applied here.)
struct FuturesQuote {
    Date start;
    Date end;
    double rate;
    DayCount day_count;  // typically Act/360
};

// Par interest-rate swap: the fixed rate that makes a spot-starting swap worth
// zero under single-curve (self-discounting) valuation.
struct SwapQuote {
    Date maturity;
    double rate;
    Frequency fixed_frequency;
    DayCount fixed_day_count;  // typically 30/360
};

using BootstrapInstrument = std::variant<DepositQuote, FuturesQuote, SwapQuote>;

// --- Repricing helpers (also used to verify the bootstrap closes) ----------

// Implied simple deposit rate from the curve: (1/DF − 1)/τ.
double implied_deposit_rate(const Curve& curve, const DepositQuote& q);

// Implied simple forward rate from the curve: (DF(start)/DF(end) − 1)/τ.
double implied_futures_rate(const Curve& curve, const FuturesQuote& q);

// Par swap rate from the curve (spot start = curve.reference_date()):
//   (DF(start) − DF(maturity)) / Σ τ_j·DF(t_j).
double par_swap_rate(const Curve& curve, const SwapQuote& q);

// --- The bootstrapper ------------------------------------------------------

// Build a zero curve from market instruments. Instruments are processed in
// maturity order: deposits and futures pin a discount factor directly, swaps
// are solved (safeguarded Newton) so the par rate matches the quote. Returns a
// log-linear (piecewise-constant-forward) curve. Each instrument reprices to
// its quote exactly because nodes are reproduced and the interpolation is local.
std::unique_ptr<Curve> bootstrap_curve(const Date& reference_date,
                                       DayCount curve_day_count,
                                       std::vector<BootstrapInstrument> instruments,
                                       const SolverConfig& cfg = {});

}  // namespace fi
