#pragma once

#include "fi/date.hpp"

namespace fi {

// Day-count conventions used to turn a pair of dates into a year fraction.
//
//   Act360     actual days / 360            (money-market: SOFR, EURIBOR cash)
//   Act365     actual days / 365            (Act/365 Fixed; GBP money market)
//   Thirty360  US (NASD) 30/360 bond basis  (US corporate / agency bonds)
enum class DayCount {
    Act360,
    Act365,
    Thirty360,
};

// Year fraction between d1 and d2 under the given convention.
// Sign follows (d2 - d1): negative if d2 precedes d1.
double year_fraction(const Date& d1, const Date& d2, DayCount dc);

// Human-readable name, e.g. for reports / debugging.
const char* to_string(DayCount dc) noexcept;

}  // namespace fi
