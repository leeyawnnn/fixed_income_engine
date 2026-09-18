#include "fi/portfolio.hpp"

#include <cmath>
#include <stdexcept>

#include "fi/json.hpp"
#include "fi/risk.hpp"

namespace fi {

namespace {

Frequency frequency_from_int(int per_year_count) {
    switch (per_year_count) {
        case 1: return Frequency::Annual;
        case 2: return Frequency::SemiAnnual;
        case 4: return Frequency::Quarterly;
        case 12: return Frequency::Monthly;
        default: break;
    }
    throw std::runtime_error("portfolio: unsupported frequency " +
                             std::to_string(per_year_count));
}

Date parse_iso_date(const std::string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        throw std::runtime_error("portfolio: expected YYYY-MM-DD, got '" + text + "'");
    }
    return Date(std::stoi(text.substr(0, 4)),
                static_cast<unsigned>(std::stoul(text.substr(5, 2))),
                static_cast<unsigned>(std::stoul(text.substr(8, 2))));
}

}  // namespace

Swap SwapPosition::to_swap(const Date& valuation_date) const {
    return Swap{notional.to_double(),
                fixed_rate,
                direction,
                valuation_date,
                valuation_date.add_months(tenor_months),
                fixed_frequency,
                fixed_day_count,
                float_frequency,
                float_day_count};
}

std::vector<Swap> Portfolio::swaps() const {
    std::vector<Swap> result;
    result.reserve(positions.size());
    for (const SwapPosition& position : positions) {
        result.push_back(position.to_swap(valuation_date));
    }
    return result;
}

Portfolio load_portfolio(const std::string& json_text) {
    const json::Value root = json::parse(json_text);

    Portfolio portfolio;
    portfolio.valuation_date = parse_iso_date(root["valuation_date"].as_string());
    portfolio.currency = root.contains("currency")
                             ? currency_from_string(root["currency"].as_string())
                             : Currency::USD;

    int index = 0;
    for (const json::Value& entry : root["swaps"].as_array()) {
        ++index;
        SwapPosition position;
        position.id = entry.contains("id") ? entry["id"].as_string()
                                           : "swap-" + std::to_string(index);
        position.notional =
            Money::from_double(entry["notional"].number(), portfolio.currency);
        position.fixed_rate = entry["fixed_rate"].number();

        const std::string& direction = entry["direction"].as_string();
        if (direction == "payer") {
            position.direction = SwapDirection::Payer;
        } else if (direction == "receiver") {
            position.direction = SwapDirection::Receiver;
        } else {
            throw std::runtime_error(
                "portfolio: direction must be payer or receiver, got '" + direction +
                "'");
        }

        // Tenor is declared in years but carried in months, so a 2.5-year trade
        // lands on a real calendar date rather than a fractional year.
        const double tenor_years = entry["tenor_years"].number();
        position.tenor_months = static_cast<int>(std::lround(tenor_years * 12.0));
        if (position.tenor_months <= 0) {
            throw std::runtime_error("portfolio: tenor must be positive");
        }

        if (entry.contains("fixed_freq")) {
            position.fixed_frequency =
                frequency_from_int(static_cast<int>(entry["fixed_freq"].number()));
        }
        if (entry.contains("float_freq")) {
            position.float_frequency =
                frequency_from_int(static_cast<int>(entry["float_freq"].number()));
        }
        portfolio.positions.push_back(std::move(position));
    }
    return portfolio;
}

PortfolioValuation value_portfolio(const Portfolio& portfolio, const Curve& discount) {
    PortfolioValuation valuation;
    valuation.currency = portfolio.currency;
    valuation.total_pv = Money::from_minor_units(0, portfolio.currency);

    for (const SwapPosition& position : portfolio.positions) {
        const Swap swap = position.to_swap(portfolio.valuation_date);
        const Swap::LegBreakdown legs = swap.legs(discount);

        PositionValuation row;
        row.id = position.id;
        row.notional = position.notional;
        row.pv = Money::from_double(swap.pv(discount), portfolio.currency);
        row.fixed_leg = Money::from_double(legs.fixed, portfolio.currency);
        row.floating_leg = Money::from_double(legs.floating, portfolio.currency);
        row.dv01 = swap_dv01(swap, discount);
        row.pv01 = swap.pv01(discount);
        row.par_rate = legs.par_rate;
        row.rate_offset = legs.rate_offset;

        // Sum the rounded rows, not the unrounded doubles: a report whose rows
        // do not add up to its total is a report nobody trusts.
        valuation.total_pv += row.pv;
        valuation.positions.push_back(std::move(row));
    }

    const std::vector<Swap> swaps = portfolio.swaps();
    valuation.total_dv01 = portfolio_dv01(swaps, discount);
    valuation.total_gamma = portfolio_curve_gamma(swaps, discount);
    return valuation;
}

}  // namespace fi
