#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "fi/bond.hpp"
#include "fi/bootstrap.hpp"
#include "fi/curve.hpp"
#include "fi/risk.hpp"
#include "fi/swap.hpp"
#include "fi/ytm_solver.hpp"
#include "treasury_fixture.hpp"

using Catch::Approx;
using namespace fi;

namespace {

// The 2025-12-31 Treasury curve, bootstrapped from published CMT par yields.
std::unique_ptr<Curve> treasury_curve(
    Interpolation scheme = Interpolation::LogLinearDiscount) {
    const auto cmt = testing::load_cmt_curve("2025-12-31");
    return bootstrap_curve(cmt.as_of, DayCount::Act365, testing::cmt_instruments(cmt),
                           scheme);
}

Swap make_swap(const Date& start, int tenor_months, double notional, double rate,
               SwapDirection direction) {
    return Swap{notional,
                rate,
                direction,
                start,
                start.add_months(tenor_months),
                Frequency::SemiAnnual,
                DayCount::Thirty360,
                Frequency::Quarterly,
                DayCount::Act360};
}

}  // namespace

// --- External validation -----------------------------------------------------

TEST_CASE("prices Hull's worked two-year bond example", "[bond][validation]") {
    // Hull, "Options, Futures, and Other Derivatives", chapter 4 (Interest
    // Rates): a two-year bond, principal 100, 6% coupon paid semiannually,
    // valued off continuously compounded zero rates of 5.0%, 5.8%, 6.4% and
    // 6.8% at 6m, 1y, 18m and 2y. Hull reports a price of 98.39, a bond yield
    // of 6.76% and a two-year par yield of 6.87%.
    //
    // This is the check that matters most in the file, because it compares
    // against a number this repository did not produce.
    const Date ref{2024, 1, 1};

    // 30/360 on these dates gives exactly 0.5, 1.0, 1.5 and 2.0, so the test
    // reproduces Hull's stylised tenors rather than calendar approximations.
    const std::vector<double> times = {0.5, 1.0, 1.5, 2.0};
    const std::vector<double> zeros = {0.050, 0.058, 0.064, 0.068};
    const LinearInterpCurve curve{ref, DayCount::Thirty360, times, zeros};
    REQUIRE(curve.time_to(ref.add_months(6)) == Approx(0.5).margin(1e-12));
    REQUIRE(curve.time_to(ref.add_months(24)) == Approx(2.0).margin(1e-12));

    const Bond bond{100.0,
                    0.06,
                    Frequency::SemiAnnual,
                    ref,
                    ref.add_months(24),
                    DayCount::Thirty360};

    const double price = price_from_curve(bond, ref, curve);
    REQUIRE(price == Approx(98.39).margin(0.005));

    // Hull's yield is continuously compounded; the engine solves the
    // semiannually compounded one, so convert before comparing.
    const SolverResult ytm = solve_ytm(bond, price, ref);
    REQUIRE(ytm.converged);
    const double continuous = 2.0 * std::log1p(ytm.root / 2.0);
    REQUIRE(continuous == Approx(0.0676).margin(5e-5));

    // The two-year par yield off the same curve.
    const ParBondQuote par_bond{ref.add_months(24), 0.0, Frequency::SemiAnnual,
                                DayCount::Thirty360};
    REQUIRE(implied_par_bond_yield(curve, par_bond) == Approx(0.0687).margin(5e-5));
}

// --- Accrued interest and clean vs dirty ------------------------------------

TEST_CASE("accrued interest is linear across the coupon period", "[bond]") {
    const Date issue{2025, 1, 15};
    const Bond bond{100.0,
                    0.05,
                    Frequency::SemiAnnual,
                    issue,
                    issue.add_months(60),
                    DayCount::Thirty360};

    // Immediately after a coupon date nothing has accrued.
    REQUIRE(accrued_interest(bond, issue) == Approx(0.0).margin(1e-12));

    // Halfway through the period, half a coupon: 100 * 5% / 2 / 2 = 1.25.
    const AccrualPeriod mid = accrual(bond, issue.add_months(3));
    REQUIRE(mid.accrued == Approx(1.25).margin(1e-9));
    REQUIRE(mid.days_accrued / mid.days_in_period == Approx(0.5).margin(1e-12));
    REQUIRE(mid.period_start == issue);
    REQUIRE(mid.period_end == issue.add_months(6));

    // A day before the next coupon, nearly a whole coupon.
    const double nearly_full = accrued_interest(bond, issue.add_months(6).add_days(-1));
    REQUIRE(nearly_full < 2.5);
    REQUIRE(nearly_full > 2.48);

    REQUIRE_THROWS_AS(accrual(bond, issue.add_days(-1)), std::invalid_argument);
    REQUIRE_THROWS_AS(accrual(bond, bond.maturity_date()), std::invalid_argument);
}

