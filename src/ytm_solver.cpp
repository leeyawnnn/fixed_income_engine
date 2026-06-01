#include "fi/ytm_solver.hpp"

#include <cmath>

namespace fi {

SolverResult solve_ytm(const Bond& bond, double target_price,
                       const Date& valuation_date, const SolverConfig& cfg) {
    const int m = per_year(bond.frequency());
    const DayCount dc = bond.day_count();
    const auto& cfs = bond.cashflows();

    // Residual: price(y) - target. price is monotonically decreasing in y.
    auto residual = [&](double y) {
        return bond.price_from_yield(y, valuation_date) - target_price;
    };

    // Analytic derivative dP/dy = Σ amount · (−τ) · (1 + y/m)^(−mτ − 1).
    auto derivative = [&](double y) {
        double d = 0.0;
        for (const Cashflow& cf : cfs) {
            if (cf.date <= valuation_date) continue;
            const double tau = year_fraction(valuation_date, cf.date, dc);
            d += cf.amount * (-tau) * std::pow(1.0 + y / m, -m * tau - 1.0);
        }
        return d;
    };

    // 1 + y/m must stay positive, so the lower bracket is just above -m; -0.99
    // is safe for every supported frequency (m >= 1). 10.0 (1000%) is a roomy
    // upper bound that brackets even deeply discounted bonds.
    const double guess = bond.coupon_rate();
    return newton_bisection(residual, derivative, guess, -0.99, 10.0, cfg);
}

SolverResult solve_ytm(const Bond& bond, double target_price,
                       const SolverConfig& cfg) {
    return solve_ytm(bond, target_price, bond.issue_date(), cfg);
}

}  // namespace fi
