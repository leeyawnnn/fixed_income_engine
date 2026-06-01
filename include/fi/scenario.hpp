#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fi/curve.hpp"
#include "fi/swap.hpp"

namespace fi {

// A rate scenario: a named shift applied to a curve's zero rates as a function
// of node tenor (in years). The shift is returned in absolute rate units
// (decimal), e.g. +25bp == +0.0025.
struct Scenario {
    std::string name;
    std::function<double(double tenor_years)> shift;
};

// --- Standard scenario builders (bp arguments are in basis points) ----------

// Uniform shift at every tenor.
Scenario parallel_scenario(double bp);

// Linear rotation: −|bp| at short_t rising to +|bp| at long_t (clamped beyond).
Scenario steepener_scenario(double bp, double short_t = 0.0, double long_t = 30.0);

// The mirror of the steepener: +|bp| short, −|bp| long.
Scenario flattener_scenario(double bp, double short_t = 0.0, double long_t = 30.0);

// Tent shape: −wing_bp at the wings (short_t, long_t), +belly_bp at belly_t.
Scenario butterfly_scenario(double belly_bp, double wing_bp, double short_t = 0.0,
                            double belly_t = 10.0, double long_t = 30.0);

// The roadmap's canonical set: parallel ±25/+100, steepener, flattener, butterfly.
std::vector<Scenario> standard_scenarios();

// --- Application and P&L ----------------------------------------------------

// A copy of `base` with each node's zero rate shifted by the scenario.
std::unique_ptr<Curve> apply_scenario(const Curve& base, const Scenario& s);

// Per-scenario P&L for a swap portfolio (single-curve self-discounting).
struct ScenarioPnL {
    std::string name;
    double base_pv = 0.0;
    double scenario_pv = 0.0;
    double pnl = 0.0;  // scenario_pv − base_pv
};

std::vector<ScenarioPnL> run_scenarios(const std::vector<Swap>& portfolio,
                                       const Curve& base,
                                       const std::vector<Scenario>& scenarios);

// Render a Markdown P&L table (the body of scenarios_report.md).
std::string scenarios_report_md(const std::vector<ScenarioPnL>& results);

}  // namespace fi
