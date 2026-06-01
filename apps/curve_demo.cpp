// curve_demo — end-to-end driver for the fixed-income engine.
//
//   curve_demo [--quotes data/swap_rates.csv] [--portfolio data/portfolio.json]
//
// Loads market quotes, bootstraps a zero curve, fits Nelson-Siegel-Svensson,
// prices the portfolio, and runs the standard scenario set. Writes curve.csv
// and scenarios_report.md, and prints a summary. Run from the repo root so the
// default data/ paths resolve.
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "fi/bootstrap.hpp"
#include "fi/curve.hpp"
#include "fi/json.hpp"
#include "fi/nss.hpp"
#include "fi/risk.hpp"
#include "fi/scenario.hpp"
#include "fi/swap.hpp"

using namespace fi;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Date parse_date(const std::string& s) {  // YYYY-MM-DD
    return Date(std::stoi(s.substr(0, 4)),
                static_cast<unsigned>(std::stoul(s.substr(5, 2))),
                static_cast<unsigned>(std::stoul(s.substr(8, 2))));
}

Frequency freq_from_int(int f) {
    switch (f) {
        case 1: return Frequency::Annual;
        case 2: return Frequency::SemiAnnual;
        case 4: return Frequency::Quarterly;
        case 12: return Frequency::Monthly;
    }
    throw std::runtime_error("unsupported frequency: " + std::to_string(f));
}

Date add_tenor(const Date& ref, double tenor_years) {
    return ref.add_months(static_cast<int>(std::lround(tenor_years * 12.0)));
}

