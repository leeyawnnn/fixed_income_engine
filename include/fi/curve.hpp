#pragma once

#include <memory>
#include <vector>

#include "fi/date.hpp"
#include "fi/day_count.hpp"

namespace fi {

// A discount curve maps a time (year fraction from the reference date) to a
// discount factor DF(t), with DF(0) = 1. Zero rates here are *continuously
// compounded*: DF(t) = exp(−z(t)·t), so z(t) = −ln DF(t) / t.
//
// Curve is an abstract behavior type (polymorphic), so inheritance is the right
// tool — unlike the plain-data Cashflow/Bond structs. Concrete curves differ
// only in how they interpolate between nodes.
class Curve {
public:
    virtual ~Curve() = default;

    // The one piece of behavior subclasses provide: DF for a time in years.
    virtual double discount(double t) const = 0;

    // A curve of the same concrete type with the node zero rates replaced (node
    // times, reference date, and day count unchanged). Used to bump curves for
    // sensitivity analysis. `zeros.size()` must equal the node count.
    virtual std::unique_ptr<Curve> with_zero_rates(std::vector<double> zeros) const = 0;

    // DF for a date (time measured by the curve's day count).
    double discount(const Date& date) const { return discount(time_to(date)); }

    // Continuously-compounded zero rate.
    double zero_rate(double t) const;
    double zero_rate(const Date& date) const { return zero_rate(time_to(date)); }

    // Continuously-compounded forward rate over [t1, t2].
    double forward_rate(double t1, double t2) const;

    // Year fraction from the reference date to `date` under the curve's convention.
    double time_to(const Date& date) const;

    const Date& reference_date() const noexcept { return reference_date_; }
    DayCount day_count() const noexcept { return day_count_; }
    const std::vector<double>& node_times() const noexcept { return times_; }
    const std::vector<double>& node_zero_rates() const noexcept { return zeros_; }

protected:
    // times must be strictly increasing and positive; zeros are continuously
    // compounded zero rates at those times. Throws std::invalid_argument
    // otherwise.
    Curve(Date reference_date, DayCount day_count, std::vector<double> times,
          std::vector<double> zeros);

    // Index `hi` such that times_[hi-1] < t < times_[hi], with the interpolation
    // weight w in [0,1] toward `hi`. Only valid for times_.front() < t < back().
    void locate(double t, std::size_t& lo, std::size_t& hi, double& w) const;

    std::vector<double> times_;
    std::vector<double> zeros_;

private:
    Date reference_date_;
    DayCount day_count_;
};

// Piecewise-linear in continuously-compounded zero rates between nodes.
// Outside the node range, the nearest node's zero rate is held flat.
class LinearInterpCurve : public Curve {
public:
    LinearInterpCurve(Date reference_date, DayCount day_count,
                      std::vector<double> times, std::vector<double> zeros)
        : Curve(reference_date, day_count, std::move(times), std::move(zeros)) {}

    using Curve::discount;  // keep the Date overload visible
    double discount(double t) const override;
    std::unique_ptr<Curve> with_zero_rates(std::vector<double> zeros) const override;
};

// Piecewise-linear in log discount factors between nodes (equivalently,
// piecewise-constant forward rates). Outside the node range, the nearest node's
// zero rate is held flat (matching LinearInterpCurve at the boundary).
class LogLinearCurve : public Curve {
public:
    LogLinearCurve(Date reference_date, DayCount day_count,
                   std::vector<double> times, std::vector<double> zeros);

    using Curve::discount;
    double discount(double t) const override;
    std::unique_ptr<Curve> with_zero_rates(std::vector<double> zeros) const override;

private:
    std::vector<double> log_df_;  // ln DF at each node
};

}  // namespace fi
