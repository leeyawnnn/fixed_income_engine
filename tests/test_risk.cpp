#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "fi/bond.hpp"
#include "fi/risk.hpp"

using Catch::Approx;
using fi::Bond;
using fi::Date;
using fi::DayCount;
using fi::Frequency;

TEST_CASE("Zero-coupon risk measures match closed forms", "[risk]") {
    // 10Y annual-compounded zero. Closed forms (m = 1, τ = T = 10, y = 5%):
    //   D_mac = T = 10
    //   D_mod = T / (1+y) = 10 / 1.05
    //   C     = T(T+1) / (1+y)^2 = 110 / 1.05^2
    Bond z{1000.0, 0.0, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
           DayCount::Thirty360};
    const double y = 0.05;
    auto r = fi::risk_measures(z, y);

    REQUIRE(r.price == Approx(1000.0 * std::pow(1.05, -10.0)).epsilon(1e-12));
    REQUIRE(r.macaulay_duration == Approx(10.0).margin(1e-9));
    REQUIRE(r.modified_duration == Approx(10.0 / 1.05).margin(1e-9));
    REQUIRE(r.convexity == Approx(110.0 / (1.05 * 1.05)).margin(1e-7));
}

TEST_CASE("Modified duration and DV01 satisfy their definitions", "[risk]") {
    Bond b{1000.0, 0.05, Frequency::SemiAnnual, Date{2020, 1, 1},
           Date{2030, 1, 1}, DayCount::Thirty360};
    const double y = 0.045;
    auto r = fi::risk_measures(b, y);

    const double m = 2.0;
    REQUIRE(r.modified_duration ==
            Approx(r.macaulay_duration / (1.0 + y / m)).epsilon(1e-12));
    REQUIRE(r.dv01 == Approx(r.modified_duration * r.price * 1e-4).epsilon(1e-12));
}

TEST_CASE("Analytic DV01 matches finite-difference DV01 to 1e-6", "[risk]") {
    // Across coupons, maturities and yields.
    struct Case { double coupon; int years; double y; Frequency f; };
    const Case cases[] = {
        {0.05, 10, 0.05, Frequency::Annual},
        {0.03, 10, 0.06, Frequency::SemiAnnual},
        {0.07, 5, 0.04, Frequency::SemiAnnual},
        {0.0, 30, 0.08, Frequency::Annual},   // long zero, largest curvature
    };
    for (const auto& c : cases) {
        Bond b{1000.0, c.coupon, c.f, Date{2020, 1, 1},
               Date{2020 + c.years, 1, 1}, DayCount::Thirty360};
        const double analytic = fi::dv01(b, c.y);
        const double fd = fi::dv01_finite_difference(b, c.y);
        REQUIRE(analytic == Approx(fd).margin(1e-6));
    }
}

TEST_CASE("Convexity matches a finite-difference second derivative", "[risk]") {
    Bond b{1000.0, 0.05, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
           DayCount::Thirty360};
    const double y = 0.05;
    const double h = 1e-4;  // small enough that O(h^2) truncation stays tiny
    const double p0 = b.price_from_yield(y);
    const double pu = b.price_from_yield(y + h);
    const double pd = b.price_from_yield(y - h);
    const double fd_convexity = (pu - 2.0 * p0 + pd) / (h * h * p0);

    REQUIRE(fi::convexity(b, y) == Approx(fd_convexity).margin(1e-3));
}

TEST_CASE("Higher-coupon bonds have lower duration", "[risk]") {
    // Same maturity and yield; only the coupon differs.
    Bond low{1000.0, 0.03, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
             DayCount::Thirty360};
    Bond high{1000.0, 0.08, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
              DayCount::Thirty360};
    const double y = 0.05;

    REQUIRE(fi::macaulay_duration(high, y) < fi::macaulay_duration(low, y));
    REQUIRE(fi::modified_duration(high, y) < fi::modified_duration(low, y));
    // A zero of the same maturity has the maximum duration (= 10).
    Bond zero{1000.0, 0.0, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
              DayCount::Thirty360};
    REQUIRE(fi::macaulay_duration(zero, y) > fi::macaulay_duration(low, y));
}
