#pragma once

#include "fi/date.hpp"

namespace fi {

// A single fixed (deterministic) cash payment on a known date.
//
// `amount` is in currency units (e.g. dollars). Doubles are fine here because
// cashflows feed directly into numerical pricing; any *persistent* monetary
// state (portfolio files, scenario reports) is stored as int64 cents per the
// project's money convention.
//
// Bonds only need fixed cashflows, so a plain struct is the right tool (no
// inheritance). Floating/forward-rate legs in the swap phases are computed from
// the curve rather than stored as a cashflow variant.
struct Cashflow {
    Date date;
    double amount;
};

}  // namespace fi
