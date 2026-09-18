#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "fi/bootstrap.hpp"
#include "fi/portfolio.hpp"
#include "treasury_fixture.hpp"

using Catch::Approx;
using namespace fi;

namespace {

std::string read_data_file(const std::string& name) {
    std::ifstream file(std::string(FI_DATA_DIR) + "/" + name);
    REQUIRE(file.is_open());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

}  // namespace

TEST_CASE("the committed portfolio file loads with exact notionals", "[portfolio]") {
    const Portfolio portfolio = load_portfolio(read_data_file("portfolio.json"));

    REQUIRE(portfolio.valuation_date == Date{2025, 12, 31});
    REQUIRE(portfolio.currency == Currency::USD);
    REQUIRE(portfolio.positions.size() == 2);

    // The notional is a declared quantity, so it is exact to the cent.
    REQUIRE(portfolio.positions[0].id == "USD-5Y-PAYER");
    REQUIRE(portfolio.positions[0].notional.minor_units() == 1'000'000'000);
    REQUIRE(portfolio.positions[0].direction == SwapDirection::Payer);
    REQUIRE(portfolio.positions[0].tenor_months == 60);
    REQUIRE(portfolio.positions[1].notional.minor_units() == 500'000'000);
    REQUIRE(portfolio.positions[1].direction == SwapDirection::Receiver);
    REQUIRE(portfolio.positions[1].tenor_months == 120);

    const std::vector<Swap> swaps = portfolio.swaps();
    REQUIRE(swaps.size() == 2);
    REQUIRE(swaps[0].maturity() == Date{2030, 12, 31});
    REQUIRE(swaps[1].maturity() == Date{2035, 12, 31});
}

TEST_CASE("portfolio parsing rejects malformed input", "[portfolio]") {
    REQUIRE_THROWS_AS(load_portfolio(R"({"valuation_date":"2025-13-31","swaps":[]})"),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(load_portfolio(R"({"valuation_date":"31/12/2025","swaps":[]})"),
                      std::runtime_error);
    REQUIRE_THROWS_AS(
        load_portfolio(
            R"({"valuation_date":"2025-12-31","swaps":[{"notional":1,"fixed_rate":0.04,)"
            R"("direction":"sideways","tenor_years":5}]})"),
        std::runtime_error);
    REQUIRE_THROWS_AS(
        load_portfolio(
            R"({"valuation_date":"2025-12-31","currency":"XYZ","swaps":[]})"),
        std::invalid_argument);
}

TEST_CASE("valuation totals are the exact sum of the reported rows",
          "[portfolio][money]") {
    // The reason totals are summed in minor units: a report whose rows do not
    // add to its total is a report nobody trusts. Rounding the total separately
    // from the rows would let them disagree by a cent.
    const auto cmt = fi::testing::load_cmt_curve("2025-12-31");
    const auto curve =
        bootstrap_curve(cmt.as_of, DayCount::Act365, fi::testing::cmt_instruments(cmt));
    const Portfolio portfolio = load_portfolio(read_data_file("portfolio.json"));
    const PortfolioValuation valuation = value_portfolio(portfolio, *curve);

    REQUIRE(valuation.positions.size() == 2);

    Money sum = Money::from_minor_units(0, Currency::USD);
    for (const PositionValuation& position : valuation.positions) sum += position.pv;
    REQUIRE(sum == valuation.total_pv);
    REQUIRE(sum.minor_units() == valuation.total_pv.minor_units());

    // Each row's legs reconstruct its PV to the cent.
    for (const PositionValuation& position : valuation.positions) {
        const Money legs = position.fixed_leg + position.floating_leg;
        INFO(position.id << " legs " << legs.to_string() << " pv "
                         << position.pv.to_string());
        REQUIRE(std::abs(legs.minor_units() - position.pv.minor_units()) <= 1);
    }

    // The book is a 5s10s curve position: opposite signs at the two tenors, and
    // a net DV01 far smaller than either leg.
    REQUIRE(valuation.positions[0].dv01 * valuation.positions[1].dv01 < 0.0);
    REQUIRE(std::abs(valuation.total_dv01) < std::abs(valuation.positions[0].dv01) +
                                                 std::abs(valuation.positions[1].dv01));
}
