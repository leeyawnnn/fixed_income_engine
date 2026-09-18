// fi_report — regenerates every committed artifact under reports/.
//
//   fi_report [--data DIR] [--out DIR] [--as-of YYYY-MM-DD]
//
// Bootstraps the Treasury curve from published CMT par yields under all three
// interpolation schemes, fits Nelson-Siegel-Svensson, values the portfolio, and
// writes the validation tables the README quotes. Every number in that README
// comes out of this program; CI reruns it and fails if the committed files
// change, so a stale claim cannot survive a push.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fi/bootstrap.hpp"
#include "fi/curve.hpp"
#include "fi/nss.hpp"
#include "fi/portfolio.hpp"
#include "fi/risk.hpp"
#include "fi/scenario.hpp"
#include "fi/version.hpp"

using namespace fi;

namespace {

struct Args {
    std::string data = "data";
    std::string out = "reports";
    std::string as_of = "2025-12-31";
};

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--data" && i + 1 < argc)
            args.data = argv[++i];
        else if (flag == "--out" && i + 1 < argc)
            args.out = argv[++i];
        else if (flag == "--as-of" && i + 1 < argc)
            args.as_of = argv[++i];
        else
            throw std::runtime_error("unrecognised argument: " + flag);
    }
    return args;
}

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::vector<std::string> split(const std::string& line, char delimiter) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, delimiter)) fields.push_back(field);
    return fields;
}

Date parse_iso_date(const std::string& text) {
    return Date(std::stoi(text.substr(0, 4)),
                static_cast<unsigned>(std::stoul(text.substr(5, 2))),
                static_cast<unsigned>(std::stoul(text.substr(8, 2))));
}

// One CMT tenor: its column name, its length in whole months, and the quote.
struct CmtPoint {
    std::string tenor;
    int months = 0;
    double tau = 0.0;
    double par_yield = 0.0;
};

// The 1.5-month column is skipped: its series only begins 2025-02-18, and a
// month and a half is not a whole number of months to step a maturity by.
const std::vector<std::pair<std::string, int>>& cmt_tenors() {
    static const std::vector<std::pair<std::string, int>> kTenors = {
        {"1M", 1},    {"2M", 2},    {"3M", 3},    {"4M", 4},  {"6M", 6},
        {"1Y", 12},   {"2Y", 24},   {"3Y", 36},   {"5Y", 60}, {"7Y", 84},
        {"10Y", 120}, {"20Y", 240}, {"30Y", 360},
    };
    return kTenors;
}

std::vector<CmtPoint> load_cmt_row(const std::string& csv_path,
                                   const std::string& iso_date) {
    std::ifstream file(csv_path);
    if (!file) throw std::runtime_error("cannot open " + csv_path);

    std::vector<std::string> header;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (header.empty()) {
            header = split(line, ',');
            continue;
        }
        const std::vector<std::string> fields = split(line, ',');
        if (fields.empty() || fields[0] != iso_date) continue;

        std::vector<CmtPoint> points;
        for (const auto& [name, months] : cmt_tenors()) {
            std::size_t column = 0;
            while (column < header.size() && header[column] != name) ++column;
            if (column >= fields.size() || fields[column].empty()) continue;
            points.push_back(CmtPoint{name, months, months / 12.0,
                                      std::stod(fields[column]) / 100.0});
        }
        return points;
    }
    throw std::runtime_error("no row for " + iso_date + " in " + csv_path);
}

std::string fixed(double value, int places) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(places) << value;
    return out.str();
}

std::string scientific(double value, int places) {
    std::ostringstream out;
    out << std::scientific << std::setprecision(places) << value;
    return out.str();
}

class Writer {
public:
    Writer(const std::string& directory, const std::string& name,
           const std::string& description)
        : path_(directory + "/" + name), stream_(path_) {
        if (!stream_) throw std::runtime_error("cannot write " + path_);
        // Every artifact says what it is and how to remake it, so a file that
        // gets separated from the repository still carries its provenance.
        stream_ << "# " << description << "\n"
                << "# Produced by: fi_report (fixed_income_engine " << version_string
                << ")\n"
                << "# Regenerate:  ./build/fi_report --out reports\n";
    }

