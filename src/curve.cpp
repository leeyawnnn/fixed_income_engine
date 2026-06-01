#include "fi/curve.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

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
            throw std::invalid_argument("Curve: node times must be strictly increasing");
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
    return std::make_unique<LinearInterpCurve>(reference_date(), day_count(),
                                               times_, std::move(zeros));
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
    return std::make_unique<LogLinearCurve>(reference_date(), day_count(),
                                            times_, std::move(zeros));
}

}  // namespace fi