TEST_CASE("clean price is dirty price minus accrued", "[bond]") {
    const Date issue{2025, 1, 15};
    const Bond bond{100.0,
                    0.05,
                    Frequency::SemiAnnual,
                    issue,
                    issue.add_months(60),
                    DayCount::Thirty360};
    const Date settlement = issue.add_months(3);

    const double dirty = dirty_price(bond, 0.05, settlement);
    const double clean = clean_price(bond, 0.05, settlement);
    REQUIRE(dirty - clean == Approx(accrued_interest(bond, settlement)).margin(1e-12));

    // The point of quoting clean: the dirty price jumps down by the coupon on a
    // payment date, the clean price does not.
    const Date before = issue.add_months(6).add_days(-1);
    const Date after = issue.add_months(6).add_days(1);
    const double dirty_drop =
        dirty_price(bond, 0.05, before) - dirty_price(bond, 0.05, after);
    const double clean_drop =
        clean_price(bond, 0.05, before) - clean_price(bond, 0.05, after);
    REQUIRE(dirty_drop > 2.4);  // roughly the 2.50 coupon
    REQUIRE(std::abs(clean_drop) < 0.05);
}

// --- Z-spread ----------------------------------------------------------------

TEST_CASE("z-spread is zero on the curve and recovers a known shift",
          "[bond][zspread]") {
    const auto curve = treasury_curve();
    const Date settlement = curve->reference_date();
    const Bond bond{100.0,
                    0.04,
                    Frequency::SemiAnnual,
                    settlement,
                    settlement.add_months(60),
                    DayCount::Thirty360};

    // A bond priced exactly off the curve has no spread to it.
    const double on_curve = price_from_curve(bond, settlement, *curve);
    const SolverResult zero = z_spread(bond, on_curve, settlement, *curve);
    REQUIRE(zero.converged);
    REQUIRE(zero.root == Approx(0.0).margin(1e-10));

    // Price it 75bp cheap to the curve and the solver should find 75bp back.
    const double cheap = price_from_curve(bond, settlement, *curve, 0.0075);
    REQUIRE(cheap < on_curve);
    const SolverResult recovered = z_spread(bond, cheap, settlement, *curve);
    REQUIRE(recovered.converged);
    REQUIRE(recovered.root == Approx(0.0075).margin(1e-9));

    REQUIRE_THROWS_AS(z_spread(bond, 100.0, bond.maturity_date(), *curve),
                      std::invalid_argument);
}

// --- Swap analytics ----------------------------------------------------------

TEST_CASE("pv01 is the annuity and differs from dv01 off par", "[swap][risk]") {
    const auto curve = treasury_curve();
    const Date ref = curve->reference_date();

    const Swap at_par = make_swap(ref, 60, 10'000'000.0, 0.0, SwapDirection::Payer);
    const double par = at_par.par_rate(*curve);
    const Swap on_market = make_swap(ref, 60, 10'000'000.0, par, SwapDirection::Payer);

    REQUIRE(on_market.pv(*curve) == Approx(0.0).margin(1e-6));
    REQUIRE(on_market.pv01(*curve) ==
            Approx(10'000'000.0 * on_market.annuity(*curve) * 1e-4).margin(1e-12));

    // At par the two are close; a swap struck 300bp away separates them.
    const double dv01_at_par = swap_dv01(on_market, *curve);
    REQUIRE(std::abs(on_market.pv01(*curve) - std::abs(dv01_at_par)) <
            0.02 * on_market.pv01(*curve));

    const Swap off_market =
        make_swap(ref, 60, 10'000'000.0, par + 0.03, SwapDirection::Payer);
    REQUIRE(
        std::abs(off_market.pv01(*curve) - std::abs(swap_dv01(off_market, *curve))) >
        0.02 * off_market.pv01(*curve));
}