struct Args {
    std::string quotes = "data/swap_rates.csv";
    std::string portfolio = "data/portfolio.json";
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "--quotes" && i + 1 < argc) a.quotes = argv[++i];
        else if (s == "--portfolio" && i + 1 < argc) a.portfolio = argv[++i];
    }
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args args = parse_args(argc, argv);

        // --- Portfolio (optional): provides valuation date + the swap book. ---
        Date ref{2024, 1, 2};
        json::Value pf;
        bool have_pf = false;
        try {
            pf = json::parse(read_file(args.portfolio));
            have_pf = true;
        } catch (const std::exception& e) {
            std::cerr << "warning: portfolio not loaded (" << e.what() << ")\n";
        }
        if (have_pf && pf.contains("valuation_date")) {
            ref = parse_date(pf["valuation_date"].as_string());
        }

        // --- Market quotes -> bootstrap instruments. ---
        std::vector<BootstrapInstrument> instruments;
        std::istringstream in(read_file(args.quotes));
        std::string line;
        std::getline(in, line);  // header
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string kind, tenor_s, rate_s;
            std::getline(ss, kind, ',');
            std::getline(ss, tenor_s, ',');
            std::getline(ss, rate_s, ',');
            const Date mat = add_tenor(ref, std::stod(tenor_s));
            const double rate = std::stod(rate_s);
            if (kind == "deposit")
                instruments.push_back(DepositQuote{mat, rate, DayCount::Act360});
            else if (kind == "swap")
                instruments.push_back(SwapQuote{mat, rate, Frequency::SemiAnnual,
                                                DayCount::Thirty360});
            else
                throw std::runtime_error("unknown instrument: " + kind);
        }
        if (instruments.empty()) throw std::runtime_error("no instruments loaded");

        auto curve = bootstrap_curve(ref, DayCount::Act365, instruments);

        std::cout << "Valuation date: " << ref.to_string() << "\n\n";

        // --- Curve output (stdout + curve.csv). ---
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "Bootstrapped zero curve:\n";
        std::cout << "   tenor   zero_rate    discount\n";
        {
            std::ofstream csv("curve.csv");
            csv << "tenor,zero_rate,discount\n";
            const auto& ts = curve->node_times();
            const auto& zs = curve->node_zero_rates();
            for (std::size_t i = 0; i < ts.size(); ++i) {
                const double df = curve->discount(ts[i]);
                std::cout << std::setw(8) << ts[i] << std::setw(12) << zs[i]
                          << std::setw(12) << df << "\n";
                csv << ts[i] << "," << zs[i] << "," << df << "\n";
            }
        }
        std::cout << "  (written to curve.csv)\n\n";

        // --- NSS fit to the bootstrapped zero rates. ---
        {
            const auto fit = fit_nss(curve->node_times(), curve->node_zero_rates());
            std::cout << "Nelson-Siegel-Svensson fit (RMSE " << std::setprecision(2)
                      << fit.rmse * 1e4 << " bp):\n"
                      << std::setprecision(6);
            std::cout << "  beta0 = " << fit.params.beta0
                      << "   beta1 = " << fit.params.beta1 << "\n";
            std::cout << "  beta2 = " << fit.params.beta2
                      << "   beta3 = " << fit.params.beta3 << "\n";
            std::cout << "  lambda1 = " << fit.params.lambda1
                      << "   lambda2 = " << fit.params.lambda2 << "\n\n";
        }

        // --- Portfolio pricing. ---
        std::vector<Swap> book;
        if (have_pf && pf.contains("swaps")) {
            for (const auto& s : pf["swaps"].as_array()) {
                const int ff = s.contains("fixed_freq")
                                   ? static_cast<int>(s["fixed_freq"].number())
                                   : 2;
                const int gf = s.contains("float_freq")
                                   ? static_cast<int>(s["float_freq"].number())
                                   : 4;
                book.emplace_back(
                    s["notional"].number(), s["fixed_rate"].number(),
                    s["direction"].as_string() == "payer" ? SwapDirection::Payer
                                                           : SwapDirection::Receiver,
                    ref, add_tenor(ref, s["tenor_years"].number()),
                    freq_from_int(ff), DayCount::Thirty360, freq_from_int(gf),
                    DayCount::Act360);
            }
        }

        std::cout << std::setprecision(2);
        if (book.empty()) {
            std::cout << "No portfolio loaded; skipping pricing & scenarios.\n";
            return 0;
        }

        std::cout << "Portfolio (" << book.size() << " swaps):\n";
        double total = 0.0;
        for (std::size_t i = 0; i < book.size(); ++i) {
            const double pv = book[i].pv(*curve);
            total += pv;
            std::cout << "  swap " << (i + 1) << ": PV = " << pv << "\n";
        }
        std::cout << "  total PV = " << total << "\n\n";

        // --- Risk: parallel DV01 + key-rate DV01 (authoritative, from engine). ---
        const auto& nt = curve->node_times();
        double total_dv01 = 0.0;
        std::vector<double> krd(nt.size(), 0.0);
        for (const Swap& s : book) {
            total_dv01 += swap_dv01(s, *curve);
            const auto k = swap_key_rate_dv01(s, *curve);
            for (std::size_t i = 0; i < k.size(); ++i) krd[i] += k[i];
        }
        std::cout << std::setprecision(2);
        std::cout << "Portfolio DV01 = " << total_dv01 << " per 1bp\n";
        std::cout << "Key-rate DV01 by node tenor ($/bp):\n";
        for (std::size_t i = 0; i < nt.size(); ++i)
            std::cout << "  " << std::setw(7) << nt[i] << "y : " << krd[i] << "\n";
        std::cout << std::setprecision(6) << "Implied forward rates between nodes:\n";
        double prev_t = 0.0;
        for (std::size_t i = 0; i < nt.size(); ++i) {
            std::cout << "  [" << prev_t << ", " << nt[i] << "] = "
                      << curve->forward_rate(prev_t, nt[i]) << "\n";
            prev_t = nt[i];
        }
        std::cout << "\n";

        // --- Scenario P&L (stdout + scenarios_report.md). ---
        const auto results = run_scenarios(book, *curve, standard_scenarios());
        const std::string md = scenarios_report_md(results);
        std::ofstream("scenarios_report.md") << md;
        std::cout << md << "\n(written to scenarios_report.md)\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
