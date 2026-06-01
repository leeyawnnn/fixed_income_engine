#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <string>

namespace fi {

// A calendar date, wrapping std::chrono::year_month_day (C++20).
//
// All date arithmetic goes through chrono's calendar so we never do naive
// integer-offset math. Differences are measured in *actual* days via the
// proleptic Gregorian calendar; day-count conventions live in day_count.hpp.
class Date {
public:
    Date() = default;

    // Construct from calendar fields. Throws std::invalid_argument if the
    // (year, month, day) triple is not a real calendar date (e.g. Feb 30).
    Date(int year, unsigned month, unsigned day);

    explicit Date(std::chrono::year_month_day ymd);

    // Construct from a serial day number: days since the Unix epoch
    // (1970-01-01 == 0). Useful for storage and round-tripping.
    static Date from_serial(std::int64_t serial_days);

    int year() const noexcept;
    unsigned month() const noexcept;
    unsigned day() const noexcept;

    std::chrono::year_month_day ymd() const noexcept { return ymd_; }
    std::chrono::sys_days sys() const noexcept { return std::chrono::sys_days{ymd_}; }

    // Days since 1970-01-01 (can be negative for earlier dates).
    std::int64_t serial() const noexcept;

    bool is_valid() const noexcept { return ymd_.ok(); }

    // Calendar arithmetic. add_months / add_years clamp the day-of-month to the
    // last valid day of the target month (e.g. Jan 31 + 1 month -> Feb 28/29).
    Date add_days(int n) const;
    Date add_months(int n) const;
    Date add_years(int n) const;

    // ISO-8601 "YYYY-MM-DD".
    std::string to_string() const;

    friend bool operator==(const Date& a, const Date& b) noexcept {
        return a.serial() == b.serial();
    }
    friend std::strong_ordering operator<=>(const Date& a, const Date& b) noexcept {
        return a.serial() <=> b.serial();
    }

private:
    std::chrono::year_month_day ymd_{};
};

// Actual calendar days from `start` to `end` (end - start). Negative if
// end precedes start.
std::int64_t days_between(const Date& start, const Date& end) noexcept;

}  // namespace fi
