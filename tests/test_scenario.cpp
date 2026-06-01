#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "fi/bootstrap.hpp"
#include "fi/curve.hpp"
#include "fi/risk.hpp"
#include "fi/scenario.hpp"
#include "fi/swap.hpp"

using Catch::Approx;
using namespace fi;

namespace {
std::unique_ptr<Curve> sample_curve(const Date& ref) {
    std::vector<BootstrapInstrument> instruments{
        DepositQuote{Date{2024, 7, 1}, 0.050, DayCount::Act360},
        SwapQuote{Date{2026, 1, 1}, 0.052, Frequency::SemiAnnual, DayCount::Thirty360},
        SwapQuote{Date{2029, 1, 1}, 0.054, Frequency::SemiAnnual, DayCount::Thirty360},
        SwapQuote{Date{2034, 1, 1}, 0.056, Frequency::SemiAnnual, DayCount::Thirty360},
    };
    return bootstrap_curve(ref, DayCount::Act365, instruments);
}

Swap payer(const Date& ref, const Date& mat, double rate) {
    return Swap{1e7,
                rate,
                SwapDirection::Payer,
                ref,
                mat,
                Frequency::SemiAnnual,
                DayCount::Thirty360,
                Frequency::Quarterly,
                DayCount::Act360};
}
}  // namespace

TEST_CASE("Scenario shift shapes are correct", "[scenario]") {
    auto par = parallel_scenario(100.0);
    REQUIRE(par.shift(0.5) == Approx(0.01));
    REQUIRE(par.shift(30.0) == Approx(0.01));

    auto steep = steepener_scenario(25.0);  // short −, long +
    REQUIRE(steep.shift(0.0) == Approx(-0.0025));
    REQUIRE(steep.shift(30.0) == Approx(0.0025));
    REQUIRE(steep.shift(15.0) == Approx(0.0));  // midpoint

    auto flat = flattener_scenario(25.0);  // short +, long −
    REQUIRE(flat.shift(0.0) == Approx(0.0025));
    REQUIRE(flat.shift(30.0) == Approx(-0.0025));

    auto fly = butterfly_scenario(25.0, 12.5);  // belly +, wings −
    REQUIRE(fly.shift(0.0) == Approx(-0.00125));
    REQUIRE(fly.shift(10.0) == Approx(0.0025));
    REQUIRE(fly.shift(30.0) == Approx(-0.00125));
}

TEST_CASE("apply_scenario shifts node zero rates", "[scenario]") {
    const Date ref{2024, 1, 1};
    auto curve = sample_curve(ref);
    auto bumped = apply_scenario(*curve, parallel_scenario(100.0));

    const auto& base_z = curve->node_zero_rates();
    const auto& bump_z = bumped->node_zero_rates();
    REQUIRE(bump_z.size() == base_z.size());
    for (std::size_t i = 0; i < base_z.size(); ++i) {
        REQUIRE(bump_z[i] == Approx(base_z[i] + 0.01));
    }
}

TEST_CASE("Parallel scenario P&L agrees with DV01 sign and order", "[scenario]") {
    const Date ref{2024, 1, 1};
    auto curve = sample_curve(ref);
    std::vector<Swap> book{payer(ref, Date{2031, 1, 1}, 0.055)};

    auto results = run_scenarios(book, *curve,
                                 {parallel_scenario(25.0), parallel_scenario(-25.0)});
    // Payer gains when rates rise.
    REQUIRE(results[0].pnl > 0.0);
    REQUIRE(results[1].pnl < 0.0);
    // P&L is reported as scenario_pv − base_pv.
    REQUIRE(results[0].pnl == Approx(results[0].scenario_pv - results[0].base_pv));
}

TEST_CASE("Small parallel P&L tracks the analytic DV01", "[scenario]") {
    const Date ref{2024, 1, 1};
    auto curve = sample_curve(ref);
    Swap s = payer(ref, Date{2031, 1, 1}, 0.055);

    auto results = run_scenarios({s}, *curve, {parallel_scenario(1.0)});
    const double dv01 = swap_dv01(s, *curve);  // central, per 1bp
    // Forward-difference P&L vs central DV01: agree to ~1% for a 1bp move.
    REQUIRE(results[0].pnl == Approx(dv01).epsilon(0.02));
}

TEST_CASE("Standard scenario set and report render", "[scenario]") {
    const Date ref{2024, 1, 1};
    auto curve = sample_curve(ref);
    std::vector<Swap> book{payer(ref, Date{2029, 1, 1}, 0.054),
                           payer(ref, Date{2034, 1, 1}, 0.056)};

    auto scenarios = standard_scenarios();
    REQUIRE(scenarios.size() == 6);

    auto results = run_scenarios(book, *curve, scenarios);
    REQUIRE(results.size() == 6);

    const std::string md = scenarios_report_md(results);
    REQUIRE(md.find("# Scenario P&L Report") != std::string::npos);
    REQUIRE(md.find("Parallel +100.00bp") != std::string::npos);
    REQUIRE(md.find("Butterfly") != std::string::npos);
    REQUIRE(md.find("| P&L |") != std::string::npos);
}
