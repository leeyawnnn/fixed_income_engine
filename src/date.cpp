#include "fi/date.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace fi {

namespace {

// Clamp a year_month + desired day to the last valid day of that month.
// e.g. (2024-02, day 31) -> 2024-02-29.
std::chrono::year_month_day clamp_to_month_end(std::chrono::year_month ym,
                                               std::chrono::day desired) {
    using namespace std::chrono;
    year_month_day candidate = ym / desired;
    if (candidate.ok()) {
        return candidate;
    }
    return ym / last;  // last day of the month
}

}  // namespace

Date::Date(int year, unsigned month, unsigned day) {
    using namespace std::chrono;
    ymd_ = std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day};
    if (!ymd_.ok()) {
        throw std::invalid_argument("Date: invalid calendar date " + to_string());
    }
}

Date::Date(std::chrono::year_month_day ymd) : ymd_(ymd) {
    if (!ymd_.ok()) {
        throw std::invalid_argument("Date: invalid calendar date " + to_string());
    }
}

Date Date::from_serial(std::int64_t serial_days) {
    using namespace std::chrono;
    sys_days sd{days{serial_days}};
    return Date{year_month_day{sd}};
}

int Date::year() const noexcept { return static_cast<int>(ymd_.year()); }
unsigned Date::month() const noexcept { return static_cast<unsigned>(ymd_.month()); }
unsigned Date::day() const noexcept { return static_cast<unsigned>(ymd_.day()); }

std::int64_t Date::serial() const noexcept {
    return std::chrono::sys_days{ymd_}.time_since_epoch().count();
}

Date Date::add_days(int n) const {
    using namespace std::chrono;
    return Date{year_month_day{sys() + days{n}}};
}

Date Date::add_months(int n) const {
    using namespace std::chrono;
    year_month ym = year_month{ymd_.year(), ymd_.month()} + months{n};
    return Date{clamp_to_month_end(ym, ymd_.day())};
}

Date Date::add_years(int n) const {
    using namespace std::chrono;
    year_month ym{ymd_.year() + years{n}, ymd_.month()};
    return Date{clamp_to_month_end(ym, ymd_.day())};
}

std::string Date::to_string() const {
    // Zero-padded ISO-8601; std::to_string keeps us off <format>'s libc++ quirks.
    auto pad2 = [](unsigned v) {
        std::string s = std::to_string(v);
        return s.size() < 2 ? "0" + s : s;
    };
    int y = year();
    std::string ys = std::to_string(y < 0 ? -y : y);
    while (ys.size() < 4) ys = "0" + ys;
    if (y < 0) ys = "-" + ys;
    return ys + "-" + pad2(month()) + "-" + pad2(day());
}

std::int64_t days_between(const Date& start, const Date& end) noexcept {
    return end.serial() - start.serial();
}

}  // namespace fi
