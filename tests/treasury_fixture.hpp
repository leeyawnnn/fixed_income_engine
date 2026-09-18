#pragma once

// Loads one dated column of the committed Treasury CMT file so tests run
// against real published par yields instead of numbers written into a test.
//
// The file is data/treasury_par_yields_2025.csv, fetched by
// scripts/fetch_treasury_curve.py; FI_DATA_DIR is set by CMake.

#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fi/bootstrap.hpp"
#include "fi/date.hpp"

namespace fi::testing {

struct CmtPoint {
    std::string tenor;       // "3M", "5Y"
    int months = 0;          // tenor in whole months, for building a maturity date
    double tau = 0.0;        // tenor in years
    double par_yield = 0.0;  // decimal, so 4.18% is 0.0418
};

struct CmtCurve {
    Date as_of;
    std::vector<CmtPoint> points;
};

// Tenor columns in file order, with their length in months. The 1.5-month
// column is deliberately absent: its series begins 2025-02-18, it is blank for
// most of the file, and 1.5 months is not a whole number of months to step a
// maturity date by.
inline const std::vector<std::pair<std::string, int>>& cmt_tenors() {
    static const std::vector<std::pair<std::string, int>> kTenors = {
        {"1M", 1},    {"2M", 2},    {"3M", 3},    {"4M", 4},  {"6M", 6},
        {"1Y", 12},   {"2Y", 24},   {"3Y", 36},   {"5Y", 60}, {"7Y", 84},
        {"10Y", 120}, {"20Y", 240}, {"30Y", 360},
    };
    return kTenors;
}

inline std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) fields.push_back(field);
    return fields;
}

// Reads the row for `iso_date`. Throws if the file or the date is missing, so a
// silently empty curve can never be mistaken for a passing test.
inline CmtCurve load_cmt_curve(const std::string& iso_date) {
    const std::string path = std::string(FI_DATA_DIR) + "/treasury_par_yields_2025.csv";
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("cannot open " + path);

    std::vector<std::string> header;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (header.empty()) {
            header = split_csv(line);
            continue;
        }
        const std::vector<std::string> fields = split_csv(line);
        if (fields.empty() || fields[0] != iso_date) continue;

        CmtCurve curve;
        curve.as_of = Date(std::stoi(iso_date.substr(0, 4)),
                           static_cast<unsigned>(std::stoul(iso_date.substr(5, 2))),
                           static_cast<unsigned>(std::stoul(iso_date.substr(8, 2))));
        for (const auto& [name, months] : cmt_tenors()) {
            std::size_t column = 0;
            while (column < header.size() && header[column] != name) ++column;
            if (column >= fields.size() || fields[column].empty()) continue;
            curve.points.push_back(CmtPoint{name, months, months / 12.0,
                                            std::stod(fields[column]) / 100.0});
        }
        return curve;
    }
    throw std::runtime_error("no row for " + iso_date + " in " + path);
}

// The CMT points as par-bond bootstrap instruments. US Treasury notes and bonds
// pay semiannually; 30/360 is used for the coupon accrual because for the
// regular periods of the hypothetical par bond a CMT rate describes - anchored
// on the same day of month, no stub - it gives exactly 0.5 per period, which is
// what Act/Act (ICMA) gives for a regular period too.
inline std::vector<BootstrapInstrument> cmt_instruments(const CmtCurve& curve) {
    std::vector<BootstrapInstrument> instruments;
    instruments.reserve(curve.points.size());
    for (const CmtPoint& point : curve.points) {
        instruments.emplace_back(ParBondQuote{curve.as_of.add_months(point.months),
                                              point.par_yield, Frequency::SemiAnnual,
                                              DayCount::Thirty360});
    }
    return instruments;
}

}  // namespace fi::testing
