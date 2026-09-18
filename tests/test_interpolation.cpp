#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "fi/bootstrap.hpp"
#include "fi/curve.hpp"
#include "treasury_fixture.hpp"

using Catch::Approx;
using fi::bootstrap_curve;
using fi::BootstrapInstrument;
using fi::Curve;
using fi::DayCount;
using fi::Interpolation;
using fi::make_curve;
using fi::repricing_residual;

namespace {

const std::vector<Interpolation>& all_schemes() {
    static const std::vector<Interpolation> kSchemes = {
        Interpolation::LinearZero,
        Interpolation::LogLinearDiscount,
        Interpolation::MonotoneConvex,
    };
    return kSchemes;
}

}  // namespace

TEST_CASE("every scheme reproduces its node zero rates exactly", "[curve][interp]") {
    const fi::Date ref{2025, 12, 31};
    const std::vector<double> times = {0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0};
    const std::vector<double> zeros = {0.0367, 0.0359, 0.0348, 0.0347,
                                       0.0373, 0.0418, 0.0484};

    for (const Interpolation scheme : all_schemes()) {
        const auto curve = make_curve(scheme, ref, DayCount::Act365, times, zeros);
        INFO("scheme: " << fi::to_string(scheme));
        for (std::size_t i = 0; i < times.size(); ++i) {
            REQUIRE(curve->zero_rate(times[i]) == Approx(zeros[i]).margin(1e-13));
        }
        REQUIRE(curve->discount(0.0) == Approx(1.0).margin(1e-15));
    }
}

TEST_CASE("every scheme reprices the instruments it was bootstrapped from",
          "[curve][interp][bootstrap]") {
    // The claim that matters: whichever interpolation is chosen, the curve
    // returns the market quotes it was built from. Residuals should be at the
    // level of double rounding, not merely small.
    const auto cmt = fi::testing::load_cmt_curve("2025-12-31");
    const auto instruments = fi::testing::cmt_instruments(cmt);
    REQUIRE(instruments.size() == 13);

    for (const Interpolation scheme : all_schemes()) {
        INFO("scheme: " << fi::to_string(scheme));
        const auto curve =
            bootstrap_curve(cmt.as_of, DayCount::Act365, instruments, scheme);
        for (const BootstrapInstrument& instrument : instruments) {
            const double residual_bp = repricing_residual(*curve, instrument) * 1e4;
            INFO("instrument: " << fi::instrument_label(instrument, cmt.as_of)
                                << "  residual_bp: " << residual_bp);
            REQUIRE(std::abs(residual_bp) < 1e-8);
        }
    }
}

TEST_CASE("schemes agree at nodes and disagree between them", "[curve][interp]") {
    // If the three schemes produced the same curve there would be nothing to
    // compare, so pin the disagreement rather than assuming it.
    const auto cmt = fi::testing::load_cmt_curve("2025-12-31");
    const auto instruments = fi::testing::cmt_instruments(cmt);

    std::vector<std::unique_ptr<Curve>> curves;
    for (const Interpolation scheme : all_schemes()) {
        curves.push_back(
            bootstrap_curve(cmt.as_of, DayCount::Act365, instruments, scheme));
    }

    // 4Y sits between the 3Y and 5Y nodes, where the schemes are free to differ.
    const double off_node = 4.0;
    double max_gap = 0.0;
    for (std::size_t i = 1; i < curves.size(); ++i) {
        max_gap = std::max(max_gap, std::abs(curves[i]->zero_rate(off_node) -
                                             curves[0]->zero_rate(off_node)));
    }
    REQUIRE(max_gap > 1e-6);  // they really are different curves
    REQUIRE(max_gap < 1e-3);  // but not wildly so: under 10bp in the zero rate
}

