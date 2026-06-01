#include "fi/bootstrap.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace fi {

namespace {

// Fixed-leg payment dates, anchored to maturity and stepped backwards so
// end-of-month clamping can't drift the schedule (same scheme as Bond).
std::vector<Date> fixed_schedule(const Date& start, const Date& maturity,
                                 Frequency freq) {
    const int step = 12 / per_year(freq);
    std::vector<Date> dates;
    for (int k = 0;; ++k) {
        Date d = maturity.add_months(-step * k);
        if (!(d > start)) break;
        dates.push_back(d);
    }
    std::reverse(dates.begin(), dates.end());
    return dates;
}

// Fixed-leg annuity Σ τ_j · DF(t_j) for a unit-rate fixed leg.
double fixed_annuity(const Curve& curve, const Date& start,
                     const SwapQuote& q) {
    double a = 0.0;
    Date prev = start;
    for (const Date& d : fixed_schedule(start, q.maturity, q.fixed_frequency)) {
        a += year_fraction(prev, d, q.fixed_day_count) * curve.discount(d);
        prev = d;
    }
    return a;
}

Date instrument_maturity(const BootstrapInstrument& inst) {
    if (auto* d = std::get_if<DepositQuote>(&inst)) return d->maturity;
    if (auto* f = std::get_if<FuturesQuote>(&inst)) return f->end;
    return std::get<SwapQuote>(inst).maturity;
}

}  // namespace

double implied_deposit_rate(const Curve& curve, const DepositQuote& q) {
    const double tau =
        year_fraction(curve.reference_date(), q.maturity, q.day_count);
    const double df = curve.discount(q.maturity);
    return (1.0 / df - 1.0) / tau;
}

double implied_futures_rate(const Curve& curve, const FuturesQuote& q) {
    const double df_start = curve.discount(q.start);
    const double df_end = curve.discount(q.end);
    const double tau = year_fraction(q.start, q.end, q.day_count);
    return (df_start / df_end - 1.0) / tau;
}

double par_swap_rate(const Curve& curve, const SwapQuote& q) {
    const Date& start = curve.reference_date();
    const double floating = curve.discount(start) - curve.discount(q.maturity);
    return floating / fixed_annuity(curve, start, q);
}

std::unique_ptr<Curve> bootstrap_curve(const Date& reference_date,
                                       DayCount curve_day_count,
                                       std::vector<BootstrapInstrument> instruments,
                                       const SolverConfig& cfg) {
    std::sort(instruments.begin(), instruments.end(),
              [](const BootstrapInstrument& a, const BootstrapInstrument& b) {
                  return instrument_maturity(a) < instrument_maturity(b);
              });

    std::vector<double> times;
    std::vector<double> zeros;

    const auto time_of = [&](const Date& d) {
        return year_fraction(reference_date, d, curve_day_count);
    };
    const auto add_node = [&](double t, double df) {
        if (!(t > 0.0)) {
            throw std::invalid_argument("bootstrap: instrument maturity must be after reference date");
        }
        times.push_back(t);
        zeros.push_back(-std::log(df) / t);  // continuously-compounded zero
    };

    for (const BootstrapInstrument& inst : instruments) {
        if (auto* dep = std::get_if<DepositQuote>(&inst)) {
            const double tau =
                year_fraction(reference_date, dep->maturity, dep->day_count);
            add_node(time_of(dep->maturity), 1.0 / (1.0 + dep->rate * tau));

        } else if (auto* fut = std::get_if<FuturesQuote>(&inst)) {
            if (times.empty()) {
                throw std::invalid_argument("bootstrap: a futures/FRA needs a prior short-end node");
            }
            LogLinearCurve current{reference_date, curve_day_count, times, zeros};
            const double df_start = current.discount(fut->start);
            const double tau =
                year_fraction(fut->start, fut->end, fut->day_count);
            add_node(time_of(fut->end), df_start / (1.0 + fut->rate * tau));

        } else {
            const auto& swp = std::get<SwapQuote>(inst);
            const double t = time_of(swp.maturity);

            // Residual: par rate of a trial curve (existing nodes + candidate
            // node at t) minus the quoted rate. Monotonic in the candidate
            // zero rate, so the [-0.5, 1.0] bracket lets Newton bisect safely.
            auto residual = [&](double z) {
                std::vector<double> ts = times;
                std::vector<double> zs = zeros;
                ts.push_back(t);
                zs.push_back(z);
                LogLinearCurve trial{reference_date, curve_day_count, ts, zs};
                return par_swap_rate(trial, swp) - swp.rate;
            };
            auto derivative = [&](double z) {
                const double h = 1e-6;
                return (residual(z + h) - residual(z - h)) / (2.0 * h);
            };

            SolverResult r =
                newton_bisection(residual, derivative, swp.rate, -0.5, 1.0, cfg);
            if (!r.converged) {
                throw std::runtime_error("bootstrap: swap node did not converge");
            }
            times.push_back(t);
            zeros.push_back(r.root);
        }
    }

    return std::make_unique<LogLinearCurve>(reference_date, curve_day_count,
                                            std::move(times), std::move(zeros));
}

}  // namespace fi
