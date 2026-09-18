#include "fi/money.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace fi {

int minor_exponent(Currency ccy) noexcept {
    switch (ccy) {
        case Currency::USD:
        case Currency::EUR:
        case Currency::GBP: return 2;
        case Currency::JPY: return 0;  // the yen has no subdivision in practice
    }
    return 2;
}

std::int64_t minor_units_per_major(Currency ccy) noexcept {
    std::int64_t scale = 1;
    for (int i = 0; i < minor_exponent(ccy); ++i) scale *= 10;
    return scale;
}

const char* to_string(Currency ccy) noexcept {
    switch (ccy) {
        case Currency::USD: return "USD";
        case Currency::EUR: return "EUR";
        case Currency::GBP: return "GBP";
        case Currency::JPY: return "JPY";
    }
    return "???";
}

Currency currency_from_string(const std::string& code) {
    if (code == "USD") return Currency::USD;
    if (code == "EUR") return Currency::EUR;
    if (code == "GBP") return Currency::GBP;
    if (code == "JPY") return Currency::JPY;
    throw std::invalid_argument("Money: unsupported currency code '" + code + "'");
}

namespace {

// Round half to even, written out rather than delegated to std::nearbyint.
// nearbyint honours the *current* floating-point rounding mode, so its
// behaviour at a tie depends on global state any library in the process can
// change. Money's rounding is part of its contract, so it does not depend on
// that.
double round_half_to_even(double x) noexcept {
    const double below = std::floor(x);
    const double frac = x - below;
    if (frac > 0.5) return below + 1.0;
    if (frac < 0.5) return below;
    // Exactly halfway: take whichever neighbour is even.
    return (std::fmod(below, 2.0) == 0.0) ? below : below + 1.0;
}

constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();

// Overflow checks written against the limits rather than with
// __builtin_*_overflow, which is a GCC/Clang extension. Signed overflow is
// undefined, so every check runs *before* the operation.
bool adds_overflow(std::int64_t a, std::int64_t b) noexcept {
    return (b > 0 && a > kMax - b) || (b < 0 && a < kMin - b);
}

bool subtracts_overflow(std::int64_t a, std::int64_t b) noexcept {
    return (b < 0 && a > kMax + b) || (b > 0 && a < kMin + b);
}

bool multiplies_overflow(std::int64_t a, std::int64_t b) noexcept {
    if (a == 0 || b == 0) return false;
    if (a > 0) return b > 0 ? a > kMax / b : b < kMin / a;
    return b > 0 ? a < kMin / b : a < kMax / b;
}

void require_same_currency(Currency a, Currency b) {
    if (a != b) {
        throw std::invalid_argument(std::string("Money: currency mismatch, ") +
                                    to_string(a) + " vs " + to_string(b));
    }
}

}  // namespace

Money Money::from_double(double major_units, Currency ccy) {
    if (!std::isfinite(major_units)) {
        throw std::overflow_error("Money::from_double: amount is not finite");
    }
    const double scaled = major_units * static_cast<double>(minor_units_per_major(ccy));

    // 2^63 is not exactly representable as a double, so comparing against
    // int64 max after conversion is undefined. Compare in double space against
    // the largest power of two that is safely below the limit.
    constexpr double kLimit = 9.2e18;
    if (std::abs(scaled) > kLimit) {
        throw std::overflow_error(
            "Money::from_double: amount does not fit in an int64 minor-unit count");
    }
    return Money{static_cast<std::int64_t>(round_half_to_even(scaled)), ccy};
}

double Money::to_double() const noexcept {
    return static_cast<double>(minor_) /
           static_cast<double>(minor_units_per_major(ccy_));
}

std::string Money::to_string() const {
    const std::int64_t scale = minor_units_per_major(ccy_);
    const bool negative = minor_ < 0;

    // Negate in the unsigned domain: -minor_ overflows for INT64_MIN.
    auto magnitude = static_cast<std::uint64_t>(minor_);
    if (negative) magnitude = ~magnitude + 1U;

    const std::uint64_t whole = magnitude / static_cast<std::uint64_t>(scale);
    const std::uint64_t frac = magnitude % static_cast<std::uint64_t>(scale);

    std::string out = negative ? "-" : "";
    out += std::to_string(whole);
    if (const int places = minor_exponent(ccy_); places > 0) {
        std::string frac_s = std::to_string(frac);
        out += '.';
        out.append(static_cast<std::size_t>(places) - frac_s.size(), '0');
        out += frac_s;
    }
    return out;
}

std::string Money::to_string_with_currency() const {
    return std::string(fi::to_string(ccy_)) + " " + to_string();
}

Money& Money::operator+=(const Money& rhs) {
    require_same_currency(ccy_, rhs.ccy_);
    if (adds_overflow(minor_, rhs.minor_)) {
        throw std::overflow_error("Money: addition overflows int64 minor units");
    }
    minor_ += rhs.minor_;
    return *this;
}

Money& Money::operator-=(const Money& rhs) {
    require_same_currency(ccy_, rhs.ccy_);
    if (subtracts_overflow(minor_, rhs.minor_)) {
        throw std::overflow_error("Money: subtraction overflows int64 minor units");
    }
    minor_ -= rhs.minor_;
    return *this;
}

Money& Money::operator*=(std::int64_t factor) {
    if (multiplies_overflow(minor_, factor)) {
        throw std::overflow_error("Money: scaling overflows int64 minor units");
    }
    minor_ *= factor;
    return *this;
}

bool operator<(const Money& a, const Money& b) {
    require_same_currency(a.ccy_, b.ccy_);
    return a.minor_ < b.minor_;
}

}  // namespace fi
