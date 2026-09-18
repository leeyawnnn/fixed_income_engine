#pragma once

#include <memory>
#include <stdexcept>
#include <vector>

#include "fi/bond.hpp"
#include "fi/curve.hpp"
#include "fi/date.hpp"
#include "fi/swap.hpp"

namespace fi {

// Analytic interest-rate risk measures for a fixed-coupon bond. All are taken
// against the same yield-based discounting Bond::price_from_yield uses,
// P = Σ c·(1 + y/m)^(−m·τ), so they are exact derivatives of that price, not
// of a curve-discounted price:
//
//   Macaulay duration   D_mac = (Σ τ·PV) / P                      (years)
//   Modified duration   D_mod = D_mac / (1 + y/m)                 dP/dy = −D_mod·P
//   Convexity           C     = (1/P) Σ c·τ·(τ + 1/m)·(1+y/m)^(−mτ−2)
//   DV01                = D_mod · P · 1e-4   (price change per 1bp, positive)
struct RiskMeasures {
    double price = 0.0;
    double macaulay_duration = 0.0;
    double modified_duration = 0.0;
    double convexity = 0.0;
    double dv01 = 0.0;
};

// One-pass computation of all measures, valued at `valuation_date`.
RiskMeasures risk_measures(const Bond& bond, double yield, const Date& valuation_date);
inline RiskMeasures risk_measures(const Bond& bond, double yield) {
    return risk_measures(bond, yield, bond.issue_date());
}

// Individual analytic measures (thin wrappers over risk_measures).
double macaulay_duration(const Bond& bond, double yield, const Date& valuation_date);
double modified_duration(const Bond& bond, double yield, const Date& valuation_date);
double convexity(const Bond& bond, double yield, const Date& valuation_date);
double dv01(const Bond& bond, double yield, const Date& valuation_date);

inline double macaulay_duration(const Bond& b, double y) {
    return macaulay_duration(b, y, b.issue_date());
}
inline double modified_duration(const Bond& b, double y) {
    return modified_duration(b, y, b.issue_date());
}
inline double convexity(const Bond& b, double y) {
    return convexity(b, y, b.issue_date());
}
inline double dv01(const Bond& b, double y) {
    return dv01(b, y, b.issue_date());
}

// Central finite-difference DV01 (per 1bp), for cross-checking the analytic
// value. `bump` is the yield perturbation used for the difference.
double dv01_finite_difference(const Bond& bond, double yield,
                              const Date& valuation_date, double bump = 1e-4);
inline double dv01_finite_difference(const Bond& b, double y, double bump = 1e-4) {
    return dv01_finite_difference(b, y, b.issue_date(), bump);
}

// --- Curve sensitivities for swaps (single-curve / self-discounted) ---------

// Parallel DV01: change in swap PV per 1bp parallel shift of the curve's zero
// rates, by central difference: (PV(+bump) − PV(−bump)) / 2.
double swap_dv01(const Swap& swap, const Curve& discount, double bump = 1e-4);

// Key-rate DV01: one entry per curve node, each the swap PV change when only
// that node's zero rate is shifted by 1bp (central difference). Their sum
// reconstructs the parallel DV01 to within third-order curvature.
std::vector<double> swap_key_rate_dv01(const Swap& swap, const Curve& discount,
                                       double bump = 1e-4);

// --- Portfolio-level curve risk ---------------------------------------------

// Sum of swap PVs under one discount curve (single-curve, self-discounted).
double portfolio_pv(const std::vector<Swap>& portfolio, const Curve& discount);

// Parallel DV01 and per-node key-rate DV01 for a whole book.
double portfolio_dv01(const std::vector<Swap>& portfolio, const Curve& discount,
                      double bump = 1e-4);
std::vector<double> portfolio_key_rate_dv01(const std::vector<Swap>& portfolio,
                                            const Curve& discount, double bump = 1e-4);

// Second-order curve sensitivity, scaled so the convexity contribution to a
// parallel shift of n basis points is gamma * n^2 - matching dv01, which is
// already quoted per basis point. Taken as the central second difference
// (PV(+b) - 2*PV + PV(-b)) / 2.
double portfolio_curve_gamma(const std::vector<Swap>& portfolio, const Curve& discount,
                             double bump = 1e-4);

// Key-rate DV01s against the parallel DV01 they are supposed to decompose.
//
// The two do not agree exactly, and the gap is worth reporting rather than
// hiding. A parallel shift moves every node at once, so the curve between any
// two nodes moves too; bumping one node at a time moves the interior only
// through that node's own influence, which the interpolation makes local. What
// is left over is the second-order cross term between nodes. It is small
// because PV is close to linear in the zero rates over 1bp, not because the
// decomposition is exact.
struct KeyRateReconciliation {
    std::vector<double> node_times;
    std::vector<double> key_rate_dv01;
    double sum_of_buckets = 0.0;
    double parallel_dv01 = 0.0;
    double residual = 0.0;           // sum - parallel
    double relative_residual = 0.0;  // residual / parallel, 0 if parallel is 0
};

KeyRateReconciliation reconcile_key_rates(const std::vector<Swap>& portfolio,
                                          const Curve& discount, double bump = 1e-4);

// --- Bucketed hedging --------------------------------------------------------

// Notionals of a set of benchmark swaps that neutralise a book's key-rate
// exposure, and what exposure survives.
//
// This is the step that turns a risk report into a decision: the key-rate
// profile says where the risk is, and this says what to trade. Each hedge
// contributes its own key-rate profile per unit notional, so the problem is
// linear - find w with K*w = -p, where K's columns are the hedges' per-unit
// profiles and p is the book's. With fewer hedges than buckets, which is the
// normal case, there is no exact solution and this returns the least-squares
// one: the smallest residual exposure achievable with the instruments offered.
struct BucketHedge {
    std::vector<double> notionals;     // one per hedge instrument
    std::vector<double> residual_krd;  // key-rate DV01 left after hedging
    double residual_dv01 = 0.0;        // parallel DV01 left after hedging
    double worst_bucket_before = 0.0;  // largest |key-rate DV01| unhedged
    double worst_bucket_after = 0.0;
};

// `hedges` are specified with unit notional; the returned notionals are the
// multipliers to apply. Throws std::invalid_argument if `hedges` is empty.
BucketHedge solve_bucket_hedge(const std::vector<Swap>& portfolio,
                               const std::vector<Swap>& hedges, const Curve& discount,
                               double bump = 1e-4);

// --- Duration versus reality -------------------------------------------------

// One row of the classic demonstration that duration alone is not enough.
struct ShiftAttribution {
    double shift_bp = 0.0;
    double actual_pnl = 0.0;
    double duration_only = 0.0;            // dv01 * shift_bp
    double duration_plus_convexity = 0.0;  // + gamma * shift_bp^2
    double duration_error = 0.0;           // actual - duration_only
    double with_convexity_error = 0.0;     // actual - duration_plus_convexity
};

// Reprices the book under each parallel shift and compares against the
// first- and second-order predictions. `shifts_bp` is in basis points.
std::vector<ShiftAttribution> shift_attribution(const std::vector<Swap>& portfolio,
                                                const Curve& discount,
                                                const std::vector<double>& shifts_bp,
                                                double bump = 1e-4);

}  // namespace fi
