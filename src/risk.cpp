#include "fi/risk.hpp"

#include <cmath>

namespace fi {

RiskMeasures risk_measures(const Bond& bond, double yield,
                           const Date& valuation_date) {
    const double m = static_cast<double>(per_year(bond.frequency()));
    const DayCount dc = bond.day_count();
    const double base = 1.0 + yield / m;

    double price = 0.0;        // Σ PV
    double t_weighted_pv = 0.0;  // Σ τ·PV
    double d2 = 0.0;           // d²P/dy²

    for (const Cashflow& cf : bond.cashflows()) {
        if (cf.date <= valuation_date) continue;
        const double tau = year_fraction(valuation_date, cf.date, dc);
        const double df = std::pow(base, -m * tau);
        const double pv = cf.amount * df;

        price += pv;
        t_weighted_pv += tau * pv;
        d2 += cf.amount * tau * (tau + 1.0 / m) * std::pow(base, -m * tau - 2.0);
    }

    RiskMeasures r;
    r.price = price;
    r.macaulay_duration = t_weighted_pv / price;
    r.modified_duration = r.macaulay_duration / base;
    r.convexity = d2 / price;
    r.dv01 = r.modified_duration * price * 1e-4;
    return r;
}

double macaulay_duration(const Bond& b, double y, const Date& val) {
    return risk_measures(b, y, val).macaulay_duration;
}
double modified_duration(const Bond& b, double y, const Date& val) {
    return risk_measures(b, y, val).modified_duration;
}
double convexity(const Bond& b, double y, const Date& val) {
    return risk_measures(b, y, val).convexity;
}
double dv01(const Bond& b, double y, const Date& val) {
    return risk_measures(b, y, val).dv01;
}

double dv01_finite_difference(const Bond& bond, double yield,
                              const Date& valuation_date, double bump) {
    const double p_up = bond.price_from_yield(yield + bump, valuation_date);
    const double p_dn = bond.price_from_yield(yield - bump, valuation_date);
    // Central estimate of −dP/dy, scaled to a 1bp move.
    return (p_dn - p_up) / (2.0 * bump) * 1e-4;
}

double swap_dv01(const Swap& swap, const Curve& discount, double bump) {
    std::vector<double> up = discount.node_zero_rates();
    std::vector<double> dn = up;
    for (double& z : up) z += bump;
    for (double& z : dn) z -= bump;
    const auto curve_up = discount.with_zero_rates(std::move(up));
    const auto curve_dn = discount.with_zero_rates(std::move(dn));
    return (swap.pv(*curve_up) - swap.pv(*curve_dn)) / 2.0;
}

std::vector<double> swap_key_rate_dv01(const Swap& swap, const Curve& discount,
                                       double bump) {
    const std::vector<double>& base = discount.node_zero_rates();
    std::vector<double> krd(base.size());
    for (std::size_t i = 0; i < base.size(); ++i) {
        std::vector<double> up = base;
        std::vector<double> dn = base;
        up[i] += bump;
        dn[i] -= bump;
        const auto curve_up = discount.with_zero_rates(std::move(up));
        const auto curve_dn = discount.with_zero_rates(std::move(dn));
        krd[i] = (swap.pv(*curve_up) - swap.pv(*curve_dn)) / 2.0;
    }
    return krd;
}

}  // namespace fi
