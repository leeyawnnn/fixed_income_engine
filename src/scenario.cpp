#include "fi/scenario.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace fi {

namespace {

double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }

std::string fmt(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << v;
    return os.str();
}

}  // namespace

Scenario parallel_scenario(double bp) {
    const double d = bp * 1e-4;
    return {"Parallel " + (bp >= 0 ? std::string("+") : std::string()) +
                fmt(bp) + "bp",
            [d](double) { return d; }};
}

Scenario steepener_scenario(double bp, double short_t, double long_t) {
    const double d = std::abs(bp) * 1e-4;
    return {"Steepener (-" + fmt(std::abs(bp)) + "/+" + fmt(std::abs(bp)) + "bp)",
            [d, short_t, long_t](double t) {
                const double w = clamp01((t - short_t) / (long_t - short_t));
                return -d + 2.0 * d * w;  // −d at short, +d at long
            }};
}

Scenario flattener_scenario(double bp, double short_t, double long_t) {
    const double d = std::abs(bp) * 1e-4;
    return {"Flattener (+" + fmt(std::abs(bp)) + "/-" + fmt(std::abs(bp)) + "bp)",
            [d, short_t, long_t](double t) {
                const double w = clamp01((t - short_t) / (long_t - short_t));
                return d - 2.0 * d * w;  // +d at short, −d at long
            }};
}

Scenario butterfly_scenario(double belly_bp, double wing_bp, double short_t,
                            double belly_t, double long_t) {
    const double belly = belly_bp * 1e-4;
    const double wing = wing_bp * 1e-4;
    return {"Butterfly (belly +" + fmt(belly_bp) + "/wings -" + fmt(wing_bp) + "bp)",
            [belly, wing, short_t, belly_t, long_t](double t) {
                if (t <= belly_t) {
                    const double w = clamp01((t - short_t) / (belly_t - short_t));
                    return -wing + (belly + wing) * w;  // −wing → +belly
                }
                const double w = clamp01((t - belly_t) / (long_t - belly_t));
                return belly - (belly + wing) * w;  // +belly → −wing
            }};
}

std::vector<Scenario> standard_scenarios() {
    return {parallel_scenario(25.0),  parallel_scenario(100.0),
            parallel_scenario(-25.0), steepener_scenario(25.0),
            flattener_scenario(25.0), butterfly_scenario(25.0, 12.5)};
}

std::unique_ptr<Curve> apply_scenario(const Curve& base, const Scenario& s) {
    const std::vector<double>& times = base.node_times();
    std::vector<double> zeros = base.node_zero_rates();
    for (std::size_t i = 0; i < zeros.size(); ++i) {
        zeros[i] += s.shift(times[i]);
    }
    return base.with_zero_rates(std::move(zeros));
}

std::vector<ScenarioPnL> run_scenarios(const std::vector<Swap>& portfolio,
                                       const Curve& base,
                                       const std::vector<Scenario>& scenarios) {
    double base_pv = 0.0;
    for (const Swap& s : portfolio) base_pv += s.pv(base);

    std::vector<ScenarioPnL> out;
    out.reserve(scenarios.size());
    for (const Scenario& sc : scenarios) {
        const auto bumped = apply_scenario(base, sc);
        double pv = 0.0;
        for (const Swap& s : portfolio) pv += s.pv(*bumped);
        out.push_back({sc.name, base_pv, pv, pv - base_pv});
    }
    return out;
}

std::string scenarios_report_md(const std::vector<ScenarioPnL>& results) {
    std::ostringstream os;
    os << "# Scenario P&L Report\n\n";
    os << "| Scenario | Base PV | Scenario PV | P&L |\n";
    os << "|---|---:|---:|---:|\n";
    for (const ScenarioPnL& r : results) {
        os << "| " << r.name << " | " << fmt(r.base_pv) << " | "
           << fmt(r.scenario_pv) << " | " << fmt(r.pnl) << " |\n";
    }
    return os.str();
}

}  // namespace fi
