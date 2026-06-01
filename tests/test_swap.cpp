#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "fi/bootstrap.hpp"
#include "fi/curve.hpp"
#include "fi/swap.hpp"

using Catch::Approx;
using fi::bootstrap_curve;
using fi::BootstrapInstrument;
using fi::Curve;
using fi::Date;
using fi::DayCount;
using fi::DepositQuote;
using fi::Frequency;
using fi::LogLinearCurve;
using fi::Swap;
using fi::SwapDirection;
using fi::SwapQuote;

namespace {

Swap make_swap(double notional, double rate, SwapDirection dir,
               const Date& ref, const Date& maturity) {
    return Swap{notional,         rate,
                dir,              ref,
                maturity,         Frequency::SemiAnnual,
                DayCount::Thirty360, Frequency::Quarterly,
                DayCount::Act360};
}

// A flat continuously-compounded curve at rate r.
std::unique_ptr<Curve> flat_curve(const Date& ref, double r) {
    return std::make_unique<LogLinearCurve>(ref, DayCount::Act365,
                                            std::vector<double>{0.5, 40.0},
                                            std::vector<double>{r, r});
}

}  // namespace

TEST_CASE("Bootstrapped swaps reprice to zero — closes the loop", "[swap]") {
    const Date ref{2024, 1, 1};
    const SwapQuote sw1{Date{2025, 1, 1}, 0.052, Frequency::SemiAnnual,
                        DayCount::Thirty360};
    const SwapQuote sw2{Date{2027, 1, 1}, 0.054, Frequency::SemiAnnual,
                        DayCount::Thirty360};
    const SwapQuote sw3{Date{2034, 1, 1}, 0.056, Frequency::SemiAnnual,
                        DayCount::Thirty360};

    std::vector<BootstrapInstrument> instruments{
        DepositQuote{Date{2024, 7, 1}, 0.050, DayCount::Act360}, sw1, sw2, sw3};
    auto curve = bootstrap_curve(ref, DayCount::Act365, instruments);

    for (const auto& q : {sw1, sw2, sw3}) {
        Swap s = make_swap(1.0, q.rate, SwapDirection::Payer, ref, q.maturity);
        // PV ~ 0 (single-curve self-discounting) and par rate == market rate.
        REQUIRE(s.pv(*curve) == Approx(0.0).margin(1e-6));
        REQUIRE(s.par_rate(*curve) == Approx(q.rate).margin(1e-9));
    }
}

TEST_CASE("Payer and receiver swaps are mirror images", "[swap]") {
    const Date ref{2024, 1, 1};
    auto disc = flat_curve(ref, 0.03);

    Swap payer = make_swap(1e7, 0.04, SwapDirection::Payer, ref, Date{2029, 1, 1});
    Swap receiver =
        make_swap(1e7, 0.04, SwapDirection::Receiver, ref, Date{2029, 1, 1});

    REQUIRE(payer.pv(*disc) == Approx(-receiver.pv(*disc)));
    // Fixed 4% vs a 3% curve: the payer is out-of-the-money (pays too much).
    REQUIRE(payer.pv(*disc) < 0.0);
    REQUIRE(receiver.pv(*disc) > 0.0);
}

TEST_CASE("Par rate zeroes the swap PV", "[swap]") {
    const Date ref{2024, 1, 1};
    auto disc = flat_curve(ref, 0.035);

    Swap probe = make_swap(1e6, 0.0, SwapDirection::Payer, ref, Date{2031, 1, 1});
    const double par = probe.par_rate(*disc);

    Swap at_par = make_swap(1e6, par, SwapDirection::Payer, ref, Date{2031, 1, 1});
    REQUIRE(at_par.pv(*disc) == Approx(0.0).margin(1e-6));
}

TEST_CASE("OIS discounting: distinct discount and projection curves", "[swap]") {
    const Date ref{2024, 1, 1};
    auto discount = flat_curve(ref, 0.030);    // OIS
    auto projection = flat_curve(ref, 0.035);  // forward/IBOR

    Swap s = make_swap(1e6, 0.04, SwapDirection::Payer, ref, Date{2030, 1, 1});

    // Both legs are positive; the par rate makes the two-curve PV vanish.
    REQUIRE(s.fixed_leg_pv(*discount) > 0.0);
    REQUIRE(s.floating_leg_pv(*discount, *projection) > 0.0);

    const double par = s.par_rate(*discount, *projection);
    Swap at_par = make_swap(1e6, par, SwapDirection::Payer, ref, Date{2030, 1, 1});
    REQUIRE(at_par.pv(*discount, *projection) == Approx(0.0).margin(1e-6));

    // With a higher projection curve, the par rate sits above the OIS-only par.
    const double par_single = s.par_rate(*discount);
    REQUIRE(par > par_single);
}

TEST_CASE("Self-discounted floating leg telescopes to DF(start) − DF(end)", "[swap]") {
    const Date ref{2024, 1, 1};
    auto curve = flat_curve(ref, 0.03);
    Swap s = make_swap(1.0, 0.0, SwapDirection::Payer, ref, Date{2030, 1, 1});

    const double expected =
        curve->discount(ref) - curve->discount(Date{2030, 1, 1});
    REQUIRE(s.floating_leg_pv(*curve) == Approx(expected).margin(1e-12));
}

TEST_CASE("Swap rejects bad construction", "[swap]") {
    const Date ref{2024, 1, 1};
    REQUIRE_THROWS(make_swap(1e6, 0.04, SwapDirection::Payer, Date{2030, 1, 1},
                             Date{2024, 1, 1}));
}
