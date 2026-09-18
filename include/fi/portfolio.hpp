#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "fi/bond.hpp"
#include "fi/curve.hpp"
#include "fi/date.hpp"
#include "fi/day_count.hpp"
#include "fi/money.hpp"
#include "fi/swap.hpp"

namespace fi {

// The portfolio definition and valuation layers, which is where Money lives.
//
// This is one of the two edges of the numerical core. A notional read out of a
// file is a declared quantity - somebody typed ten million dollars and meant
// exactly that - so it is held as Money and never drifts. A PV is a computed
// quantity and is a double throughout the pricing that produces it; it becomes
// Money only at the moment it is written into a report, with one explicit
// rounding, and every total is then summed in minor units so the parts add up
// to the whole exactly.

// A booked interest-rate swap, as declared rather than as priced.
struct SwapPosition {
    std::string id;
    Money notional;
    double fixed_rate = 0.0;
    SwapDirection direction = SwapDirection::Payer;
    int tenor_months = 0;
    Frequency fixed_frequency = Frequency::SemiAnnual;
    DayCount fixed_day_count = DayCount::Thirty360;
    Frequency float_frequency = Frequency::Quarterly;
    DayCount float_day_count = DayCount::Act360;

    // The pricing object. Crossing into the numerical core is where the
    // notional becomes a double, and it happens here and nowhere else.
    Swap to_swap(const Date& valuation_date) const;
};

struct Portfolio {
    Date valuation_date;
    Currency currency = Currency::USD;
    std::vector<SwapPosition> positions;

    std::vector<Swap> swaps() const;
};

// Parses the demo's portfolio file. Throws std::runtime_error on malformed
// input or an unsupported frequency.
Portfolio load_portfolio(const std::string& json_text);

// --- Valuation output --------------------------------------------------------

struct PositionValuation {
    std::string id;
    Money notional;
    Money pv;
    Money fixed_leg;
    Money floating_leg;
    double dv01 = 0.0;  // per basis point, in currency units
    double pv01 = 0.0;  // per basis point on the fixed rate
    double par_rate = 0.0;
    double rate_offset = 0.0;  // struck rate minus par rate
};

struct PortfolioValuation {
    Currency currency = Currency::USD;
    std::vector<PositionValuation> positions;
    Money total_pv;
    double total_dv01 = 0.0;
    double total_gamma = 0.0;  // convexity contribution per (basis point)^2
};

// Values every position off `discount` (single-curve, self-discounted). The
// total is the exact sum of the rounded position PVs, so a report's rows always
// add to its total - rounding the total separately would let them disagree by a
// cent.
PortfolioValuation value_portfolio(const Portfolio& portfolio, const Curve& discount);

}  // namespace fi
