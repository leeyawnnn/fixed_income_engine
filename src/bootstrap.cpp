#include "fi/bootstrap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

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
double fixed_annuity(const Curve& curve, const Date& start, const SwapQuote& q) {
    double a = 0.0;
    Date prev = start;
    for (const Date& d : fixed_schedule(start, q.maturity, q.fixed_frequency)) {
        a += year_fraction(prev, d, q.fixed_day_count) * curve.discount(d);
        prev = d;
    }
    return a;
}

// Coupon/payment dates for a par bond, same backward-from-maturity scheme.
double par_bond_annuity(const Curve& curve, const Date& start, const ParBondQuote& q) {
    double a = 0.0;
    Date prev = start;
    for (const Date& d : fixed_schedule(start, q.maturity, q.frequency)) {
        a += year_fraction(prev, d, q.day_count) * curve.discount(d);
        prev = d;
    }
    return a;
}

std::string tenor_label(const Date& reference_date, const Date& maturity) {
    const double years = year_fraction(reference_date, maturity, DayCount::Act365);
    char buffer[32];
    if (years < 0.95) {
        std::snprintf(buffer, sizeof(buffer), "%gM",
                      std::round(years * 12.0 * 2.0) / 2.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%gY", std::round(years));
    }
    return buffer;
}

}  // namespace