TEST_CASE("log-linear forwards are a step function, monotone convex is not",
          "[curve][interp]") {
    // This is the whole argument for the scheme. Log-linear interpolation makes
    // the forward constant within an interval and jump at each node; monotone
    // convex makes it continuous.
    const auto cmt = fi::testing::load_cmt_curve("2025-12-31");
    const auto instruments = fi::testing::cmt_instruments(cmt);

    const auto step = bootstrap_curve(cmt.as_of, DayCount::Act365, instruments,
                                      Interpolation::LogLinearDiscount);
    const auto smooth = bootstrap_curve(cmt.as_of, DayCount::Act365, instruments,
                                        Interpolation::MonotoneConvex);

    // Node times are Act/365 year fractions to real calendar dates, so the 5Y
    // node is at 5.0027, not 5.0. Take them from the curve rather than assuming.
    const std::vector<double>& nodes = step->node_times();
    const double five_year = nodes[nodes.size() - 5];  // ..., 5Y, 7Y, 10Y, 20Y, 30Y
    const double three_year = nodes[nodes.size() - 6];
    REQUIRE(five_year > three_year);

    // Anywhere strictly inside (3Y, 5Y): the step curve's forward does not move.
    const double a = three_year + 0.25 * (five_year - three_year);
    const double b = three_year + 0.75 * (five_year - three_year);
    REQUIRE(step->instantaneous_forward(a) ==
            Approx(step->instantaneous_forward(b)).margin(1e-9));

    // The monotone convex forward does move across the same span.
    REQUIRE(std::abs(smooth->instantaneous_forward(a) -
                     smooth->instantaneous_forward(b)) > 1e-5);

    // And it is continuous across the 5Y node, where the step curve jumps.
    const double eps = 1e-4;
    const double jump_step = std::abs(step->instantaneous_forward(five_year + eps) -
                                      step->instantaneous_forward(five_year - eps));
    const double jump_smooth = std::abs(smooth->instantaneous_forward(five_year + eps) -
                                        smooth->instantaneous_forward(five_year - eps));
    INFO("step jump " << jump_step << ", smooth jump " << jump_smooth);
    REQUIRE(jump_step > 1e-4);
    REQUIRE(jump_smooth < 1e-5);
}

TEST_CASE("monotone convex forwards stay non-negative on the Treasury curve",
          "[curve][interp]") {
    const auto cmt = fi::testing::load_cmt_curve("2025-12-31");
    const auto instruments = fi::testing::cmt_instruments(cmt);
    const auto curve = bootstrap_curve(cmt.as_of, DayCount::Act365, instruments,
                                       Interpolation::MonotoneConvex);

    for (int step = 1; step < 3000; ++step) {
        const double t = step * 0.01;
        INFO("t = " << t);
        REQUIRE(curve->instantaneous_forward(t) >= 0.0);
    }
}

TEST_CASE("the positivity collar cannot rescue a negative discrete forward",
          "[curve][interp]") {
    // Hagan-West's collar bounds the forward at each *node* into
    // [0, 2*min(adjacent discrete forwards)]. It cannot make the forward
    // non-negative across an interval whose discrete forward is itself
    // negative, because the interval's average forward is fixed by the two node
    // zeros: no interpolant can average to -1% and stay above zero. This pins
    // that limitation rather than claiming a guarantee the scheme does not give.
    const fi::Date ref{2025, 12, 31};
    const std::vector<double> times = {1.0, 2.0, 3.0};
    const std::vector<double> zeros = {0.05, 0.02, 0.01};  // z*t falls after 1Y

    const auto curve =
        make_curve(Interpolation::MonotoneConvex, ref, DayCount::Act365, times, zeros);

    // The (1Y, 2Y) discrete forward implied by these zeros.
    const double discrete =
        (zeros[1] * times[1] - zeros[0] * times[0]) / (times[1] - times[0]);
    REQUIRE(discrete < 0.0);
    REQUIRE(curve->forward_rate(1.0, 2.0) == Approx(discrete).margin(1e-12));

    double most_negative = 0.0;
    for (int step = 1; step < 20; ++step) {
        const double t = 1.0 + step * 0.05;
        most_negative = std::min(most_negative, curve->instantaneous_forward(t));
    }
    REQUIRE(most_negative < 0.0);

    // Node reproduction is unaffected, which is what the bootstrap relies on.
    for (std::size_t i = 0; i < times.size(); ++i) {
        REQUIRE(curve->zero_rate(times[i]) == Approx(zeros[i]).margin(1e-12));
    }
}

TEST_CASE("a single-node curve is flat under every scheme", "[curve][interp]") {
    const fi::Date ref{2025, 12, 31};
    for (const Interpolation scheme : all_schemes()) {
        INFO("scheme: " << fi::to_string(scheme));
        const auto curve = make_curve(scheme, ref, DayCount::Act365, {2.0}, {0.04});
        REQUIRE(curve->zero_rate(2.0) == Approx(0.04).margin(1e-13));
        REQUIRE(curve->discount(1.0) == Approx(std::exp(-0.04)).margin(1e-9));
    }
}
