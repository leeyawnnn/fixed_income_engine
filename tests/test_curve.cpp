#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "fi/curve.hpp"

using Catch::Approx;
using fi::Curve;
using fi::Date;
using fi::DayCount;
using fi::LinearInterpCurve;
using fi::LogLinearCurve;

namespace {
const Date kRef{2020, 1, 1};
const std::vector<double> kTimes{1.0, 2.0};
const std::vector<double> kZeros{0.02, 0.03};  // continuously compounded
}  // namespace

TEST_CASE("Linear curve reproduces nodes and interpolates zero rates", "[curve]") {
    LinearInterpCurve c{kRef, DayCount::Thirty360, kTimes, kZeros};

    // At the nodes: DF = exp(-z*t).
    REQUIRE(c.discount(1.0) == Approx(std::exp(-0.02 * 1.0)));
    REQUIRE(c.discount(2.0) == Approx(std::exp(-0.03 * 2.0)));

    // Midpoint: z(1.5) = 0.025 (linear), DF = exp(-0.025*1.5) = exp(-0.0375).
    REQUIRE(c.zero_rate(1.5) == Approx(0.025));
    REQUIRE(c.discount(1.5) == Approx(std::exp(-0.0375)));

    // Continuously-compounded forward over [1,2]:
    //   f = (ln DF(1) - ln DF(2)) / 1 = (-0.02 - (-0.06)) = 0.04.
    REQUIRE(c.forward_rate(1.0, 2.0) == Approx(0.04));

    REQUIRE(c.discount(0.0) == Approx(1.0));
}

TEST_CASE("Log-linear curve interpolates log discount factors", "[curve]") {
    LogLinearCurve c{kRef, DayCount::Thirty360, kTimes, kZeros};

    // Nodes match the linear curve exactly.
    REQUIRE(c.discount(1.0) == Approx(std::exp(-0.02)));
    REQUIRE(c.discount(2.0) == Approx(std::exp(-0.06)));

    // Midpoint: ln DF(1.5) = 0.5*(-0.02) + 0.5*(-0.06) = -0.04.
    REQUIRE(c.discount(1.5) == Approx(std::exp(-0.04)));
    REQUIRE(c.zero_rate(1.5) == Approx(0.04 / 1.5));

    // Forward is constant within a segment for a log-linear curve.
    REQUIRE(c.forward_rate(1.0, 1.5) == Approx(0.04));
    REQUIRE(c.forward_rate(1.5, 2.0) == Approx(0.04));
}

TEST_CASE("Linear and log-linear differ between nodes but agree at them", "[curve]") {
    LinearInterpCurve lin{kRef, DayCount::Thirty360, kTimes, kZeros};
    LogLinearCurve logl{kRef, DayCount::Thirty360, kTimes, kZeros};

    REQUIRE(lin.discount(1.0) == Approx(logl.discount(1.0)));
    REQUIRE(lin.discount(2.0) == Approx(logl.discount(2.0)));
    // exp(-0.0375) != exp(-0.04)
    REQUIRE(lin.discount(1.5) != Approx(logl.discount(1.5)));
    REQUIRE(lin.discount(1.5) > logl.discount(1.5));
}

TEST_CASE("Flat zero-rate extrapolation outside the node range", "[curve]") {
    LinearInterpCurve c{kRef, DayCount::Thirty360, kTimes, kZeros};

    // Before first node: hold z = 0.02.
    REQUIRE(c.discount(0.5) == Approx(std::exp(-0.02 * 0.5)));
    // After last node: hold z = 0.03.
    REQUIRE(c.discount(3.0) == Approx(std::exp(-0.03 * 3.0)));

    // Both curves agree outside the range (same flat-zero extrapolation).
    LogLinearCurve l{kRef, DayCount::Thirty360, kTimes, kZeros};
    REQUIRE(c.discount(3.0) == Approx(l.discount(3.0)));
}

TEST_CASE("Date-based lookups use the curve day count", "[curve]") {
    // Under 30/360, 2020-01-01 -> 2021-01-01 is exactly 1.0 years.
    LinearInterpCurve c{kRef, DayCount::Thirty360, kTimes, kZeros};
    REQUIRE(c.discount(Date{2021, 1, 1}) == Approx(c.discount(1.0)));
    REQUIRE(c.zero_rate(Date{2022, 1, 1}) == Approx(c.zero_rate(2.0)));
}

TEST_CASE("Polymorphic use through the base interface", "[curve]") {
    std::unique_ptr<Curve> c =
        std::make_unique<LogLinearCurve>(kRef, DayCount::Thirty360, kTimes, kZeros);
    REQUIRE(c->discount(1.5) == Approx(std::exp(-0.04)));
    REQUIRE(c->discount(Date{2021, 1, 1}) == Approx(std::exp(-0.02)));
}

TEST_CASE("Curve construction validates its nodes", "[curve]") {
    REQUIRE_THROWS_AS(
        LinearInterpCurve(kRef, DayCount::Thirty360, {1.0, 2.0}, {0.02}),
        std::invalid_argument);  // size mismatch
    REQUIRE_THROWS_AS(
        LinearInterpCurve(kRef, DayCount::Thirty360, {2.0, 1.0}, {0.03, 0.02}),
        std::invalid_argument);  // not increasing
    REQUIRE_THROWS_AS(
        LinearInterpCurve(kRef, DayCount::Thirty360, {0.0, 1.0}, {0.02, 0.03}),
        std::invalid_argument);  // non-positive time
}