    std::ofstream& stream() { return stream_; }
    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::ofstream stream_;
};

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args args = parse_args(argc, argv);
        std::filesystem::create_directories(args.out);

        const Date as_of = parse_iso_date(args.as_of);
        const std::vector<CmtPoint> cmt =
            load_cmt_row(args.data + "/treasury_par_yields_2025.csv", args.as_of);
        if (cmt.empty()) throw std::runtime_error("no CMT quotes for " + args.as_of);

        std::vector<BootstrapInstrument> instruments;
        instruments.reserve(cmt.size());
        for (const CmtPoint& point : cmt) {
            instruments.emplace_back(
                ParBondQuote{as_of.add_months(point.months), point.par_yield,
                             Frequency::SemiAnnual, DayCount::Thirty360});
        }

        const std::vector<Interpolation> schemes = {
            Interpolation::LinearZero,
            Interpolation::LogLinearDiscount,
            Interpolation::MonotoneConvex,
        };

        std::vector<std::unique_ptr<Curve>> curves;
        for (const Interpolation scheme : schemes) {
            curves.push_back(
                bootstrap_curve(as_of, DayCount::Act365, instruments, scheme));
        }
        const Curve& base = *curves[1];  // log-linear is the reporting curve

        // --- 1. Repricing residuals, per scheme ------------------------------
        double worst_residual_bp = 0.0;
        {
            Writer writer(args.out, "repricing_residuals.csv",
                          "Bootstrap repricing check: curve-implied quote minus market "
                          "quote, per instrument, per interpolation scheme.");
            writer.stream() << "scheme,instrument,tenor,market_quote_pct,"
                               "implied_quote_pct,residual_bp\n";
            for (std::size_t s = 0; s < schemes.size(); ++s) {
                for (std::size_t i = 0; i < instruments.size(); ++i) {
                    const double residual =
                        repricing_residual(*curves[s], instruments[i]);
                    const double market = quoted_rate(instruments[i]);
                    worst_residual_bp =
                        std::max(worst_residual_bp, std::abs(residual) * 1e4);
                    writer.stream() << to_string(schemes[s]) << ','
                                    << instrument_label(instruments[i], as_of) << ','
                                    << cmt[i].tenor << ',' << fixed(market * 100.0, 4)
                                    << ',' << fixed((market + residual) * 100.0, 10)
                                    << ',' << scientific(residual * 1e4, 3) << '\n';
                }
            }
        }

        // --- 2. Curve nodes, per scheme --------------------------------------
        {
            Writer writer(args.out, "curve_nodes.csv",
                          "Bootstrapped curve at its nodes: continuously compounded "
                          "zero rate, discount factor, and instantaneous forward.");
            writer.stream() << "scheme,tenor,node_years,zero_rate_pct,discount_factor,"
                               "instantaneous_forward_pct\n";
            for (std::size_t s = 0; s < schemes.size(); ++s) {
                const std::vector<double>& times = curves[s]->node_times();
                for (std::size_t i = 0; i < times.size(); ++i) {
                    writer.stream()
                        << to_string(schemes[s]) << ',' << cmt[i].tenor << ','
                        << fixed(times[i], 6) << ','
                        << fixed(curves[s]->zero_rate(times[i]) * 100.0, 6) << ','
                        << fixed(curves[s]->discount(times[i]), 8) << ','
                        << fixed(curves[s]->instantaneous_forward(times[i]) * 100.0, 6)
                        << '\n';
                }
            }
        }

        // --- 3. Dense forward curves, for the interpolation figure -----------
        {
            Writer writer(args.out, "forward_curves.csv",
                          "Instantaneous forward rate on a dense grid, per "
                          "interpolation scheme. This is where the schemes disagree.");
            writer.stream() << "tenor_years";
            for (const Interpolation scheme : schemes) {
                writer.stream() << ',' << to_string(scheme) << "_forward_pct";
            }
            writer.stream() << '\n';
            for (int step = 1; step <= 3000; ++step) {
                const double t = step * 0.01;
                writer.stream() << fixed(t, 2);
                for (const auto& curve : curves) {
                    writer.stream()
                        << ',' << fixed(curve->instantaneous_forward(t) * 100.0, 6);
                }
                writer.stream() << '\n';
            }
        }

        // --- 4. Zero and par curves on a dense grid, for the curve figure ----
        {
            Writer writer(args.out, "zero_curve.csv",
                          "Bootstrapped zero curve and discount factors on a dense "
                          "grid (log-linear interpolation).");
            writer.stream() << "tenor_years,zero_rate_pct,discount_factor\n";
            for (int step = 1; step <= 3000; ++step) {
                const double t = step * 0.01;
                writer.stream()
                    << fixed(t, 2) << ',' << fixed(base.zero_rate(t) * 100.0, 6) << ','
                    << fixed(base.discount(t), 8) << '\n';
            }
        }

        // --- 5. NSS fit -------------------------------------------------------
        double nss_rmse_bp = 0.0;
        NSSFitResult nss;
        {
            std::vector<double> taus;
            std::vector<double> yields;
            for (const CmtPoint& point : cmt) {
                taus.push_back(point.tau);
                yields.push_back(point.par_yield);
            }
            nss = fit_nss(taus, yields);
            nss_rmse_bp = nss.rmse * 1e4;

            Writer writer(args.out, "nss_fit.csv",
                          "Nelson-Siegel-Svensson fitted to the CMT par yields: "
                          "market yield, fitted yield, residual.");
            writer.stream() << "tenor,tau_years,market_yield_pct,fitted_yield_pct,"
                               "residual_bp\n";
            for (std::size_t i = 0; i < taus.size(); ++i) {
                const double fitted = nss.params.yield(taus[i]);
                writer.stream()
                    << cmt[i].tenor << ',' << fixed(taus[i], 6) << ','
                    << fixed(yields[i] * 100.0, 4) << ',' << fixed(fitted * 100.0, 6)
                    << ',' << fixed((fitted - yields[i]) * 1e4, 4) << '\n';
            }

            Writer params(args.out, "nss_params.csv",
                          "Fitted Nelson-Siegel-Svensson parameters and fit quality.");
            params.stream() << "parameter,value\n"
                            << "beta0," << fixed(nss.params.beta0, 8) << '\n'
                            << "beta1," << fixed(nss.params.beta1, 8) << '\n'
                            << "beta2," << fixed(nss.params.beta2, 8) << '\n'
                            << "beta3," << fixed(nss.params.beta3, 8) << '\n'
                            << "lambda1," << fixed(nss.params.lambda1, 8) << '\n'
                            << "lambda2," << fixed(nss.params.lambda2, 8) << '\n'
                            << "rmse_bp," << fixed(nss_rmse_bp, 4) << '\n'
                            << "iterations," << nss.iterations << '\n'
                            << "converged," << (nss.converged ? 1 : 0) << '\n';
        }

        // --- 6. Portfolio valuation (Money-typed) -----------------------------
        const Portfolio portfolio =
            load_portfolio(read_file(args.data + "/portfolio.json"));
        if (!(portfolio.valuation_date == as_of)) {
            throw std::runtime_error("portfolio valuation date " +
                                     portfolio.valuation_date.to_string() +
                                     " does not match the curve date " + args.as_of);
        }
        const PortfolioValuation valuation = value_portfolio(portfolio, base);
        const std::vector<Swap> book = portfolio.swaps();
        {
            Writer writer(args.out, "portfolio_valuation.csv",
                          "Portfolio valuation. Money columns are exact to the minor "
                          "unit and the rows sum to the total.");
            writer.stream() << "id,currency,notional,pv,fixed_leg,floating_leg,"
                               "dv01_per_bp,pv01_per_bp,par_rate_pct,rate_offset_bp\n";
            for (const PositionValuation& row : valuation.positions) {
                writer.stream()
                    << row.id << ',' << to_string(valuation.currency) << ','
                    << row.notional.to_string() << ',' << row.pv.to_string() << ','
                    << row.fixed_leg.to_string() << ',' << row.floating_leg.to_string()
                    << ',' << fixed(row.dv01, 2) << ',' << fixed(row.pv01, 2) << ','
                    << fixed(row.par_rate * 100.0, 4) << ','
                    << fixed(row.rate_offset * 1e4, 2) << '\n';
            }
            writer.stream() << "TOTAL," << to_string(valuation.currency) << ",,"
                            << valuation.total_pv.to_string() << ",,,"
                            << fixed(valuation.total_dv01, 2) << ",,,\n";

            // The two totals can differ by up to half a minor unit per position,
            // and showing both is the honest way to present the Money boundary.
            // TOTAL is the sum of the rounded rows, so the report adds up.
            // TOTAL_UNROUNDED is what the pricing produced before rounding, and
            // is the figure the scenario table works from.
            writer.stream() << "TOTAL_UNROUNDED," << to_string(valuation.currency)
                            << ",," << fixed(portfolio_pv(book, base), 6) << ",,,,,,\n";
        }

        // --- 7. Key-rate reconciliation --------------------------------------
        const KeyRateReconciliation key_rates = reconcile_key_rates(book, base);
        {
            Writer writer(args.out, "key_rate_dv01.csv",
                          "Key-rate DV01 by curve node, with the parallel DV01 it is "
                          "supposed to decompose and the residual between them.");
            writer.stream() << "tenor,node_years,key_rate_dv01\n";
            for (std::size_t i = 0; i < key_rates.key_rate_dv01.size(); ++i) {
                writer.stream()
                    << cmt[i].tenor << ',' << fixed(key_rates.node_times[i], 6) << ','
                    << fixed(key_rates.key_rate_dv01[i], 4) << '\n';
            }
            writer.stream() << "SUM,," << fixed(key_rates.sum_of_buckets, 4) << '\n'
                            << "PARALLEL,," << fixed(key_rates.parallel_dv01, 4) << '\n'
                            << "RESIDUAL,," << fixed(key_rates.residual, 6) << '\n'
                            << "RESIDUAL_PCT,,"
                            << fixed(key_rates.relative_residual * 100.0, 6) << '\n';
        }

        // --- 8. Bucketed hedge ------------------------------------------------
        std::vector<Swap> hedges;
        const std::vector<int> hedge_tenors = {24, 60, 84, 120, 360};
        for (const int months : hedge_tenors) {
            hedges.emplace_back(1.0, 0.04, SwapDirection::Receiver, as_of,
                                as_of.add_months(months), Frequency::SemiAnnual,
                                DayCount::Thirty360, Frequency::Quarterly,
                                DayCount::Act360);
        }
        const BucketHedge hedge = solve_bucket_hedge(book, hedges, base);
        {
            Writer writer(args.out, "bucket_hedge.csv",
                          "Benchmark notionals that neutralise the book's key-rate "
                          "exposure, and the exposure that survives.");
            writer.stream() << "hedge_tenor_months,notional\n";
            for (std::size_t i = 0; i < hedges.size(); ++i) {
                writer.stream()
                    << hedge_tenors[i] << ',' << fixed(hedge.notionals[i], 2) << '\n';
            }
            writer.stream() << "# residual key-rate DV01 after hedging\n"
                               "tenor,node_years,residual_key_rate_dv01\n";
            for (std::size_t i = 0; i < hedge.residual_krd.size(); ++i) {
                writer.stream()
                    << cmt[i].tenor << ',' << fixed(key_rates.node_times[i], 6) << ','
                    << fixed(hedge.residual_krd[i], 6) << '\n';
            }
            writer.stream() << "WORST_BUCKET_BEFORE,,"
                            << fixed(hedge.worst_bucket_before, 4) << '\n'
                            << "WORST_BUCKET_AFTER,,"
                            << fixed(hedge.worst_bucket_after, 6) << '\n'
                            << "RESIDUAL_DV01,," << fixed(hedge.residual_dv01, 6)
                            << '\n';
        }

        // --- 9. Duration vs duration-plus-convexity ---------------------------
        const std::vector<double> shifts = {-200, -150, -100, -50, -25,
                                            25,   50,   100,  150, 200};
        const std::vector<ShiftAttribution> attribution =
            shift_attribution(book, base, shifts);
        {
            Writer writer(args.out, "shift_attribution.csv",
                          "Actual P&L under a parallel shift against the duration-only "
                          "and duration-plus-convexity predictions.");
            writer.stream() << "shift_bp,actual_pnl,duration_only,"
                               "duration_plus_convexity,duration_error,"
                               "with_convexity_error\n";
            for (const ShiftAttribution& row : attribution) {
                writer.stream()
                    << fixed(row.shift_bp, 0) << ',' << fixed(row.actual_pnl, 2) << ','
                    << fixed(row.duration_only, 2) << ','
                    << fixed(row.duration_plus_convexity, 2) << ','
                    << fixed(row.duration_error, 2) << ','
                    << fixed(row.with_convexity_error, 2) << '\n';
            }
        }

        // --- 10. Scenario P&L -------------------------------------------------
        const std::vector<ScenarioPnL> scenarios =
            run_scenarios(book, base, standard_scenarios());
        {
            Writer writer(args.out, "scenario_pnl.csv",
                          "Portfolio P&L under the standard curve scenarios.");
            writer.stream() << "scenario,base_pv,scenario_pv,pnl\n";
            for (const ScenarioPnL& row : scenarios) {
                writer.stream()
                    << '"' << row.name << "\"," << fixed(row.base_pv, 2) << ','
                    << fixed(row.scenario_pv, 2) << ',' << fixed(row.pnl, 2) << '\n';
            }
        }

        // --- 11. Run metadata -------------------------------------------------
        {
            std::ofstream meta(args.out + "/run.meta.json");
            meta << "{\n"
                 << "  \"produced_by\": \"./build/fi_report --out " << args.out
                 << "\",\n"
                 << "  \"engine_version\": \"" << version_string << "\",\n"
                 << "  \"curve_as_of\": \"" << args.as_of << "\",\n"
                 << "  \"data_source\": \"data/treasury_par_yields_2025.csv (US "
                    "Treasury Daily Par Yield Curve Rates, CMT)\",\n"
                 << "  \"instruments\": " << instruments.size() << ",\n"
                 << "  \"interpolation_schemes\": [";
            for (std::size_t i = 0; i < schemes.size(); ++i) {
                meta << (i ? ", " : "") << '"' << to_string(schemes[i]) << '"';
            }
            meta << "],\n"
                 << "  \"worst_repricing_residual_bp\": "
                 << scientific(worst_residual_bp, 3) << ",\n"
                 << "  \"nss_rmse_bp\": " << fixed(nss_rmse_bp, 4) << ",\n"
                 << "  \"portfolio_total_pv\": \""
                 << valuation.total_pv.to_string_with_currency() << "\",\n"
                 << "  \"portfolio_dv01_per_bp\": " << fixed(valuation.total_dv01, 2)
                 << ",\n"
                 << "  \"key_rate_residual_pct_of_parallel\": "
                 << fixed(key_rates.relative_residual * 100.0, 6) << "\n"
                 << "}\n";
            // Deliberately no timestamp and no machine identity: nothing here is
            // hardware-dependent or time-dependent, and a timestamp would make
            // the file differ on every run, which would defeat the CI check that
            // the committed artifacts still regenerate byte for byte.
        }

        std::cout << "Curve as of " << args.as_of << " from " << instruments.size()
                  << " CMT par yields\n"
                  << "  worst repricing residual : " << scientific(worst_residual_bp, 3)
                  << " bp (across all three interpolation schemes)\n"
                  << "  NSS fit RMSE             : " << fixed(nss_rmse_bp, 4) << " bp\n"
                  << "  portfolio PV             : "
                  << valuation.total_pv.to_string_with_currency() << "\n"
                  << "  portfolio DV01           : " << fixed(valuation.total_dv01, 2)
                  << " per bp\n"
                  << "  key-rate sum vs parallel : "
                  << fixed(key_rates.relative_residual * 100.0, 4) << "% residual\n"
                  << "  hedged worst bucket      : "
                  << fixed(hedge.worst_bucket_before, 1) << " -> "
                  << fixed(hedge.worst_bucket_after, 4) << " per bp\n"
                  << "artifacts written to " << args.out << "/\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fi_report: " << error.what() << "\n";
        return 1;
    }
}
