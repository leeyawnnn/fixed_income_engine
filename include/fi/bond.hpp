#pragma once

#include <stdexcept>
#include <vector>

#include "fi/cashflow.hpp"
#include "fi/curve.hpp"
#include "fi/date.hpp"
#include "fi/day_count.hpp"
#include "fi/solver.hpp"

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
    Bond(double face_value, double coupon_rate, Frequency frequency, Date issue_date,
         Date maturity_date, DayCount day_count);

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

// --- Settlement quantities ---------------------------------------------------

// Where a settlement date sits in its coupon period.
struct AccrualPeriod {
    Date period_start;
    Date period_end;            // the next coupon date
    double accrued = 0.0;       // currency, on the bond's day count
    double days_accrued = 0.0;  // in year-fraction terms
    double days_in_period = 0.0;
};

// The coupon period containing `settlement` and the interest accrued into it.
// Throws std::invalid_argument if settlement is outside [issue, maturity).
//
// Bonds quote *clean*: the price on the screen excludes accrued interest, so
// it does not sawtooth down by a coupon on every payment date. What actually
// changes hands is the dirty price, clean + accrued. Everything discounted in
// this library is a dirty price, because that is what the cashflows are worth;
// the clean price is a quoting convention laid on top.
AccrualPeriod accrual(const Bond& bond, const Date& settlement);

double accrued_interest(const Bond& bond, const Date& settlement);

// Dirty price is PV of the remaining cashflows; clean is dirty minus accrued.
double dirty_price(const Bond& bond, double yield, const Date& settlement);
double clean_price(const Bond& bond, double yield, const Date& settlement);

// --- Spread to a curve -------------------------------------------------------

// Z-spread: the constant continuously-compounded spread z added to every point
// of the curve that makes the curve-discounted PV equal `target_dirty_price`.
//
//   PV(z) = sum_k c_k * DF(t_k) * exp(-z * t_k)
//
// Unlike a yield spread it does not assume a flat curve, and unlike an
// asset-swap spread it involves no swap: it is the parallel move in *zero*
// rates the bond is paying you over the risk-free curve. Solved by safeguarded
// Newton over [-0.5, 1.0].
//
// Throws std::invalid_argument if the bond has no cashflows after settlement.
SolverResult z_spread(const Bond& bond, double target_dirty_price,
                      const Date& settlement, const Curve& discount,
                      const SolverConfig& cfg = {});

// PV of the bond's remaining cashflows off `discount`, with a constant spread
// added to the curve's zero rates. The dirty price at spread == 0 is the
// curve's own valuation of the bond.
double price_from_curve(const Bond& bond, const Date& settlement, const Curve& discount,
                        double spread = 0.0);

}  // namespace fi
