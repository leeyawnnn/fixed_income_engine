#pragma once

#include <vector>

#include "fi/bond.hpp"
#include "fi/curve.hpp"
#include "fi/date.hpp"
#include "fi/swap.hpp"

namespace fi {

// Analytic interest-rate risk measures for a fixed-coupon bond, all consistent
// with the Phase 2/3 discounting P = Σ c·(1 + y/m)^(−m·τ):
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
RiskMeasures risk_measures(const Bond& bond, double yield,
                           const Date& valuation_date);
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
inline double dv01(const Bond& b, double y) { return dv01(b, y, b.issue_date()); }

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

}  // namespace fi
