#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "fi/date.hpp"

using fi::Date;

TEST_CASE("Date stores and reports calendar fields", "[date]") {
    Date d{2024, 2, 29};  // leap day
    REQUIRE(d.year() == 2024);
    REQUIRE(d.month() == 2);
    REQUIRE(d.day() == 29);
    REQUIRE(d.is_valid());
    REQUIRE(d.to_string() == "2024-02-29");
}

TEST_CASE("Date rejects impossible calendar dates", "[date]") {
    REQUIRE_THROWS_AS(Date(2023, 2, 29), std::invalid_argument);  // 2023 not a leap year
    REQUIRE_THROWS_AS(Date(2024, 13, 1), std::invalid_argument);
    REQUIRE_THROWS_AS(Date(2024, 4, 31), std::invalid_argument);  // April has 30 days
}

TEST_CASE("Serial round-trips through the Unix epoch", "[date]") {
    REQUIRE(Date(1970, 1, 1).serial() == 0);
    REQUIRE(Date(1970, 1, 2).serial() == 1);
    REQUIRE(Date(1969, 12, 31).serial() == -1);

    Date d{2024, 5, 31};
    REQUIRE(Date::from_serial(d.serial()) == d);
}

TEST_CASE("days_between counts actual calendar days", "[date]") {
    // 2024 is a leap year: Jan(31)+Feb(29)+Mar(31)+Apr(30)+May(31)+Jun(30)=182.
    REQUIRE(fi::days_between(Date{2024, 1, 1}, Date{2024, 7, 1}) == 182);
    // Full leap year is 366 days.
    REQUIRE(fi::days_between(Date{2024, 1, 1}, Date{2025, 1, 1}) == 366);
    // Full non-leap year is 365 days.
    REQUIRE(fi::days_between(Date{2023, 1, 1}, Date{2024, 1, 1}) == 365);
    // Reversed order is negative.
    REQUIRE(fi::days_between(Date{2024, 7, 1}, Date{2024, 1, 1}) == -182);
}

TEST_CASE("add_days uses the real calendar across month/year ends", "[date]") {
    REQUIRE(Date{2024, 2, 28}.add_days(1) == Date{2024, 2, 29});  // leap
    REQUIRE(Date{2023, 2, 28}.add_days(1) == Date{2023, 3, 1});   // non-leap
    REQUIRE(Date{2024, 12, 31}.add_days(1) == Date{2025, 1, 1});
    REQUIRE(Date{2024, 1, 1}.add_days(-1) == Date{2023, 12, 31});
}

TEST_CASE("add_months clamps to month end", "[date]") {
    // Jan 31 + 1 month -> Feb 29 (leap) / Feb 28 (non-leap).
    REQUIRE(Date{2024, 1, 31}.add_months(1) == Date{2024, 2, 29});
    REQUIRE(Date{2023, 1, 31}.add_months(1) == Date{2023, 2, 28});
    // Ordinary case, no clamp.
    REQUIRE(Date{2024, 1, 15}.add_months(6) == Date{2024, 7, 15});
    // Roll back over a year boundary.
    REQUIRE(Date{2024, 1, 31}.add_months(-2) == Date{2023, 11, 30});
}

TEST_CASE("add_years clamps the leap day", "[date]") {
    REQUIRE(Date{2024, 2, 29}.add_years(1) == Date{2025, 2, 28});
    REQUIRE(Date{2024, 2, 29}.add_years(4) == Date{2028, 2, 29});  // next leap year
    REQUIRE(Date{2024, 6, 15}.add_years(10) == Date{2034, 6, 15});
}

TEST_CASE("Date ordering is chronological", "[date]") {
    REQUIRE(Date{2024, 1, 1} < Date{2024, 1, 2});
    REQUIRE(Date{2023, 12, 31} < Date{2024, 1, 1});
    REQUIRE(Date{2024, 5, 31} == Date{2024, 5, 31});
    REQUIRE(Date{2024, 6, 1} > Date{2024, 5, 31});
}
