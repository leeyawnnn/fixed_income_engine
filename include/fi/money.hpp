#pragma once

#include <cstdint>
#include <string>

namespace fi {

// Currencies are carried as a tag so two amounts cannot be added unless they
// agree. `minor_exponent` is the number of decimal places in the currency's
// minor unit: 2 for a cent, 0 for a yen. Getting this from a table rather than
// assuming 100 is the difference between code that works on USD and code that
// works.
enum class Currency : std::uint8_t {
    USD,
    EUR,
    GBP,
    JPY,
};

int minor_exponent(Currency ccy) noexcept;  // USD/EUR/GBP -> 2, JPY -> 0
std::int64_t minor_units_per_major(Currency ccy) noexcept;  // 100, or 1 for JPY
const char* to_string(Currency ccy) noexcept;               // "USD"
Currency currency_from_string(const std::string& code);     // throws if unknown

// An exact monetary amount, held as a signed count of minor units.
//
// Why integers at all, when every price in this library is a double? Because
// the two jobs are different. Discounting, root-finding and curve fitting are
// approximations of continuous mathematics, and a double is the correct
// representation for them: the answer is uncertain in the fifth decimal place
// for reasons that have nothing to do with floating point. A booked notional,
// a settlement amount, or a P&L figure in a report is not an approximation of
// anything - it is a specific number of cents, and 0.1 + 0.2 must not be
// 0.30000000000000004.
//
// So the boundary is deliberate and it runs in one place: doubles inside the
// numerical core, Money at the edges where a quantity is declared (the
// portfolio file) or emitted (reports, CSV). Crossing it is explicit in both
// directions - there is no implicit conversion, and from_double() names its
// rounding mode.
class Money {
public:
    Money() = default;

    // Exact construction. No rounding happens here because none is needed.
    static Money from_minor_units(std::int64_t minor, Currency ccy) noexcept {
        return Money{minor, ccy};
    }

    // Construction from a computed double, rounding half to even ("banker's
    // rounding"): 2.5 and 3.5 both round to the nearest even minor unit, 2 and
    // 4. Half-up would bias a large book of roundings upward by half a minor
    // unit per exact tie; half-to-even has no first-order bias, which is why
    // it is the IEEE-754 default and the market convention for settlement.
    //
    // Throws std::overflow_error if the amount does not fit in an int64 count
    // of minor units, or if it is not finite. Explicit by design: a double
    // turning into money is a decision, not a coercion.
    static Money from_double(double major_units, Currency ccy);

    // The reverse crossing, also explicit. Exact for any amount inside 2^53
    // minor units, which is every book this library will ever be pointed at.
    double to_double() const noexcept;

    std::int64_t minor_units() const noexcept { return minor_; }
    Currency currency() const noexcept { return ccy_; }

    // Fixed-point decimal with the currency's own number of places and no
    // thousands separators: "-203004.53", "1250" for JPY. This is the CSV form.
    std::string to_string() const;

    // The same amount prefixed with its code: "USD -203004.53".
    std::string to_string_with_currency() const;

    // Arithmetic is exact and currency-checked. Mixing currencies throws
    // std::invalid_argument rather than silently producing a number.
    Money& operator+=(const Money& rhs);
    Money& operator-=(const Money& rhs);
    Money operator-() const noexcept { return Money{-minor_, ccy_}; }

    // Scaling by an integer count (lots, contracts) stays exact. There is
    // deliberately no operator*(double): multiplying money by a rate is a
    // pricing operation, and it belongs on the double side of the boundary.
    Money& operator*=(std::int64_t factor);

    friend Money operator+(Money a, const Money& b) { return a += b; }
    friend Money operator-(Money a, const Money& b) { return a -= b; }
    friend Money operator*(Money a, std::int64_t f) { return a *= f; }
    friend Money operator*(std::int64_t f, Money a) { return a *= f; }

    friend bool operator==(const Money& a, const Money& b) noexcept {
        return a.ccy_ == b.ccy_ && a.minor_ == b.minor_;
    }
    friend bool operator<(const Money& a, const Money& b);

private:
    Money(std::int64_t minor, Currency ccy) noexcept : minor_(minor), ccy_(ccy) {}

    std::int64_t minor_ = 0;
    Currency ccy_ = Currency::USD;
};

}  // namespace fi
