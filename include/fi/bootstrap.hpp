#pragma once

#include <memory>
#include <string>
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

// Par coupon bond: the coupon that makes a bond trade at exactly 100 at that
// maturity. This is what a Treasury CMT rate is, and it is the same relation as
// a par swap rate -- c·Σ α_j·DF(t_j) + DF(T) = 1 -- which is why bootstrapping
// them uses the same solve. It is kept as its own type because a Treasury and a
// swap are not the same instrument, and a validation table that says "swap 5Y"
// when it means "UST 5Y par yield" is a table nobody can check.
//
// For maturities shorter than one coupon period the schedule collapses to a
// single payment and the relation degenerates to DF = 1/(1 + c·α), i.e. simple
// interest on the bond's own day count.
struct ParBondQuote {
    Date maturity;
    double par_yield;
    Frequency frequency;  // semiannual for US Treasuries
    DayCount day_count;
};

using BootstrapInstrument =
    std::variant<DepositQuote, FuturesQuote, SwapQuote, ParBondQuote>;

// --- Repricing helpers (also used to verify the bootstrap closes) ----------

// Implied simple deposit rate from the curve: (1/DF − 1)/τ.
double implied_deposit_rate(const Curve& curve, const DepositQuote& q);

// Implied simple forward rate from the curve: (DF(start)/DF(end) − 1)/τ.
double implied_futures_rate(const Curve& curve, const FuturesQuote& q);

// Par swap rate from the curve (spot start = curve.reference_date()):
//   (DF(start) - DF(maturity)) / Sum tau_j*DF(t_j).
double par_swap_rate(const Curve& curve, const SwapQuote& q);

// Par yield implied by the curve for a bond maturing at q.maturity:
//   (1 - DF(T)) / Sum alpha_j*DF(t_j).
double implied_par_bond_yield(const Curve& curve, const ParBondQuote& q);

// Repricing residual for any instrument, in the quote's own units (a decimal
// rate, so 1e-4 is one basis point): curve-implied quote minus market quote.
// This is what makes "the bootstrap closes" a measured claim rather than an
// assertion -- for an exactly-bootstrapped instrument it should be at the level
// of double rounding, not merely small.
double repricing_residual(const Curve& curve, const BootstrapInstrument& instrument);

// The quoted rate carried by an instrument, whatever its kind.
double quoted_rate(const BootstrapInstrument& instrument) noexcept;

// The instrument's maturity (the far leg for a futures/FRA).
Date instrument_maturity(const BootstrapInstrument& instrument) noexcept;

// A short label for reports: "Deposit 6M", "UST par 5Y", "Swap 10Y".
std::string instrument_label(const BootstrapInstrument& instrument,
                             const Date& reference_date);

// --- The bootstrapper ------------------------------------------------------

// Build a zero curve from market instruments. Instruments are processed in
// maturity order: deposits and futures pin a discount factor directly, swaps
// are solved (safeguarded Newton) so the par rate matches the quote. Returns a
// log-linear (piecewise-constant-forward) curve. Each instrument reprices to
// its quote exactly because nodes are reproduced and the interpolation is local.
// `scheme` is used for both the trial curves inside the solve and the curve
// returned, which is what makes the repricing exact. Bootstrapping under one
// interpolation and then reading the nodes back under another does not reprice:
// a par instrument's annuity depends on discount factors at its intermediate
// coupon dates, and those are precisely what the schemes disagree about.
std::unique_ptr<Curve> bootstrap_curve(
    const Date& reference_date, DayCount curve_day_count,
    std::vector<BootstrapInstrument> instruments,
    Interpolation scheme = Interpolation::LogLinearDiscount,
    const SolverConfig& cfg = {});

// Overload for callers that want a non-default solver config but the default
// interpolation, so adding the scheme parameter did not break them.
inline std::unique_ptr<Curve> bootstrap_curve(
    const Date& reference_date, DayCount curve_day_count,
    std::vector<BootstrapInstrument> instruments, const SolverConfig& cfg) {
    return bootstrap_curve(reference_date, curve_day_count, std::move(instruments),
                           Interpolation::LogLinearDiscount, cfg);
}

}  // namespace fi
