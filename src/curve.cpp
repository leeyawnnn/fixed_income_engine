#include "fi/curve.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace fi {

Curve::Curve(Date reference_date, DayCount day_count, std::vector<double> times,
             std::vector<double> zeros)
    : times_(std::move(times)),
      zeros_(std::move(zeros)),
      reference_date_(reference_date),
      day_count_(day_count) {
    if (times_.empty() || times_.size() != zeros_.size()) {
        throw std::invalid_argument("Curve: need matching, non-empty node vectors");
    }
    for (std::size_t i = 0; i < times_.size(); ++i) {
        if (times_[i] <= 0.0) {
            throw std::invalid_argument("Curve: node times must be positive");
        }
        if (i > 0 && !(times_[i] > times_[i - 1])) {
            throw std::invalid_argument(
                "Curve: node times must be strictly increasing");
        }
    }
}

double Curve::time_to(const Date& date) const {
    return year_fraction(reference_date_, date, day_count_);
}

void Curve::locate(double t, std::size_t& lo, std::size_t& hi, double& w) const {
    const auto it = std::upper_bound(times_.begin(), times_.end(), t);
    hi = static_cast<std::size_t>(it - times_.begin());
    lo = hi - 1;
    w = (t - times_[lo]) / (times_[hi] - times_[lo]);
}

double Curve::zero_rate(double t) const {
    if (t <= 0.0) return zeros_.front();  // instantaneous proxy
    return -std::log(discount(t)) / t;
}

double Curve::instantaneous_forward(double t) const {
    // Central difference on -ln DF, one-sided at t = 0 where there is no left
    // neighbour. The step is small enough to resolve a kink and large enough
    // that the difference of two logs does not lose its leading digits.
    constexpr double kStep = 1e-6;
    const double lo = std::max(0.0, t - kStep);
    const double hi = t + kStep;
    return (std::log(discount(lo)) - std::log(discount(hi))) / (hi - lo);
}

double Curve::forward_rate(double t1, double t2) const {
    if (!(t2 > t1)) {
        throw std::invalid_argument("Curve::forward_rate: require t2 > t1");
    }
    return (std::log(discount(t1)) - std::log(discount(t2))) / (t2 - t1);
}

double LinearInterpCurve::discount(double t) const {
    if (t <= 0.0) return 1.0;
    if (t <= times_.front()) return std::exp(-zeros_.front() * t);
    if (t >= times_.back()) return std::exp(-zeros_.back() * t);

    std::size_t lo, hi;
    double w;
    locate(t, lo, hi, w);
    const double z = zeros_[lo] + w * (zeros_[hi] - zeros_[lo]);
    return std::exp(-z * t);
}

std::unique_ptr<Curve> LinearInterpCurve::with_zero_rates(
    std::vector<double> zeros) const {
    return std::make_unique<LinearInterpCurve>(reference_date(), day_count(), times_,
                                               std::move(zeros));
}

LogLinearCurve::LogLinearCurve(Date reference_date, DayCount day_count,
                               std::vector<double> times, std::vector<double> zeros)
    : Curve(reference_date, day_count, std::move(times), std::move(zeros)) {
    log_df_.reserve(times_.size());
    for (std::size_t i = 0; i < times_.size(); ++i) {
        log_df_.push_back(-zeros_[i] * times_[i]);  // ln DF at node i
    }
}

double LogLinearCurve::discount(double t) const {
    if (t <= 0.0) return 1.0;
    if (t <= times_.front()) return std::exp(-zeros_.front() * t);
    if (t >= times_.back()) return std::exp(-zeros_.back() * t);

    std::size_t lo, hi;
    double w;
    locate(t, lo, hi, w);
    const double ld = log_df_[lo] + w * (log_df_[hi] - log_df_[lo]);
    return std::exp(ld);
}

std::unique_ptr<Curve> LogLinearCurve::with_zero_rates(
    std::vector<double> zeros) const {
    return std::make_unique<LogLinearCurve>(reference_date(), day_count(), times_,
                                            std::move(zeros));
}

// --- Interpolation dispatch --------------------------------------------------

const char* to_string(Interpolation scheme) noexcept {
    switch (scheme) {
        case Interpolation::LinearZero: return "linear-zero";
        case Interpolation::LogLinearDiscount: return "log-linear-df";
        case Interpolation::MonotoneConvex: return "monotone-convex";
    }
    return "unknown";
}

std::unique_ptr<Curve> make_curve(Interpolation scheme, Date reference_date,
                                  DayCount day_count, std::vector<double> times,
                                  std::vector<double> zeros) {
    switch (scheme) {
        case Interpolation::LinearZero:
            return std::make_unique<LinearInterpCurve>(
                reference_date, day_count, std::move(times), std::move(zeros));
        case Interpolation::LogLinearDiscount:
            return std::make_unique<LogLinearCurve>(reference_date, day_count,
                                                    std::move(times), std::move(zeros));
        case Interpolation::MonotoneConvex:
            return std::make_unique<MonotoneConvexCurve>(
                reference_date, day_count, std::move(times), std::move(zeros));
    }
    throw std::invalid_argument("make_curve: unknown interpolation scheme");
}

