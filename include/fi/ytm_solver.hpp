#pragma once

#include "fi/bond.hpp"
#include "fi/date.hpp"
#include "fi/solver.hpp"

namespace fi {

// Solve for the yield to maturity (annualized, compounded at the bond's coupon
// frequency) that reprices the bond to `target_price` as of `valuation_date`.
//
// Uses safeguarded Newton-Raphson: the initial guess is the coupon rate, the
// analytic price derivative drives Newton steps, and a [-0.99, 10.0] yield
// bracket lets the solver bisect if Newton would diverge. Returns the root,
// iteration count, and a convergence flag (per SolverConfig).
SolverResult solve_ytm(const Bond& bond, double target_price,
                       const Date& valuation_date, const SolverConfig& cfg = {});

// Convenience overload valuing as of the issue date.
SolverResult solve_ytm(const Bond& bond, double target_price,
                       const SolverConfig& cfg = {});

}  // namespace fi
