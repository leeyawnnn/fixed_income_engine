#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "fi/day_count.hpp"

using Catch::Approx;
using fi::Date;
using fi::DayCount;
using fi::year_fraction;

// All expected values below are computed by hand from the day-count definitions.

TEST_CASE("Act/360 year fractions", "[day_count]") {
    // 2024-01-01 -> 2024-07-01 is 182 actual days (2024 is leap).
    REQUIRE(year_fraction(Date{2024, 1, 1}, Date{2024, 7, 1}, DayCount::Act360) ==
            Approx(182.0 / 360.0));
    // Full leap year: 366 / 360.
    REQUIRE(year_fraction(Date{2024, 1, 1}, Date{2025, 1, 1}, DayCount::Act360) ==
            Approx(366.0 / 360.0));
    // A 90-day money-market period: 2024-03-01 -> 2024-05-30 = 90 days.
    REQUIRE(year_fraction(Date{2024, 3, 1}, Date{2024, 5, 30}, DayCount::Act360) ==
            Approx(90.0 / 360.0));
}

TEST_CASE("Act/365 year fractions", "[day_count]") {
    REQUIRE(year_fraction(Date{2024, 1, 1}, Date{2024, 7, 1}, DayCount::Act365) ==
            Approx(182.0 / 365.0));
    // Non-leap full year is exactly 1.0.
    REQUIRE(year_fraction(Date{2023, 1, 1}, Date{2024, 1, 1}, DayCount::Act365) ==
            Approx(1.0));
    // Leap full year is 366/365 (slightly more than 1).
    REQUIRE(year_fraction(Date{2024, 1, 1}, Date{2025, 1, 1}, DayCount::Act365) ==
            Approx(366.0 / 365.0));
}

TEST_CASE("30/360 year fractions are calendar-regular", "[day_count]") {
    // 6 whole months: 30*6/360 = 0.5 exactly, regardless of actual day counts.
    REQUIRE(year_fraction(Date{2024, 1, 1}, Date{2024, 7, 1}, DayCount::Thirty360) ==
            Approx(0.5));
    // Full year is exactly 1.0 under 30/360 (even a leap year).
    REQUIRE(year_fraction(Date{2024, 1, 1}, Date{2025, 1, 1}, DayCount::Thirty360) ==
            Approx(1.0));
    // One month: 30/360.
    REQUIRE(year_fraction(Date{2024, 4, 15}, Date{2024, 5, 15}, DayCount::Thirty360) ==
            Approx(30.0 / 360.0));
}

TEST_CASE("30/360 end-of-month day adjustments", "[day_count]") {
    // d1 = 31 -> 30. 2024-01-31 -> 2024-04-30:
    //   360*0 + 30*(4-1) + (30-30) = 90  ->  90/360.
    REQUIRE(year_fraction(Date{2024, 1, 31}, Date{2024, 4, 30}, DayCount::Thirty360) ==
            Approx(90.0 / 360.0));
    // d1 -> 30 then d2 = 31 -> 30. 2024-01-31 -> 2024-07-31:
    //   30*(7-1) + (30-30) = 180  ->  0.5.
    REQUIRE(year_fraction(Date{2024, 1, 31}, Date{2024, 7, 31}, DayCount::Thirty360) ==
            Approx(0.5));
    // d2 = 31 but d1 != 30 (no second adjustment). 2024-01-15 -> 2024-03-31:
    //   30*(3-1) + (31-15) = 60 + 16 = 76  ->  76/360.
    REQUIRE(year_fraction(Date{2024, 1, 15}, Date{2024, 3, 31}, DayCount::Thirty360) ==
            Approx(76.0 / 360.0));
}

TEST_CASE("Year fraction sign follows date order", "[day_count]") {
    REQUIRE(year_fraction(Date{2024, 7, 1}, Date{2024, 1, 1}, DayCount::Act360) ==
            Approx(-182.0 / 360.0));
    REQUIRE(year_fraction(Date{2024, 7, 1}, Date{2024, 1, 1}, DayCount::Thirty360) ==
            Approx(-0.5));
}

TEST_CASE("DayCount names", "[day_count]") {
    REQUIRE(std::string(fi::to_string(DayCount::Act360)) == "Act/360");
    REQUIRE(std::string(fi::to_string(DayCount::Act365)) == "Act/365");
    REQUIRE(std::string(fi::to_string(DayCount::Thirty360)) == "30/360");
}