double implied_deposit_rate(const Curve& curve, const DepositQuote& q) {
    const double tau = year_fraction(curve.reference_date(), q.maturity, q.day_count);
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

double implied_par_bond_yield(const Curve& curve, const ParBondQuote& q) {
    const Date& start = curve.reference_date();
    return (1.0 - curve.discount(q.maturity)) / par_bond_annuity(curve, start, q);
}

Date instrument_maturity(const BootstrapInstrument& inst) noexcept {
    if (const auto* d = std::get_if<DepositQuote>(&inst)) return d->maturity;
    if (const auto* f = std::get_if<FuturesQuote>(&inst)) return f->end;
    if (const auto* b = std::get_if<ParBondQuote>(&inst)) return b->maturity;
    return std::get<SwapQuote>(inst).maturity;
}

double quoted_rate(const BootstrapInstrument& inst) noexcept {
    if (const auto* d = std::get_if<DepositQuote>(&inst)) return d->rate;
    if (const auto* f = std::get_if<FuturesQuote>(&inst)) return f->rate;
    if (const auto* b = std::get_if<ParBondQuote>(&inst)) return b->par_yield;
    return std::get<SwapQuote>(inst).rate;
}

double repricing_residual(const Curve& curve, const BootstrapInstrument& inst) {
    if (const auto* d = std::get_if<DepositQuote>(&inst)) {
        return implied_deposit_rate(curve, *d) - d->rate;
    }
    if (const auto* f = std::get_if<FuturesQuote>(&inst)) {
        return implied_futures_rate(curve, *f) - f->rate;
    }
    if (const auto* b = std::get_if<ParBondQuote>(&inst)) {
        return implied_par_bond_yield(curve, *b) - b->par_yield;
    }
    const auto& swp = std::get<SwapQuote>(inst);
    return par_swap_rate(curve, swp) - swp.rate;
}

std::string instrument_label(const BootstrapInstrument& inst,
                             const Date& reference_date) {
    const std::string tenor = tenor_label(reference_date, instrument_maturity(inst));
    if (std::holds_alternative<DepositQuote>(inst)) return "Deposit " + tenor;
    if (std::holds_alternative<FuturesQuote>(inst)) return "Future " + tenor;
    if (std::holds_alternative<ParBondQuote>(inst)) return "UST par " + tenor;
    return "Swap " + tenor;
}

std::unique_ptr<Curve> bootstrap_curve(const Date& reference_date,
                                       DayCount curve_day_count,
                                       std::vector<BootstrapInstrument> instruments,
                                       Interpolation scheme, const SolverConfig& cfg) {
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
            throw std::invalid_argument(
                "bootstrap: instrument maturity must be after reference date");
        }
        times.push_back(t);
        zeros.push_back(-std::log(df) / t);  // continuously-compounded zero
    };

    for (const BootstrapInstrument& inst : instruments) {
        if (const auto* dep = std::get_if<DepositQuote>(&inst)) {
            const double tau =
                year_fraction(reference_date, dep->maturity, dep->day_count);
            add_node(time_of(dep->maturity), 1.0 / (1.0 + dep->rate * tau));

        } else if (const auto* fut = std::get_if<FuturesQuote>(&inst)) {
            if (times.empty()) {
                throw std::invalid_argument(
                    "bootstrap: a futures/FRA needs a prior short-end node");
            }
            const LogLinearCurve current{reference_date, curve_day_count, times, zeros};
            const double df_start = current.discount(fut->start);
            const double tau = year_fraction(fut->start, fut->end, fut->day_count);
            add_node(time_of(fut->end), df_start / (1.0 + fut->rate * tau));

        } else if (const auto* bond = std::get_if<ParBondQuote>(&inst)) {
            const double t = time_of(bond->maturity);

            // Same shape as the swap solve: the par relation is monotone in the
            // candidate zero rate, so Newton with a bisection safeguard over
            // [-0.5, 1.0] cannot run away.
            auto residual = [&](double z) {
                std::vector<double> ts = times;
                std::vector<double> zs = zeros;
                ts.push_back(t);
                zs.push_back(z);
                const LogLinearCurve trial{reference_date, curve_day_count,
                                           std::move(ts), std::move(zs)};
                return implied_par_bond_yield(trial, *bond) - bond->par_yield;
            };
            auto derivative = [&](double z) {
                const double h = 1e-6;
                return (residual(z + h) - residual(z - h)) / (2.0 * h);
            };

            const SolverResult r =
                newton_bisection(residual, derivative, bond->par_yield, -0.5, 1.0, cfg);
            // A failed solve here is not fatal: this pass only produces the
            // starting guess for the refinement below, which re-solves every
            // node against the whole curve and reports the real failure.
            times.push_back(t);
            zeros.push_back(r.converged ? r.root : bond->par_yield);

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
                const LogLinearCurve trial{reference_date, curve_day_count,
                                           std::move(ts), std::move(zs)};
                return par_swap_rate(trial, swp) - swp.rate;
            };
            auto derivative = [&](double z) {
                const double h = 1e-6;
                return (residual(z + h) - residual(z - h)) / (2.0 * h);
            };

            SolverResult r =
                newton_bisection(residual, derivative, swp.rate, -0.5, 1.0, cfg);
            times.push_back(t);
            zeros.push_back(r.converged ? r.root : swp.rate);
        }
    }

    // --- Global refinement -------------------------------------------------
    //
    // The sequential pass above assumes locality: that pinning a node at a long
    // maturity leaves discount factors at shorter tenors alone. That is true of
    // log-linear interpolation, where a node only touches the two intervals
    // either side of it, which is why the pass runs log-linearly whatever
    // scheme was asked for - it is being used as a starting guess, and it is a
    // guess that reprices exactly for one of the three schemes.
    //
    // Locality fails for monotone convex. There the instantaneous forward at a
    // node is built from the discrete forwards on *both* sides, so adding the
    // 10Y node moves the curve back through 7Y and 5Y and the instruments
    // already bootstrapped stop repricing. Measured on the 2025-12-31 Treasury
    // curve, a one-pass monotone convex bootstrap holds 1e-12 bp through 1Y,
    // drifts to 0.02 bp by 3Y and 394 bp by 20Y, and the 30Y solve then fails
    // outright.
    //
    // So each node is re-solved against the whole curve in Gauss-Seidel sweeps
    // until every instrument reprices. Log-linear converges on the first sweep
    // because it is already exact; monotone convex takes about fifteen.
    constexpr int kMaxSweeps = 100;
    constexpr double kResidualTolerance = 1e-14;  // rate units, i.e. 1e-10 bp

    const auto worst_residual = [&](const std::vector<double>& candidate) {
        const auto trial =
            make_curve(scheme, reference_date, curve_day_count, times, candidate);
        double worst = 0.0;
        for (const BootstrapInstrument& inst : instruments) {
            worst = std::max(worst, std::abs(repricing_residual(*trial, inst)));
        }
        return worst;
    };

    std::vector<double> best = zeros;
    double best_worst = worst_residual(best);

    for (int sweep = 0; sweep < kMaxSweeps && best_worst > kResidualTolerance;
         ++sweep) {
        for (std::size_t i = 0; i < instruments.size(); ++i) {
            auto residual = [&](double z) {
                std::vector<double> candidate = zeros;
                candidate[i] = z;
                const auto trial = make_curve(scheme, reference_date, curve_day_count,
                                              times, std::move(candidate));
                return repricing_residual(*trial, instruments[i]);
            };
            auto derivative = [&](double z) {
                const double h = 1e-7;
                return (residual(z + h) - residual(z - h)) / (2.0 * h);
            };
            const SolverResult r =
                newton_bisection(residual, derivative, zeros[i], -0.5, 1.0, cfg);
            if (r.converged) zeros[i] = r.root;
        }

        // Progress is not monotone in the first few sweeps - a node moved to
        // fix its own instrument can briefly worsen a neighbour - so keep the
        // best curve seen rather than the last one.
        if (const double worst = worst_residual(zeros); worst < best_worst) {
            best_worst = worst;
            best = zeros;
        }
    }

    if (best_worst > 1e-10) {  // 1e-6 bp, far below any market tolerance
        throw std::runtime_error("bootstrap: could not reprice all instruments under " +
                                 std::string(to_string(scheme)) +
                                 " interpolation (worst residual " +
                                 std::to_string(best_worst * 1e4) + " bp)");
    }
    return make_curve(scheme, reference_date, curve_day_count, std::move(times),
                      std::move(best));
}

}  // namespace fi
