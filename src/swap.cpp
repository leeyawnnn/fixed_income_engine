#include "fi/swap.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace fi {

namespace {

// Payment dates, anchored to maturity and stepped backwards (EOM-safe).
std::vector<Date> schedule(const Date& start, const Date& maturity,
                           Frequency freq) {
    const int step = 12 / per_year(freq);
    std::vector<Date> dates;
    for (int k = 0;; ++k) {
        Date d = maturity.add_months(-step * k);
        if (!(d > start)) break;
        dates.push_back(d);
    }
    std::reverse(dates.begin(), dates.end());
    return dates;
}

}  // namespace

Swap::Swap(double notional, double fixed_rate, SwapDirection direction,
           Date start, Date maturity, Frequency fixed_frequency,
           DayCount fixed_day_count, Frequency float_frequency,
           DayCount float_day_count)
    : notional_(notional),
      fixed_rate_(fixed_rate),
      direction_(direction),
      start_(start),
      maturity_(maturity),
      fixed_frequency_(fixed_frequency),
      fixed_day_count_(fixed_day_count),
      float_frequency_(float_frequency),
      float_day_count_(float_day_count) {
    if (!(maturity_ > start_)) {
        throw std::invalid_argument("Swap: maturity must be after start");
    }
}

double Swap::annuity(const Curve& discount) const {
    double a = 0.0;
    Date prev = start_;
    for (const Date& d : schedule(start_, maturity_, fixed_frequency_)) {
        a += year_fraction(prev, d, fixed_day_count_) * discount.discount(d);
        prev = d;
    }
    return a;
}

double Swap::fixed_leg_pv(const Curve& discount) const {
    return notional_ * fixed_rate_ * annuity(discount);
}

double Swap::floating_leg_pv(const Curve& discount,
                             const Curve& projection) const {
    double pv = 0.0;
    Date prev = start_;
    for (const Date& d : schedule(start_, maturity_, float_frequency_)) {
        // L_j·τ_j = DF_proj(prev)/DF_proj(d) − 1  (the accrual cancels).
        const double fwd_accrual =
            projection.discount(prev) / projection.discount(d) - 1.0;
        pv += fwd_accrual * discount.discount(d);
        prev = d;
    }
    return notional_ * pv;
}

double Swap::pv(const Curve& discount, const Curve& projection) const {
    const double fixed = fixed_leg_pv(discount);
    const double floating = floating_leg_pv(discount, projection);
    return direction_ == SwapDirection::Payer ? floating - fixed
                                              : fixed - floating;
}

double Swap::par_rate(const Curve& discount, const Curve& projection) const {
    const double floating = floating_leg_pv(discount, projection);
    return floating / (notional_ * annuity(discount));
}

}  // namespace fi
