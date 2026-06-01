#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "fi/bootstrap.hpp"

using Catch::Approx;
using fi::bootstrap_curve;
using fi::BootstrapInstrument;
using fi::Curve;
using fi::Date;
using fi::DayCount;
using fi::DepositQuote;
using fi::Frequency;
using fi::FuturesQuote;
using fi::implied_deposit_rate;
using fi::implied_futures_rate;
using fi::par_swap_rate;
using fi::SwapQuote;

TEST_CASE("Deposit + two swaps reprice to their quotes exactly", "[bootstrap]") {
    const Date ref{2024, 1, 1};

    const DepositQuote dep{Date{2024, 7, 1}, 0.050, DayCount::Act360};
    const SwapQuote sw1{Date{2025, 1, 1}, 0.052, Frequency::SemiAnnual,
                        DayCount::Thirty360};
    const SwapQuote sw2{Date{2026, 1, 1}, 0.055, Frequency::SemiAnnual,
                        DayCount::Thirty360};

    std::vector<BootstrapInstrument> instruments{dep, sw1, sw2};
    auto curve = bootstrap_curve(ref, DayCount::Act365, instruments);

    // Each instrument reprices to its market quote.
    REQUIRE(implied_deposit_rate(*curve, dep) == Approx(0.050).margin(1e-10));
    REQUIRE(par_swap_rate(*curve, sw1) == Approx(0.052).margin(1e-10));
    REQUIRE(par_swap_rate(*curve, sw2) == Approx(0.055).margin(1e-10));

    // Sanity: DF(0) = 1 and discount factors strictly decrease with maturity.
    REQUIRE(curve->discount(0.0) == Approx(1.0));
    REQUIRE(curve->discount(dep.maturity) > curve->discount(sw1.maturity));
    REQUIRE(curve->discount(sw1.maturity) > curve->discount(sw2.maturity));
    REQUIRE(curve->discount(sw2.maturity) < 1.0);
}

TEST_CASE("Deposit + future + swap reprice to their quotes", "[bootstrap]") {
    const Date ref{2024, 1, 1};

    const DepositQuote dep{Date{2024, 4, 1}, 0.050, DayCount::Act360};
    // Future starts at the deposit's maturity (a curve node), ends 3M later.
    const FuturesQuote fut{Date{2024, 4, 1}, Date{2024, 7, 1}, 0.052,
                           DayCount::Act360};
    const SwapQuote sw{Date{2025, 1, 1}, 0.053, Frequency::SemiAnnual,
                       DayCount::Thirty360};

    std::vector<BootstrapInstrument> instruments{dep, fut, sw};
    auto curve = bootstrap_curve(ref, DayCount::Act365, instruments);

    REQUIRE(implied_deposit_rate(*curve, dep) == Approx(0.050).margin(1e-10));
    REQUIRE(implied_futures_rate(*curve, fut) == Approx(0.052).margin(1e-10));
    REQUIRE(par_swap_rate(*curve, sw) == Approx(0.053).margin(1e-10));
}

TEST_CASE("Bootstrap is order-independent in the input list", "[bootstrap]") {
    const Date ref{2024, 1, 1};
    const DepositQuote dep{Date{2024, 7, 1}, 0.050, DayCount::Act360};
    const SwapQuote sw1{Date{2025, 1, 1}, 0.052, Frequency::SemiAnnual,
                        DayCount::Thirty360};
    const SwapQuote sw2{Date{2026, 1, 1}, 0.055, Frequency::SemiAnnual,
                        DayCount::Thirty360};

    // Same instruments, shuffled order — the bootstrapper sorts by maturity.
    std::vector<BootstrapInstrument> shuffled{sw2, dep, sw1};
    auto curve = bootstrap_curve(ref, DayCount::Act365, shuffled);

    REQUIRE(implied_deposit_rate(*curve, dep) == Approx(0.050).margin(1e-10));
    REQUIRE(par_swap_rate(*curve, sw1) == Approx(0.052).margin(1e-10));
    REQUIRE(par_swap_rate(*curve, sw2) == Approx(0.055).margin(1e-10));
}

TEST_CASE("A longer swap ladder reprices across all tenors", "[bootstrap]") {
    const Date ref{2024, 1, 1};
    std::vector<BootstrapInstrument> instruments{
        DepositQuote{Date{2024, 7, 1}, 0.0500, DayCount::Act360},
        SwapQuote{Date{2025, 1, 1}, 0.0510, Frequency::SemiAnnual, DayCount::Thirty360},
        SwapQuote{Date{2026, 1, 1}, 0.0525, Frequency::SemiAnnual, DayCount::Thirty360},
        SwapQuote{Date{2029, 1, 1}, 0.0540, Frequency::SemiAnnual, DayCount::Thirty360},
        SwapQuote{Date{2034, 1, 1}, 0.0555, Frequency::SemiAnnual, DayCount::Thirty360},
    };
    auto curve = bootstrap_curve(ref, DayCount::Act365, instruments);

    for (const auto& inst : instruments) {
        if (auto* d = std::get_if<DepositQuote>(&inst)) {
            REQUIRE(implied_deposit_rate(*curve, *d) == Approx(d->rate).margin(1e-9));
        } else if (auto* s = std::get_if<SwapQuote>(&inst)) {
            REQUIRE(par_swap_rate(*curve, *s) == Approx(s->rate).margin(1e-9));
        }
    }
}

TEST_CASE("A futures/FRA without a prior short-end node is rejected", "[bootstrap]") {
    const Date ref{2024, 1, 1};
    std::vector<BootstrapInstrument> instruments{
        FuturesQuote{Date{2024, 1, 1}, Date{2024, 4, 1}, 0.05, DayCount::Act360}};
    REQUIRE_THROWS(bootstrap_curve(ref, DayCount::Act365, instruments));
}
