#include "fi/bond.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace fi {

int per_year(Frequency f) noexcept { return static_cast<int>(f); }

namespace {

std::vector<Cashflow> build_schedule(double face, double coupon_rate,
                                     Frequency freq, const Date& issue,
                                     const Date& maturity) {
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

Bond::Bond(double face_value, double coupon_rate, Frequency frequency,
           Date issue_date, Date maturity_date, DayCount day_count)
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

}  // namespace fi