// --- Hagan-West monotone convex ---------------------------------------------

namespace {

// On one interval, write f(x) = fd + g(x) with g(0) = f_{i-1} - fd and
// g(1) = f_i - fd. Hagan-West section 4 gives four closed forms for g, chosen
// so the interpolant stays monotone; `Sector` holds one interval's endpoints
// and evaluates whichever applies.
//
// Every form satisfies integral(1) == 0. That is not decoration: it is exactly
// the statement that the interval's average forward is fd, which is what makes
// the node zero rates reproduce and therefore what makes a curve bootstrapped
// under this scheme reprice its inputs.
class Sector {
public:
    Sector(double g0, double g1) : g0_(g0), g1_(g1) {}

    double value(double x) const {
        switch (region()) {
            case Region::Flat: return 0.0;
            case Region::Quadratic:
                return g0_ * (1.0 - 4.0 * x + 3.0 * x * x) +
                       g1_ * (-2.0 * x + 3.0 * x * x);
            case Region::FlatThenRise: {
                const double eta = eta_flat_then_rise();
                if (x <= eta) return g0_;
                const double u = (x - eta) / (1.0 - eta);
                return g0_ + (g1_ - g0_) * u * u;
            }
            case Region::FallThenFlat: {
                const double eta = eta_fall_then_flat();
                if (x >= eta) return g1_;
                const double u = (eta - x) / eta;
                return g1_ + (g0_ - g1_) * u * u;
            }
            case Region::TwoSided: {
                const double eta = eta_two_sided();
                const double a = amplitude();
                if (x <= eta) {
                    const double u = (eta - x) / eta;
                    return a + (g0_ - a) * u * u;
                }
                const double u = (x - eta) / (1.0 - eta);
                return a + (g1_ - a) * u * u;
            }
        }
        return 0.0;
    }

    // integral of g from 0 to x.
    double integral(double x) const {
        switch (region()) {
            case Region::Flat: return 0.0;
            case Region::Quadratic:
                return g0_ * (x - 2.0 * x * x + x * x * x) + g1_ * (x * x * x - x * x);
            case Region::FlatThenRise: {
                const double eta = eta_flat_then_rise();
                if (x <= eta) return g0_ * x;
                const double d = x - eta;
                return g0_ * x + (g1_ - g0_) * d * d * d / (3.0 * sq(1.0 - eta));
            }
            case Region::FallThenFlat: {
                const double eta = eta_fall_then_flat();
                const double at_eta = g1_ * eta + (g0_ - g1_) * eta / 3.0;
                if (x >= eta) return at_eta + g1_ * (x - eta);
                const double d = eta - x;
                return g1_ * x +
                       (g0_ - g1_) * (eta * eta * eta - d * d * d) / (3.0 * sq(eta));
            }
            case Region::TwoSided: {
                const double eta = eta_two_sided();
                const double a = amplitude();
                const double at_eta = a * eta + (g0_ - a) * eta / 3.0;
                if (x <= eta) {
                    const double d = eta - x;
                    return a * x +
                           (g0_ - a) * (eta * eta * eta - d * d * d) / (3.0 * sq(eta));
                }
                const double d = x - eta;
                return at_eta + a * d + (g1_ - a) * d * d * d / (3.0 * sq(1.0 - eta));
            }
        }
        return 0.0;
    }

private:
    enum class Region : std::uint8_t {
        Flat,
        Quadratic,
        FlatThenRise,
        FallThenFlat,
        TwoSided,
    };

    static double sq(double v) { return v * v; }

    Region region() const {
        if (g0_ == 0.0 && g1_ == 0.0) return Region::Flat;
        // The plain quadratic is monotone when g1 lies between the two
        // boundaries -g0/2 and -2*g0. Which of those is the lower bound flips
        // with the sign of g0, so compare against min and max rather than
        // writing the inequalities out per sign and getting them backwards.
        const double lower = std::min(-0.5 * g0_, -2.0 * g0_);
        const double upper = std::max(-0.5 * g0_, -2.0 * g0_);
        if (g1_ >= lower && g1_ <= upper) return Region::Quadratic;
        if ((g0_ < 0.0 && g1_ > -2.0 * g0_) || (g0_ > 0.0 && g1_ < -2.0 * g0_)) {
            return Region::FlatThenRise;
        }
        if ((g0_ > 0.0 && g1_ < 0.0 && g1_ > -0.5 * g0_) ||
            (g0_ < 0.0 && g1_ > 0.0 && g1_ < -0.5 * g0_)) {
            return Region::FallThenFlat;
        }
        return Region::TwoSided;
    }

    // Each eta is a crossover point inside (0, 1). The clamps keep a value that
    // rounding pushed to an endpoint from dividing by zero; they never change a
    // well-conditioned result.
    static double safe_eta(double eta) { return std::clamp(eta, 1e-12, 1.0 - 1e-12); }

