#include "fi/risk.hpp"

#include <Eigen/Dense>

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace fi {

RiskMeasures risk_measures(const Bond& bond, double yield, const Date& valuation_date) {
    const auto m = static_cast<double>(per_year(bond.frequency()));
    const DayCount dc = bond.day_count();
    const double base = 1.0 + yield / m;

    double price = 0.0;          // Σ PV
    double t_weighted_pv = 0.0;  // Σ τ·PV
    double d2 = 0.0;             // d²P/dy²

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

// --- Portfolio-level curve risk ---------------------------------------------

namespace {

// A copy of `base` with every node zero rate moved by `shift`.
std::unique_ptr<Curve> shifted(const Curve& base, double shift) {
    std::vector<double> zeros = base.node_zero_rates();
    for (double& z : zeros) z += shift;
    return base.with_zero_rates(std::move(zeros));
}

// A copy of `base` with only node `index` moved by `shift`.
std::unique_ptr<Curve> shifted_node(const Curve& base, std::size_t index,
                                    double shift) {
    std::vector<double> zeros = base.node_zero_rates();
    zeros[index] += shift;
    return base.with_zero_rates(std::move(zeros));
}

}  // namespace

double portfolio_pv(const std::vector<Swap>& portfolio, const Curve& discount) {
    double pv = 0.0;
    for (const Swap& swap : portfolio) pv += swap.pv(discount);
    return pv;
}

double portfolio_dv01(const std::vector<Swap>& portfolio, const Curve& discount,
                      double bump) {
    const auto up = shifted(discount, bump);
    const auto down = shifted(discount, -bump);
    return (portfolio_pv(portfolio, *up) - portfolio_pv(portfolio, *down)) / 2.0;
}

std::vector<double> portfolio_key_rate_dv01(const std::vector<Swap>& portfolio,
                                            const Curve& discount, double bump) {
    const std::size_t nodes = discount.node_zero_rates().size();
    std::vector<double> krd(nodes, 0.0);
    for (std::size_t i = 0; i < nodes; ++i) {
        const auto up = shifted_node(discount, i, bump);
        const auto down = shifted_node(discount, i, -bump);
        krd[i] = (portfolio_pv(portfolio, *up) - portfolio_pv(portfolio, *down)) / 2.0;
    }
    return krd;
}

double portfolio_curve_gamma(const std::vector<Swap>& portfolio, const Curve& discount,
                             double bump) {
    const auto up = shifted(discount, bump);
    const auto down = shifted(discount, -bump);
    const double base = portfolio_pv(portfolio, discount);
    return (portfolio_pv(portfolio, *up) - 2.0 * base +
            portfolio_pv(portfolio, *down)) /
           2.0;
}

KeyRateReconciliation reconcile_key_rates(const std::vector<Swap>& portfolio,
                                          const Curve& discount, double bump) {
    KeyRateReconciliation report;
    report.node_times = discount.node_times();
    report.key_rate_dv01 = portfolio_key_rate_dv01(portfolio, discount, bump);
    for (const double bucket : report.key_rate_dv01) report.sum_of_buckets += bucket;
    report.parallel_dv01 = portfolio_dv01(portfolio, discount, bump);
    report.residual = report.sum_of_buckets - report.parallel_dv01;
    if (report.parallel_dv01 != 0.0) {
        report.relative_residual = report.residual / report.parallel_dv01;
    }
    return report;
}

BucketHedge solve_bucket_hedge(const std::vector<Swap>& portfolio,
                               const std::vector<Swap>& hedges, const Curve& discount,
                               double bump) {
    if (hedges.empty()) {
        throw std::invalid_argument("solve_bucket_hedge: need at least one hedge");
    }

    const std::vector<double> book = portfolio_key_rate_dv01(portfolio, discount, bump);
    const auto buckets = static_cast<Eigen::Index>(book.size());
    const auto instruments = static_cast<Eigen::Index>(hedges.size());

    // Column j is hedge j's key-rate profile for one unit of notional.
    Eigen::MatrixXd sensitivities(buckets, instruments);
    for (Eigen::Index j = 0; j < instruments; ++j) {
        const std::vector<Swap> single{hedges[static_cast<std::size_t>(j)]};
        const std::vector<double> column =
            portfolio_key_rate_dv01(single, discount, bump);
        for (Eigen::Index i = 0; i < buckets; ++i) {
            sensitivities(i, j) = column[static_cast<std::size_t>(i)];
        }
    }

    Eigen::VectorXd target(buckets);
    for (Eigen::Index i = 0; i < buckets; ++i) {
        target(i) = -book[static_cast<std::size_t>(i)];
    }

    // Column-pivoting QR rather than the normal equations: the hedge profiles
    // overlap heavily (a 10Y swap has real 5Y exposure), so the Gram matrix is
    // badly conditioned and squaring it would throw away half the precision.
    const Eigen::VectorXd weights = sensitivities.colPivHouseholderQr().solve(target);
    const Eigen::VectorXd residual = sensitivities * weights - target;

    BucketHedge result;
    result.notionals.resize(hedges.size());
    for (Eigen::Index j = 0; j < instruments; ++j) {
        result.notionals[static_cast<std::size_t>(j)] = weights(j);
    }
    result.residual_krd.resize(book.size());
    for (Eigen::Index i = 0; i < buckets; ++i) {
        result.residual_krd[static_cast<std::size_t>(i)] = residual(i);
        result.worst_bucket_before = std::max(
            result.worst_bucket_before, std::abs(book[static_cast<std::size_t>(i)]));
        result.worst_bucket_after =
            std::max(result.worst_bucket_after, std::abs(residual(i)));
    }

    result.residual_dv01 = portfolio_dv01(portfolio, discount, bump);
    for (std::size_t j = 0; j < hedges.size(); ++j) {
        const std::vector<Swap> single{hedges[j]};
        result.residual_dv01 +=
            result.notionals[j] * portfolio_dv01(single, discount, bump);
    }
    return result;
}

std::vector<ShiftAttribution> shift_attribution(const std::vector<Swap>& portfolio,
                                                const Curve& discount,
                                                const std::vector<double>& shifts_bp,
                                                double bump) {
    const double base = portfolio_pv(portfolio, discount);
    const double dv01 = portfolio_dv01(portfolio, discount, bump);
    const double gamma = portfolio_curve_gamma(portfolio, discount, bump);

    std::vector<ShiftAttribution> rows;
    rows.reserve(shifts_bp.size());
    for (const double shift_bp : shifts_bp) {
        const auto bumped = shifted(discount, shift_bp * 1e-4);

        ShiftAttribution row;
        row.shift_bp = shift_bp;
        row.actual_pnl = portfolio_pv(portfolio, *bumped) - base;
        row.duration_only = dv01 * shift_bp;
        row.duration_plus_convexity = row.duration_only + gamma * shift_bp * shift_bp;
        row.duration_error = row.actual_pnl - row.duration_only;
        row.with_convexity_error = row.actual_pnl - row.duration_plus_convexity;
        rows.push_back(row);
    }
    return rows;
}

}  // namespace fi
