#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "fi/bond.hpp"
#include "fi/ytm_solver.hpp"

using Catch::Approx;
using fi::Bond;
using fi::Date;
using fi::DayCount;
using fi::Frequency;
using fi::solve_ytm;

TEST_CASE("YTM round-trips the pricing function", "[ytm]") {
    Bond b{1000.0, 0.05, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
           DayCount::Thirty360};

    for (double y : {0.01, 0.03, 0.05, 0.07, 0.12}) {
        const double price = b.price_from_yield(y);
        auto r = solve_ytm(b, price);
        REQUIRE(r.converged);
        REQUIRE(r.root == Approx(y).margin(1e-9));
        REQUIRE(r.iterations <= 50);
    }
}

TEST_CASE("YTM round-trips a semi-annual bond", "[ytm]") {
    Bond b{1000.0, 0.04, Frequency::SemiAnnual, Date{2020, 1, 1},
           Date{2027, 1, 1}, DayCount::Thirty360};

    const double y = 0.055;
    const double price = b.price_from_yield(y);
    auto r = solve_ytm(b, price);
    REQUIRE(r.converged);
    REQUIRE(r.root == Approx(y).margin(1e-9));
}

TEST_CASE("Par bond solves to its coupon rate", "[ytm]") {
    Bond b{1000.0, 0.05, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
           DayCount::Thirty360};
    auto r = solve_ytm(b, 1000.0);
    REQUIRE(r.converged);
    REQUIRE(r.root == Approx(0.05).margin(1e-9));
}

TEST_CASE("Deep-discount, very low coupon bond still converges", "[ytm]") {
    // 30Y, 1% annual coupon priced to an 8% yield: a deeply discounted bond,
    // far from the coupon-rate initial guess.
    Bond b{1000.0, 0.01, Frequency::Annual, Date{2020, 1, 1}, Date{2050, 1, 1},
           DayCount::Thirty360};

    const double y = 0.08;
    const double price = b.price_from_yield(y);
    REQUIRE(price < 250.0);  // genuinely deep discount

    auto r = solve_ytm(b, price);
    REQUIRE(r.converged);
    REQUIRE(r.root == Approx(y).margin(1e-9));
}

TEST_CASE("Zero-coupon YTM matches the analytic yield", "[ytm]") {
    Bond z{1000.0, 0.0, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
           DayCount::Thirty360};

    const double y = 0.06;
    const double price = 1000.0 * std::pow(1.0 + y, -10.0);
    auto r = solve_ytm(z, price);
    REQUIRE(r.converged);
    REQUIRE(r.root == Approx(y).margin(1e-9));
}

TEST_CASE("Negative yield is recoverable for a deep premium", "[ytm]") {
    Bond b{1000.0, 0.05, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
           DayCount::Thirty360};

    const double y = -0.01;
    const double price = b.price_from_yield(y);
    REQUIRE(price > 1000.0);
    auto r = solve_ytm(b, price);
    REQUIRE(r.converged);
    REQUIRE(r.root == Approx(y).margin(1e-9));
}

TEST_CASE("SolverConfig controls iteration budget", "[ytm]") {
    Bond b{1000.0, 0.05, Frequency::Annual, Date{2020, 1, 1}, Date{2030, 1, 1},
           DayCount::Thirty360};

    // One iteration from a poor starting point should not be enough to hit a
    // 1e-12 residual tolerance.
    fi::SolverConfig tight{};
    tight.value_tolerance = 1e-12;
    tight.step_tolerance = 1e-16;
    tight.max_iterations = 1;
    auto r = solve_ytm(b, b.price_from_yield(0.20), tight);
    REQUIRE_FALSE(r.converged);
    REQUIRE(r.iterations == 1);
}
