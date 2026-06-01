#include "fi/day_count.hpp"

#include <stdexcept>

namespace fi {

double year_fraction(const Date& d1, const Date& d2, DayCount dc) {
    switch (dc) {
        case DayCount::Act360:
            return static_cast<double>(days_between(d1, d2)) / 360.0;

        case DayCount::Act365:
            return static_cast<double>(days_between(d1, d2)) / 365.0;

        case DayCount::Thirty360: {
            // US (NASD) 30/360 bond basis.
            int y1 = d1.year(), y2 = d2.year();
            int m1 = static_cast<int>(d1.month()), m2 = static_cast<int>(d2.month());
            int dd1 = static_cast<int>(d1.day()), dd2 = static_cast<int>(d2.day());

            if (dd1 == 31) dd1 = 30;
            if (dd2 == 31 && dd1 == 30) dd2 = 30;

            double days = 360.0 * (y2 - y1) + 30.0 * (m2 - m1) + (dd2 - dd1);
            return days / 360.0;
        }
    }
    throw std::invalid_argument("year_fraction: unknown DayCount");
}

const char* to_string(DayCount dc) noexcept {
    switch (dc) {
        case DayCount::Act360:    return "Act/360";
        case DayCount::Act365:    return "Act/365";
        case DayCount::Thirty360: return "30/360";
    }
    return "Unknown";
}

}  // namespace fi
