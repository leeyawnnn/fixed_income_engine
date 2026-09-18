#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "fi/money.hpp"

using fi::Currency;
using fi::Money;

TEST_CASE("minor-unit scale follows the currency, not a hardcoded 100", "[money]") {
    REQUIRE(fi::minor_exponent(Currency::USD) == 2);
    REQUIRE(fi::minor_units_per_major(Currency::USD) == 100);
    REQUIRE(fi::minor_exponent(Currency::JPY) == 0);
    REQUIRE(fi::minor_units_per_major(Currency::JPY) == 1);

    REQUIRE(Money::from_double(1234.56, Currency::USD).minor_units() == 123456);
    REQUIRE(Money::from_double(1234.0, Currency::JPY).minor_units() == 1234);
    REQUIRE(Money::from_double(1234.0, Currency::JPY).to_string() == "1234");
}

TEST_CASE("double round-trips exactly through minor units", "[money]") {
    // Every amount here is representable to the cent, so the crossing is exact
    // in both directions.
    const std::vector<double> amounts = {0.0,        0.01,        -0.01,     1.0,
                                         -203004.53, 10000000.00, 1e9 + 0.99};
    for (const double a : amounts) {
        const Money m = Money::from_double(a, Currency::USD);
        REQUIRE(m.to_double() == a);
        REQUIRE(Money::from_double(m.to_double(), Currency::USD) == m);
    }
}

TEST_CASE("exact halves round half-to-even, not half-away-from-zero", "[money]") {
    // 0.005 and 0.015 are not exactly representable as doubles, so a test that
    // used them would be testing the binary representation rather than the
    // rounding rule. Use JPY, whose minor unit is 1, so the ties are exact.
    REQUIRE(Money::from_double(2.5, Currency::JPY).minor_units() == 2);
    REQUIRE(Money::from_double(3.5, Currency::JPY).minor_units() == 4);
    REQUIRE(Money::from_double(-2.5, Currency::JPY).minor_units() == -2);
    REQUIRE(Money::from_double(-3.5, Currency::JPY).minor_units() == -4);
    REQUIRE(Money::from_double(0.5, Currency::JPY).minor_units() == 0);
    REQUIRE(Money::from_double(1.5, Currency::JPY).minor_units() == 2);

    // Away from a tie the nearest value wins, as usual.
    REQUIRE(Money::from_double(2.4999, Currency::JPY).minor_units() == 2);
    REQUIRE(Money::from_double(2.5001, Currency::JPY).minor_units() == 3);

    // The point of half-to-even: over a symmetric set of ties the roundings
    // cancel instead of accumulating a bias.
    std::int64_t total = 0;
    for (int k = 0; k < 100; ++k) {
        total += Money::from_double(k + 0.5, Currency::JPY).minor_units();
    }
    // Ties round down for even k and up for odd k, so the errors cancel exactly.
    REQUIRE(total == 5000);
}

TEST_CASE("accumulating many small amounts does not drift", "[money]") {
    // The canonical float failure: 0.01 has no exact binary representation, so
    // summing it 10,000 times in double leaves a visible residue. The same sum
    // in minor units is exact by construction.
    constexpr int kCount = 10000;

    double as_double = 0.0;
    for (int i = 0; i < kCount; ++i) as_double += 0.01;
    REQUIRE(as_double != 100.0);  // this is the bug Money exists to avoid

    Money as_money;
    const Money penny = Money::from_double(0.01, Currency::USD);
    for (int i = 0; i < kCount; ++i) as_money += penny;

    REQUIRE(as_money.minor_units() == kCount);  // 10,000 cents
    REQUIRE(as_money.to_double() == 100.0);
    REQUIRE(as_money.to_string() == "100.00");
}

TEST_CASE("formatting pads the fractional part and handles sign", "[money]") {
    REQUIRE(Money::from_double(-203004.53, Currency::USD).to_string() == "-203004.53");
    REQUIRE(Money::from_minor_units(5, Currency::USD).to_string() == "0.05");
    REQUIRE(Money::from_minor_units(-5, Currency::USD).to_string() == "-0.05");
    REQUIRE(Money::from_minor_units(0, Currency::USD).to_string() == "0.00");
    REQUIRE(Money::from_minor_units(-1, Currency::USD).to_string_with_currency() ==
            "USD -0.01");

    // INT64_MIN cannot be negated in the signed domain; formatting must not
    // rely on doing so.
    const Money extreme = Money::from_minor_units(
        std::numeric_limits<std::int64_t>::min(), Currency::USD);
    REQUIRE(extreme.to_string() == "-92233720368547758.08");
}

TEST_CASE("arithmetic is currency-checked and overflow-checked", "[money]") {
    const Money usd = Money::from_double(10.0, Currency::USD);
    const Money eur = Money::from_double(10.0, Currency::EUR);

    REQUIRE((usd + usd).to_double() == 20.0);
    REQUIRE((usd - usd).minor_units() == 0);
    REQUIRE((usd * 3).to_double() == 30.0);
    REQUIRE((-usd).to_double() == -10.0);
    REQUIRE(usd - usd * 2 == -usd);

    REQUIRE_THROWS_AS(usd + eur, std::invalid_argument);
    REQUIRE_THROWS_AS(usd - eur, std::invalid_argument);
    REQUIRE_THROWS_AS(usd < eur, std::invalid_argument);

    const Money big = Money::from_minor_units(std::numeric_limits<std::int64_t>::max(),
                                              Currency::USD);
    REQUIRE_THROWS_AS(big + Money::from_minor_units(1, Currency::USD),
                      std::overflow_error);
    REQUIRE_THROWS_AS(big * 2, std::overflow_error);
    REQUIRE_THROWS_AS(Money::from_double(1e30, Currency::USD), std::overflow_error);
    REQUIRE_THROWS_AS(
        Money::from_double(std::numeric_limits<double>::quiet_NaN(), Currency::USD),
        std::overflow_error);
}

TEST_CASE("currency codes round-trip and reject unknown input", "[money]") {
    for (const Currency c :
         {Currency::USD, Currency::EUR, Currency::GBP, Currency::JPY}) {
        REQUIRE(fi::currency_from_string(fi::to_string(c)) == c);
    }
    REQUIRE_THROWS_AS(fi::currency_from_string("XYZ"), std::invalid_argument);
}
