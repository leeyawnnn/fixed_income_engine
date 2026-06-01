#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "fi/bond.hpp"

using Catch::Approx;
using fi::Bond;
using fi::Date;
using fi::DayCount;
using fi::Frequency;

// Closed-form price of a level-coupon bond on a coupon date, m coupons/year,
// n total periods, per-period coupon c, face F, periodic yield y/m.
static double closed_form_price(double c, double F, int n, double periodic_yield) {
    const double df_n = std::pow(1.0 + periodic_yield, -n);
    const double annuity = (1.0 - df_n) / periodic_yield;
    return c * annuity + F * df_n;
}

TEST_CASE("Coupon schedule is generated correctly", "[bond]") {
    // 10Y, 5% annual coupon, $1000 face. Under 30/360 the year fractions are
    // exact integers, anchored to the maturity date.
    Bond b{1000.0, 0.05, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
           DayCount::Thirty360};

    const auto& cfs = b.cashflows();
    REQUIRE(cfs.size() == 10);
    REQUIRE(cfs.front().date == Date{2021, 1, 1});
    REQUIRE(cfs.front().amount == Approx(50.0));
    REQUIRE(cfs.back().date == Date{2030, 1, 1});
    REQUIRE(cfs.back().amount == Approx(1050.0));  // last coupon + principal
}

TEST_CASE("Bond priced at its coupon rate is at par", "[bond]") {
    Bond b{1000.0, 0.05, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
           DayCount::Thirty360};
    REQUIRE(b.price_from_yield(0.05) == Approx(1000.0).margin(1e-9));
}

TEST_CASE("Bond priced above its coupon rate trades at a discount", "[bond]") {
    Bond b{1000.0, 0.05, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
           DayCount::Thirty360};

    const double price = b.price_from_yield(0.06);
    REQUIRE(price < 1000.0);

    // Independent closed-form check: c=50, F=1000, n=10, y=6%.
    const double expected = closed_form_price(50.0, 1000.0, 10, 0.06);
    REQUIRE(price == Approx(expected).epsilon(1e-12));
    REQUIRE(expected == Approx(926.3991294858642).margin(1e-9));  // hand value
}

TEST_CASE("Bond priced below its coupon rate trades at a premium", "[bond]") {
    Bond b{1000.0, 0.05, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
           DayCount::Thirty360};
    REQUIRE(b.price_from_yield(0.04) > 1000.0);
}

TEST_CASE("Semi-annual bond at par", "[bond]") {
    // 5Y, 5% coupon paid semi-annually -> 10 periods of 25, par at 5% yield.
    Bond b{1000.0, 0.05, Frequency::SemiAnnual, Date{2020, 1, 1},
           Date{2025, 1, 1}, DayCount::Thirty360};
    REQUIRE(b.cashflows().size() == 10);
    REQUIRE(b.price_from_yield(0.05) == Approx(1000.0).margin(1e-9));

    const double expected = closed_form_price(25.0, 1000.0, 10, 0.05 / 2.0);
    REQUIRE(b.price_from_yield(0.05) == Approx(expected).epsilon(1e-12));
}

TEST_CASE("Zero-coupon bond price equals face times discount factor", "[bond]") {
    // 5Y zero, $1000 face. Only one cashflow: principal at maturity.
    Bond z{1000.0, 0.0, Frequency::Annual, Date{2020, 1, 1}, Date{2025, 1, 1},
           DayCount::Thirty360};

    REQUIRE(z.cashflows().size() == 1);
    REQUIRE(z.cashflows().front().date == Date{2025, 1, 1});
    REQUIRE(z.cashflows().front().amount == Approx(1000.0));

    const double y = 0.05;
    const double expected = 1000.0 * std::pow(1.0 + y, -5.0);
    REQUIRE(z.price_from_yield(y) == Approx(expected).epsilon(1e-12));
}

TEST_CASE("Bond rejects bad construction", "[bond]") {
    REQUIRE_THROWS_AS(Bond(1000.0, 0.05, Frequency::Annual, Date{2030, 1, 1},
                           Date{2020, 1, 1}, DayCount::Thirty360),
                      std::invalid_argument);
}
