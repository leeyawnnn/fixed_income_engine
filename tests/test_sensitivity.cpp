#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numeric>
#include <vector>

#include "fi/bootstrap.hpp"
#include "fi/risk.hpp"
#include "fi/swap.hpp"

using Catch::Approx;
using fi::bootstrap_curve;
using fi::BootstrapInstrument;
using fi::Date;
using fi::DayCount;
using fi::DepositQuote;
using fi::Frequency;
using fi::Swap;
using fi::SwapDirection;
using fi::SwapQuote;

namespace {
std::unique_ptr<fi::Curve> sample_curve(const Date& ref) {
    std::vector<BootstrapInstrument> instruments{
        DepositQuote{Date{2024, 7, 1}, 0.050, DayCount::Act360},
        SwapQuote{Date{2025, 1, 1}, 0.051, Frequency::SemiAnnual, DayCount::Thirty360},
        SwapQuote{Date{2026, 1, 1}, 0.052, Frequency::SemiAnnual, DayCount::Thirty360},
        SwapQuote{Date{2029, 1, 1}, 0.054, Frequency::SemiAnnual, DayCount::Thirty360},
        SwapQuote{Date{2034, 1, 1}, 0.056, Frequency::SemiAnnual, DayCount::Thirty360},
    };
    return bootstrap_curve(ref, DayCount::Act365, instruments);
}

Swap make_swap(const Date& ref, const Date& maturity, double rate) {
    return Swap{1e7,
                rate,
                SwapDirection::Payer,
                ref,
                maturity,
                Frequency::SemiAnnual,
                DayCount::Thirty360,
                Frequency::Quarterly,
                DayCount::Act360};
}
}  // namespace

TEST_CASE("Swap DV01 is non-trivial and signed correctly", "[sensitivity]") {
    const Date ref{2024, 1, 1};
    auto curve = sample_curve(ref);
    Swap payer = make_swap(ref, Date{2031, 1, 1}, 0.055);

    const double dv01 = fi::swap_dv01(payer, *curve);
    // A payer swap gains when rates rise, so its +1bp DV01 is positive.
    REQUIRE(dv01 > 0.0);
    // Roughly notional · maturity · 1bp in magnitude (order of a few thousand).
    REQUIRE(std::abs(dv01) > 100.0);
}

TEST_CASE("Key-rate DV01s sum to the parallel DV01", "[sensitivity]") {
    const Date ref{2024, 1, 1};
    auto curve = sample_curve(ref);
    Swap payer = make_swap(ref, Date{2031, 1, 1}, 0.055);

    const double dv01 = fi::swap_dv01(payer, *curve);
    const std::vector<double> krd = fi::swap_key_rate_dv01(payer, *curve);

    REQUIRE(krd.size() == curve->node_times().size());
    const double sum = std::accumulate(krd.begin(), krd.end(), 0.0);
    REQUIRE(sum == Approx(dv01).epsilon(1e-6));
}

TEST_CASE("Key-rate risk concentrates near the swap maturity", "[sensitivity]") {
    const Date ref{2024, 1, 1};
    auto curve = sample_curve(ref);
    // A 5Y swap maturing on a curve node (2029-01-01).
    Swap payer = make_swap(ref, Date{2029, 1, 1}, 0.054);

    const std::vector<double> krd = fi::swap_key_rate_dv01(payer, *curve);
    const auto& times = curve->node_times();

    // Identify the largest-magnitude bucket; it should be at/after ~3Y, not the
    // very short end.
    std::size_t arg = 0;
    for (std::size_t i = 1; i < krd.size(); ++i) {
        if (std::abs(krd[i]) > std::abs(krd[arg])) arg = i;
    }
    REQUIRE(times[arg] > 2.0);
}

TEST_CASE("A par swap still carries DV01", "[sensitivity]") {
    const Date ref{2024, 1, 1};
    auto curve = sample_curve(ref);
    // Build an at-par 7Y swap (PV ~ 0) and confirm its DV01 is non-zero.
    Swap probe = make_swap(ref, Date{2031, 1, 1}, 0.0);
    const double par = probe.par_rate(*curve);
    Swap at_par = make_swap(ref, Date{2031, 1, 1}, par);

    REQUIRE(at_par.pv(*curve) == Approx(0.0).margin(1e-3));
    REQUIRE(std::abs(fi::swap_dv01(at_par, *curve)) > 100.0);
}
