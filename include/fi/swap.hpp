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
    double par_rate(const Curve& discount) const { return par_rate(discount, discount); }

    double notional() const noexcept { return notional_; }
    double fixed_rate() const noexcept { return fixed_rate_; }
    SwapDirection direction() const noexcept { return direction_; }
    Date start() const noexcept { return start_; }
    Date maturity() const noexcept { return maturity_; }

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