    double eta_flat_then_rise() const {
        return safe_eta((g1_ + 2.0 * g0_) / (g1_ - g0_));
    }
    double eta_fall_then_flat() const { return safe_eta(3.0 * g1_ / (g1_ - g0_)); }
    double eta_two_sided() const { return safe_eta(g1_ / (g1_ + g0_)); }
    double amplitude() const { return -g0_ * g1_ / (g0_ + g1_); }

    double g0_;
    double g1_;
};

}  // namespace

MonotoneConvexCurve::MonotoneConvexCurve(Date reference_date, DayCount day_count,
                                         std::vector<double> times,
                                         std::vector<double> zeros)
    : Curve(reference_date, day_count, std::move(times), std::move(zeros)) {
    build_forwards();
}

void MonotoneConvexCurve::build_forwards() {
    const std::size_t n = times_.size();

    // Prepend t = 0, where -ln DF is zero by definition. The first interval is
    // then a genuine interpolation interval rather than a flat stub, which is
    // the scheme's own treatment of the front end.
    grid_.assign(n + 1, 0.0);
    zt_.assign(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        grid_[i + 1] = times_[i];
        zt_[i + 1] = zeros_[i] * times_[i];
    }

    // Discrete forward over each interval: the average instantaneous forward
    // that reproduces the interval's change in -ln DF.
    fd_.assign(n + 1, 0.0);
    for (std::size_t i = 1; i <= n; ++i) {
        fd_[i] = (zt_[i] - zt_[i - 1]) / (grid_[i] - grid_[i - 1]);
    }
    fd_[0] = fd_[1];

    // Instantaneous forwards at the nodes, by interpolating the discrete
    // forwards on either side; the ends extrapolate from the single neighbour.
    f_.assign(n + 1, 0.0);
    if (n == 1) {
        f_[0] = fd_[1];
        f_[1] = fd_[1];
    } else {
        for (std::size_t i = 1; i < n; ++i) {
            const double left = grid_[i] - grid_[i - 1];
            const double right = grid_[i + 1] - grid_[i];
            const double span = grid_[i + 1] - grid_[i - 1];
            f_[i] = (left / span) * fd_[i + 1] + (right / span) * fd_[i];
        }
        f_[0] = fd_[1] - 0.5 * (f_[1] - fd_[1]);
        f_[n] = fd_[n] - 0.5 * (f_[n - 1] - fd_[n]);
    }

    // Hagan-West's positivity collar. The upper bound is guarded against a
    // negative discrete forward, where 2*min(...) would sit below the lower
    // bound; there the collar degenerates to zero, and the scheme cannot
    // represent the negative forward the data implies.
    const auto collar = [](double value, double bound) {
        return std::clamp(value, 0.0, std::max(0.0, bound));
    };
    f_[0] = collar(f_[0], 2.0 * fd_[1]);
    for (std::size_t i = 1; i < n; ++i) {
        f_[i] = collar(f_[i], 2.0 * std::min(fd_[i], fd_[i + 1]));
    }
    f_[n] = collar(f_[n], 2.0 * fd_[n]);
}

double MonotoneConvexCurve::discount(double t) const {
    if (t <= 0.0) return 1.0;
    // Beyond the last node, hold the zero rate flat. This matches the other two
    // curves so a comparison between schemes is about what they do between
    // nodes, not about three different extrapolation conventions.
    if (t >= grid_.back()) return std::exp(-zeros_.back() * t);

    const auto it = std::upper_bound(grid_.begin(), grid_.end(), t);
    const auto hi = static_cast<std::size_t>(it - grid_.begin());
    const std::size_t lo = hi - 1;

    const double width = grid_[hi] - grid_[lo];
    const double x = (t - grid_[lo]) / width;
    const Sector sector{f_[lo] - fd_[hi], f_[hi] - fd_[hi]};

    const double minus_log_df =
        zt_[lo] + fd_[hi] * (t - grid_[lo]) + width * sector.integral(x);
    return std::exp(-minus_log_df);
}

double MonotoneConvexCurve::instantaneous_forward(double t) const {
    if (t >= grid_.back()) return zeros_.back();  // matches the flat-zero tail
    const double clamped = std::max(t, 0.0);

    const auto it = std::upper_bound(grid_.begin(), grid_.end(), clamped);
    auto hi = static_cast<std::size_t>(it - grid_.begin());
    if (hi == 0) hi = 1;  // t == 0 sits on the left edge of the first interval
    const std::size_t lo = hi - 1;

    const double x = (clamped - grid_[lo]) / (grid_[hi] - grid_[lo]);
    const Sector sector{f_[lo] - fd_[hi], f_[hi] - fd_[hi]};
    return fd_[hi] + sector.value(x);
}

std::unique_ptr<Curve> MonotoneConvexCurve::with_zero_rates(
    std::vector<double> zeros) const {
    return std::make_unique<MonotoneConvexCurve>(reference_date(), day_count(), times_,
                                                 std::move(zeros));
}

}  // namespace fi
