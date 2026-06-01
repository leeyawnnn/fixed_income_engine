#pragma once

#include <vector>

#include "fi/cashflow.hpp"
#include "fi/date.hpp"
#include "fi/day_count.hpp"

namespace fi {

// Coupon payment frequency, as integer coupons per year.
enum class Frequency {
    Annual = 1,
    SemiAnnual = 2,
    Quarterly = 4,
    Monthly = 12,
};

// Coupons per year as an int (e.g. SemiAnnual -> 2).
int per_year(Frequency f) noexcept;

// A fixed-coupon bullet bond: level coupons at a fixed frequency, principal
// repaid at maturity. A zero-coupon bond is just coupon_rate == 0.
class Bond {
public:
    // coupon_rate is the *annual* rate (0.05 == 5%). The per-period coupon is
    // face_value * coupon_rate / per_year(frequency). Throws if maturity is not
    // after issue, or the frequency does not divide 12.
    Bond(double face_value, double coupon_rate, Frequency frequency,
         Date issue_date, Date maturity_date, DayCount day_count);

    // The generated schedule, ascending by date. The maturity cashflow includes
    // the principal. Zero-amount coupons (e.g. a zero-coupon bond's interim
    // dates) are omitted.
    const std::vector<Cashflow>& cashflows() const noexcept { return cashflows_; }

    // Present value of all cashflows after `valuation_date`, discounted at a
    // flat yield compounded at the coupon frequency m:
    //     PV = Σ cf.amount · (1 + y/m)^(−m · τ)
    // where τ is the year fraction from valuation_date to the cashflow under the
    // bond's day-count convention. This is the full (dirty) price.
    double price_from_yield(double yield, const Date& valuation_date) const;

    // Convenience overload valuing as of the issue date (no accrued interest).
    double price_from_yield(double yield) const {
        return price_from_yield(yield, issue_date_);
    }

    double face_value() const noexcept { return face_value_; }
    double coupon_rate() const noexcept { return coupon_rate_; }
    Frequency frequency() const noexcept { return frequency_; }
    Date issue_date() const noexcept { return issue_date_; }
    Date maturity_date() const noexcept { return maturity_date_; }
    DayCount day_count() const noexcept { return day_count_; }

private:
    double face_value_;
    double coupon_rate_;
    Frequency frequency_;
    Date issue_date_;
    Date maturity_date_;
    DayCount day_count_;
    std::vector<Cashflow> cashflows_;
};

}  // namespace fi