TEST_CASE("leg breakdown sums to the swap PV and explains its sign", "[swap]") {
    const auto curve = treasury_curve();
    const Date ref = curve->reference_date();

    const Swap payer = make_swap(ref, 120, 5'000'000.0, 0.055, SwapDirection::Payer);
    const Swap::LegBreakdown legs = payer.legs(*curve);

    REQUIRE(legs.fixed + legs.floating == Approx(payer.pv(*curve)).margin(1e-8));
    REQUIRE(legs.fixed < 0.0);     // a payer pays the fixed leg away
    REQUIRE(legs.floating > 0.0);  // and receives the floating leg

    // Paying above the par rate means a negative PV, by exactly the rate offset
    // applied to the annuity.
    REQUIRE(legs.rate_offset > 0.0);
    REQUIRE(payer.pv(*curve) < 0.0);
    REQUIRE(payer.pv(*curve) ==
            Approx(-legs.rate_offset * 5'000'000.0 * legs.annuity).margin(1e-6));
}

TEST_CASE("a forward-starting swap is consistent with the spot curve", "[swap]") {
    const auto curve = treasury_curve();
    const Date ref = curve->reference_date();

    // 5y5y: a five-year swap starting in five years.
    const Date forward_start = ref.add_months(60);
    const Swap forward =
        make_swap(forward_start, 60, 10'000'000.0, 0.0, SwapDirection::Payer);
    const double forward_par = forward.par_rate(*curve);

    // Struck at its own par rate it is worth nothing today.
    const Swap at_par =
        make_swap(forward_start, 60, 10'000'000.0, forward_par, SwapDirection::Payer);
    REQUIRE(at_par.pv(*curve) == Approx(0.0).margin(1e-6));

    // Basis consistency: a 10y swap is a 5y swap plus a 5y5y swap. Paying fixed
    // at the 10y par rate on both sub-periods must reprice the 10y swap, so the
    // 10y par rate is the annuity-weighted blend of the 5y and 5y5y par rates.
    const Swap spot5 = make_swap(ref, 60, 10'000'000.0, 0.0, SwapDirection::Payer);
    const Swap spot10 = make_swap(ref, 120, 10'000'000.0, 0.0, SwapDirection::Payer);
    const double par5 = spot5.par_rate(*curve);
    const double par10 = spot10.par_rate(*curve);

    const double a5 = spot5.annuity(*curve);
    const double a_fwd = forward.annuity(*curve);
    const double a10 = spot10.annuity(*curve);
    REQUIRE(a5 + a_fwd == Approx(a10).margin(1e-10));

    const double blended = (par5 * a5 + forward_par * a_fwd) / a10;
    REQUIRE(blended == Approx(par10).margin(1e-10));
}

// --- Key-rate reconciliation and hedging -------------------------------------

TEST_CASE("key-rate DV01s sum to the parallel DV01 up to a reported residual",
          "[risk][keyrate]") {
    const auto curve = treasury_curve();
    const Date ref = curve->reference_date();
    const std::vector<Swap> book = {
        make_swap(ref, 60, 10'000'000.0, 0.050, SwapDirection::Payer),
        make_swap(ref, 120, 5'000'000.0, 0.045, SwapDirection::Receiver),
    };

    const KeyRateReconciliation report = reconcile_key_rates(book, *curve);
    REQUIRE(report.key_rate_dv01.size() == curve->node_times().size());
    REQUIRE(report.residual ==
            Approx(report.sum_of_buckets - report.parallel_dv01).margin(1e-12));

    // They agree closely but not exactly, and the test pins the size of the gap
    // rather than pretending it is zero: the decomposition drops the
    // second-order cross terms between nodes.
    REQUIRE(std::abs(report.relative_residual) < 1e-3);
    REQUIRE(std::abs(report.parallel_dv01) > 1.0);  // there is real risk to decompose
}

