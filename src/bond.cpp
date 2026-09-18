#include "fi/bond.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace fi {

int per_year(Frequency f) noexcept {
    return static_cast<int>(f);
}

namespace {

std::vector<Cashflow> build_schedule(double face, double coupon_rate, Frequency freq,
                                     const Date& issue, const Date& maturity) {
    if (!(maturity > issue)) {
        throw std::invalid_argument("Bond: maturity must be after issue date");
    }
    const int m = per_year(freq);
    const int step = 12 / m;
    if (step * m != 12) {
        throw std::invalid_argument("Bond: frequency must divide 12 months");
    }

    // Coupon dates are anchored to maturity and stepped backwards, so the
    // end-of-month clamping in Date::add_months can't accumulate drift.
    std::vector<Date> dates;
    for (int k = 0;; ++k) {
        Date d = maturity.add_months(-step * k);
        if (!(d > issue)) break;
        dates.push_back(d);
    }
    std::reverse(dates.begin(), dates.end());

    const double coupon = face * coupon_rate / m;
    std::vector<Cashflow> cfs;
    cfs.reserve(dates.size());
    for (const Date& d : dates) {
        double amount = coupon;
        if (d == maturity) amount += face;  // principal repaid at maturity
        if (amount != 0.0) cfs.push_back(Cashflow{d, amount});
    }
    return cfs;
}

}  // namespace

Bond::Bond(double face_value, double coupon_rate, Frequency frequency, Date issue_date,
           Date maturity_date, DayCount day_count)
    : face_value_(face_value),
      coupon_rate_(coupon_rate),
      frequency_(frequency),
      issue_date_(issue_date),
      maturity_date_(maturity_date),
      day_count_(day_count),
      cashflows_(build_schedule(face_value, coupon_rate, frequency, issue_date,
                                maturity_date)) {}

double Bond::price_from_yield(double yield, const Date& valuation_date) const {
    const int m = per_year(frequency_);
    double pv = 0.0;
    for (const Cashflow& cf : cashflows_) {
        if (cf.date <= valuation_date) continue;  // ignore past/today cashflows
        const double tau = year_fraction(valuation_date, cf.date, day_count_);
        const double df = std::pow(1.0 + yield / m, -m * tau);
        pv += cf.amount * df;
    }
    return pv;
}

// --- Settlement quantities ---------------------------------------------------

AccrualPeriod accrual(const Bond& bond, const Date& settlement) {
    if (settlement < bond.issue_date() || !(settlement < bond.maturity_date())) {
        throw std::invalid_argument(
            "accrual: settlement must lie in [issue, maturity)");
    }

    // Walk back from maturity in whole coupon periods, the same anchoring the
    // schedule uses, so the period boundaries agree with the cashflow dates
    // even when end-of-month clamping is in play.
    const int step = 12 / per_year(bond.frequency());
    Date period_end = bond.maturity_date();
    Date period_start = period_end.add_months(-step);
    while (period_start > settlement) {
        period_end = period_start;
        period_start = period_end.add_months(-step);
    }
    if (period_start < bond.issue_date()) period_start = bond.issue_date();

    AccrualPeriod period;
    period.period_start = period_start;
    period.period_end = period_end;
    period.days_accrued = year_fraction(period_start, settlement, bond.day_count());
    period.days_in_period = year_fraction(period_start, period_end, bond.day_count());

    const double full_coupon =
        bond.face_value() * bond.coupon_rate() / per_year(bond.frequency());
    period.accrued = period.days_in_period > 0.0
                         ? full_coupon * (period.days_accrued / period.days_in_period)
                         : 0.0;
    return period;
}

double accrued_interest(const Bond& bond, const Date& settlement) {
    return accrual(bond, settlement).accrued;
}

double dirty_price(const Bond& bond, double yield, const Date& settlement) {
    return bond.price_from_yield(yield, settlement);
}

double clean_price(const Bond& bond, double yield, const Date& settlement) {
    return dirty_price(bond, yield, settlement) - accrued_interest(bond, settlement);
}

// --- Spread to a curve -------------------------------------------------------

double price_from_curve(const Bond& bond, const Date& settlement, const Curve& discount,
                        double spread) {
    double pv = 0.0;
    for (const Cashflow& cf : bond.cashflows()) {
        if (cf.date <= settlement) continue;
        // Time is measured from settlement, but the curve is anchored at its own
        // reference date, so discount to settlement and divide out the stub.
        const double t_settle = discount.time_to(settlement);
        const double t_flow = discount.time_to(cf.date);
        const double df = discount.discount(t_flow) / discount.discount(t_settle);
        pv += cf.amount * df * std::exp(-spread * (t_flow - t_settle));
    }
    return pv;
}

SolverResult z_spread(const Bond& bond, double target_dirty_price,
                      const Date& settlement, const Curve& discount,
                      const SolverConfig& cfg) {
    bool has_future_flow = false;
    for (const Cashflow& cf : bond.cashflows()) {
        if (cf.date > settlement) {
            has_future_flow = true;
            break;
        }
    }
    if (!has_future_flow) {
        throw std::invalid_argument("z_spread: no cashflows after settlement");
    }

    auto residual = [&](double spread) {
        return price_from_curve(bond, settlement, discount, spread) -
               target_dirty_price;
    };
    auto derivative = [&](double spread) {
        const double h = 1e-7;
        return (residual(spread + h) - residual(spread - h)) / (2.0 * h);
    };
    return newton_bisection(residual, derivative, 0.0, -0.5, 1.0, cfg);
}

}  // namespace fi
