#pragma once

#include <cstdint>
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

    // Instantaneous forward f(t) = -d/dt ln DF(t). The default takes it
    // numerically by central difference, which is enough to draw a curve;
    // schemes that define f(t) analytically override it. The forward curve is
    // where interpolation schemes visibly disagree, so this is the function
    // worth plotting when comparing them.
    virtual double instantaneous_forward(double t) const;

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

// Which interpolation a curve uses between its nodes. Every scheme here
// reproduces the node zero rates exactly, so every one reprices the instruments
// it was bootstrapped from; they differ in what happens *between* nodes, and
// that difference is only really visible in the forward curve.
enum class Interpolation : std::uint8_t {
    LinearZero,         // linear in z(t)
    LogLinearDiscount,  // linear in ln DF(t), i.e. piecewise-constant forwards
    MonotoneConvex,     // Hagan-West monotone convex
};

const char* to_string(Interpolation scheme) noexcept;

// Build a curve of the requested kind. Bootstrapping and bumping both go
// through this so a curve can never silently change scheme.
std::unique_ptr<Curve> make_curve(Interpolation scheme, Date reference_date,
                                  DayCount day_count, std::vector<double> times,
                                  std::vector<double> zeros);

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
    LogLinearCurve(Date reference_date, DayCount day_count, std::vector<double> times,
                   std::vector<double> zeros);

    using Curve::discount;
    double discount(double t) const override;
    std::unique_ptr<Curve> with_zero_rates(std::vector<double> zeros) const override;

private:
    std::vector<double> log_df_;  // ln DF at each node
};

// Hagan-West monotone convex interpolation.
//
// Hagan, P. S. and West, G. (2006), "Interpolation Methods for Curve
// Construction", Applied Mathematical Finance 13(2), 89-129, section 4.
//
// The scheme interpolates the *forward* curve rather than the zero curve. From
// the node zeros it forms the discrete forward over each interval,
//   fd_i = (z_i*t_i - z_{i-1}*t_{i-1}) / (t_i - t_{i-1}),
// then instantaneous forwards at the nodes by interpolating those, then fits a
// piecewise-quadratic to the deviation g = f - fd on each interval, switching
// between four closed forms so the result stays monotone where the data is
// monotone. Because every form integrates to zero over its interval, the node
// zeros come back exactly - the same reason the simpler schemes reproduce their
// nodes.
//
// The point of using it is what the other schemes do to forwards: linear-in-zero
// interpolation produces forwards with a discontinuity at every node, and
// log-linear produces forwards that are literally a step function. Both look
// acceptable as zero curves and wrong as forward curves.
//
// It applies Hagan-West's positivity collar, which bounds each node forward into
// [0, 2*min(adjacent discrete forwards)]. That guarantees non-negative forwards,
// which also means this scheme cannot represent a curve whose data implies
// negative forwards.
class MonotoneConvexCurve : public Curve {
public:
    MonotoneConvexCurve(Date reference_date, DayCount day_count,
                        std::vector<double> times, std::vector<double> zeros);

    using Curve::discount;
    double discount(double t) const override;
    double instantaneous_forward(double t) const override;
    std::unique_ptr<Curve> with_zero_rates(std::vector<double> zeros) const override;

private:
    void build_forwards();

    // zt_[i] = z_i*t_i = -ln DF at node i, with zt_[0] = 0 at t = 0.
    std::vector<double> grid_;  // 0, t_1, ..., t_n
    std::vector<double> zt_;    // 0, z_1*t_1, ..., z_n*t_n
    std::vector<double> fd_;    // fd_[i] over (grid_[i-1], grid_[i]], i >= 1
    std::vector<double> f_;     // instantaneous forward at each grid point
};

}  // namespace fi