TEST_CASE("bucketed hedge reduces key-rate exposure", "[risk][hedge]") {
    const auto curve = treasury_curve();
    const Date ref = curve->reference_date();
    const std::vector<Swap> book = {
        make_swap(ref, 60, 10'000'000.0, 0.050, SwapDirection::Payer),
        make_swap(ref, 120, 5'000'000.0, 0.045, SwapDirection::Receiver),
    };

    // Benchmark receivers at the liquid points, unit notional.
    std::vector<Swap> hedges;
    for (const int months : {24, 60, 84, 120, 360}) {
        hedges.push_back(make_swap(ref, months, 1.0, 0.04, SwapDirection::Receiver));
    }

    const BucketHedge hedge = solve_bucket_hedge(book, hedges, *curve);
    REQUIRE(hedge.notionals.size() == hedges.size());
    REQUIRE(hedge.residual_krd.size() == curve->node_times().size());

    // Five instruments against thirteen buckets cannot zero everything, but the
    // worst remaining bucket should be a small fraction of what it was.
    REQUIRE(hedge.worst_bucket_before > 100.0);
    REQUIRE(hedge.worst_bucket_after < 0.05 * hedge.worst_bucket_before);
    REQUIRE(std::abs(hedge.residual_dv01) < std::abs(portfolio_dv01(book, *curve)));

    REQUIRE_THROWS_AS(solve_bucket_hedge(book, {}, *curve), std::invalid_argument);
}

TEST_CASE("hedging a swap with itself is exact", "[risk][hedge]") {
    // A sanity check with a known answer: the hedge for a book of one payer is
    // the identical receiver, notional 1:1, leaving nothing behind.
    const auto curve = treasury_curve();
    const Date ref = curve->reference_date();
    const std::vector<Swap> book = {
        make_swap(ref, 60, 7'500'000.0, 0.048, SwapDirection::Payer)};
    const std::vector<Swap> hedges = {
        make_swap(ref, 60, 1.0, 0.048, SwapDirection::Receiver)};

    const BucketHedge hedge = solve_bucket_hedge(book, hedges, *curve);
    REQUIRE(hedge.notionals[0] == Approx(7'500'000.0).epsilon(1e-9));
    REQUIRE(hedge.worst_bucket_after < 1e-6);
    REQUIRE(std::abs(hedge.residual_dv01) < 1e-6);
}

// --- Duration versus convexity ----------------------------------------------

TEST_CASE("convexity matters for large shifts and not for small ones",
          "[risk][convexity]") {
    const auto curve = treasury_curve();
    const Date ref = curve->reference_date();
    const std::vector<Swap> book = {
        make_swap(ref, 120, 25'000'000.0, 0.040, SwapDirection::Payer)};

    const std::vector<ShiftAttribution> rows =
        shift_attribution(book, *curve, {-200.0, -25.0, 25.0, 200.0});
    REQUIRE(rows.size() == 4);

    for (const ShiftAttribution& row : rows) {
        INFO("shift " << row.shift_bp << "bp");
        // Adding the second-order term always gets closer to the truth.
        REQUIRE(std::abs(row.with_convexity_error) <= std::abs(row.duration_error));
    }

    // The shape of the error is the point. Duration's absolute error is
    // quadratic in the shift while the move it is approximating is linear, so
    // the *relative* error grows in proportion to the shift: measured here it
    // runs 1.17% at 25bp and 9.60% at 200bp, a factor of 8.2 for an 8x shift.
    const ShiftAttribution& small = rows[2];  // +25bp
    const ShiftAttribution& large = rows[3];  // +200bp
    const double small_relative = std::abs(small.duration_error / small.actual_pnl);
    const double large_relative = std::abs(large.duration_error / large.actual_pnl);
    REQUIRE(large_relative > 4.0 * small_relative);
    REQUIRE(large_relative / small_relative < 16.0);

    // And at every shift the second-order term removes at least an order of
    // magnitude of that error.
    for (const ShiftAttribution& row : rows) {
        INFO("shift " << row.shift_bp << "bp");
        REQUIRE(std::abs(row.with_convexity_error) <
                0.1 * std::abs(row.duration_error));
    }
}
