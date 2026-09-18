#pragma once

#include "fi/bond.hpp"  // Frequency
#include "fi/curve.hpp"
#include "fi/date.hpp"
#include "fi/day_count.hpp"

namespace fi {

// Direction is from the holder's perspective: a Payer pays fixed / receives
// floating; a Receiver receives fixed / pays floating.
enum class SwapDirection { Payer, Receiver };

// A vanilla single-currency interest-rate swap.
//
// OIS discounting (the post-2008 standard): cashflows are discounted on an OIS
// curve, while floating coupons are projected off a (possibly different)
// forward curve. Pass one curve for the legacy single-curve case; the two-curve
// overloads take an explicit projection curve.
//
// Floating coupon j over [t_{j-1}, t_j] uses the simple forward
//   L_j = (DF_proj(t_{j-1})/DF_proj(t_j) − 1) / τ_j,
// so its PV contribution is (DF_proj(t_{j-1})/DF_proj(t_j) − 1)·DF_disc(t_j);
// the accrual τ_j cancels, which is why a self-discounted floating leg
// telescopes to DF(start) − DF(maturity).
class Swap {
public:
    Swap(double notional, double fixed_rate, SwapDirection direction, Date start,
         Date maturity, Frequency fixed_frequency, DayCount fixed_day_count,
         Frequency float_frequency, DayCount float_day_count);

    // Fixed-leg annuity Σ α_k·DF_disc(t_k), per unit notional.
    double annuity(const Curve& discount) const;

    // Unsigned leg present values (always ≥ 0 for positive rates/notional).
    double fixed_leg_pv(const Curve& discount) const;
    double floating_leg_pv(const Curve& discount, const Curve& projection) const;
    double floating_leg_pv(const Curve& discount) const {
        return floating_leg_pv(discount, discount);
    }

    // Swap PV with the direction's sign applied.
    double pv(const Curve& discount, const Curve& projection) const;
    double pv(const Curve& discount) const { return pv(discount, discount); }

    // Fixed rate that makes the swap worth zero (direction-independent).
    double par_rate(const Curve& discount, const Curve& projection) const;
    double par_rate(const Curve& discount) const {
        return par_rate(discount, discount);
    }

    // PV01: value of one basis point on the fixed rate, i.e. notional * annuity
    // * 1e-4. Always positive.
    //
    // PV01 and DV01 are routinely conflated and are not the same number. PV01
    // moves the *coupon* and holds the curve still; DV01 moves the *curve* and
    // holds the coupon still. They are close for a par swap, because there the
    // two moves hit almost the same cashflows, and they separate as the swap
    // goes off-market.
    double pv01(const Curve& discount) const {
        return notional_ * annuity(discount) * 1e-4;
    }

    // PV split into its legs, signed from the holder's view, which is what a
    // valuation report shows. fixed + floating == pv().
    struct LegBreakdown {
        double fixed = 0.0;
        double floating = 0.0;
        double annuity = 0.0;  // per unit notional
        double par_rate = 0.0;
        double rate_offset = 0.0;  // fixed_rate - par_rate
    };
    LegBreakdown legs(const Curve& discount, const Curve& projection) const;
    LegBreakdown legs(const Curve& discount) const { return legs(discount, discount); }

    double notional() const noexcept { return notional_; }
    double fixed_rate() const noexcept { return fixed_rate_; }
    SwapDirection direction() const noexcept { return direction_; }
    Date start() const noexcept { return start_; }
    Date maturity() const noexcept { return maturity_; }

    // Leg conventions. `float_day_count()` does not enter pv() by design: the
    // projected coupon L_j and the accrual τ_j are taken on the same basis, so
    // L_j·τ_j collapses to DF_proj(t_{j-1})/DF_proj(t_j) − 1 and the day count
    // cancels. It is still part of the trade's terms, and it is what a basis or
    // multi-curve extension would need, so the swap carries it and reports can
    // print it.
    Frequency fixed_frequency() const noexcept { return fixed_frequency_; }
    DayCount fixed_day_count() const noexcept { return fixed_day_count_; }
    Frequency float_frequency() const noexcept { return float_frequency_; }
    DayCount float_day_count() const noexcept { return float_day_count_; }

private:
    double notional_;
    double fixed_rate_;
    SwapDirection direction_;
    Date start_;
    Date maturity_;
    Frequency fixed_frequency_;
    DayCount fixed_day_count_;
    Frequency float_frequency_;
    DayCount float_day_count_;
};

}  // namespace fi
