#include "fi/nss.hpp"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace fi {

namespace {

constexpr int kNParams = 6;
using Vec = Eigen::VectorXd;
using Mat = Eigen::MatrixXd;

NSSParams from_vec(const Vec& p) {
    NSSParams q;
    q.beta0 = p[0];
    q.beta1 = p[1];
    q.beta2 = p[2];
    q.beta3 = p[3];
    q.lambda1 = p[4];
    q.lambda2 = p[5];
    return q;
}

Vec to_vec(const NSSParams& q) {
    Vec p(kNParams);
    p << q.beta0, q.beta1, q.beta2, q.beta3, q.lambda1, q.lambda2;
    return p;
}

double clamp_lambda(double v) { return std::clamp(v, 0.01, 60.0); }

}  // namespace

double NSSParams::yield(double tau) const {
    if (tau <= 0.0) return beta0 + beta1;  // τ→0 limit (f1→1, f2→0, f3→0)
    const double x1 = tau / lambda1;
    const double e1 = std::exp(-x1);
    const double f1 = (1.0 - e1) / x1;
    const double f2 = f1 - e1;
    const double x2 = tau / lambda2;
    const double e2 = std::exp(-x2);
    const double f3 = (1.0 - e2) / x2 - e2;
    return beta0 + beta1 * f1 + beta2 * f2 + beta3 * f3;
}

NSSParams nss_initial_guess(const std::vector<double>& taus,
                            const std::vector<double>& yields) {
    if (taus.empty() || taus.size() != yields.size()) {
        throw std::invalid_argument("nss_initial_guess: need matching non-empty data");
    }
    std::size_t i_min = 0, i_max = 0;
    for (std::size_t i = 1; i < taus.size(); ++i) {
        if (taus[i] < taus[i_min]) i_min = i;
        if (taus[i] > taus[i_max]) i_max = i;
    }
    NSSParams g;
    g.beta0 = yields[i_max];                 // long end ≈ level
    g.beta1 = yields[i_min] - yields[i_max]; // short − long
    g.beta2 = 0.0;
    g.beta3 = 0.0;
    g.lambda1 = 2.0;
    g.lambda2 = 5.0;
    return g;
}

NSSFitResult fit_nss(const std::vector<double>& taus,
                     const std::vector<double>& yields,
                     const NSSParams& initial_guess, const SolverConfig& cfg) {
    if (taus.empty() || taus.size() != yields.size()) {
        throw std::invalid_argument("fit_nss: need matching non-empty data");
    }
    const int n = static_cast<int>(taus.size());

    auto residuals = [&](const Vec& p) {
        const NSSParams q = from_vec(p);
        Vec r(n);
        for (int i = 0; i < n; ++i) r[i] = q.yield(taus[i]) - yields[i];
        return r;
    };

    Vec p = to_vec(initial_guess);
    Vec r = residuals(p);
    double cost = r.squaredNorm();
    double damping = 1e-3;

    NSSFitResult result;
    int it = 0;
    for (; it < cfg.max_iterations; ++it) {
        // Central-difference Jacobian (n × 6).
        Mat J(n, kNParams);
        for (int j = 0; j < kNParams; ++j) {
            const double h = 1e-6 * (1.0 + std::abs(p[j]));
            Vec pp = p, pm = p;
            pp[j] += h;
            pm[j] -= h;
            J.col(j) = (residuals(pp) - residuals(pm)) / (2.0 * h);
        }

        const Mat JtJ = J.transpose() * J;
        const Vec grad = J.transpose() * r;  // ∇(½cost) = Jᵀr; zero at the min
        if (grad.norm() < cfg.value_tolerance) {
            result.converged = true;
            break;
        }

        bool step_accepted = false;
        for (int tries = 0; tries < 40; ++tries) {
            // Marquardt-scaled damping on the diagonal.
            Mat A = JtJ;
            A.diagonal().array() += damping * (JtJ.diagonal().array() + 1e-12);
            const Vec delta = A.ldlt().solve(-grad);

            Vec p_new = p + delta;
            p_new[4] = clamp_lambda(p_new[4]);
            p_new[5] = clamp_lambda(p_new[5]);
            const Vec r_new = residuals(p_new);
            const double new_cost = r_new.squaredNorm();

            if (new_cost < cost) {
                const double rel = (cost - new_cost) / std::max(cost, 1e-30);
                p = p_new;
                r = r_new;
                cost = new_cost;
                damping = std::max(damping * 0.3, 1e-12);
                step_accepted = true;
                if (delta.norm() < cfg.step_tolerance ||
                    rel < cfg.value_tolerance) {
                    result.converged = true;
                }
                break;
            }
            damping *= 3.0;
            if (damping > 1e12) break;  // cannot make progress
        }

        if (result.converged) break;
        if (!step_accepted) break;  // stalled at a (local) minimum
    }

    result.params = from_vec(p);
    result.rmse = std::sqrt(cost / n);
    result.iterations = it;
    return result;
}

NSSFitResult fit_nss(const std::vector<double>& taus,
                     const std::vector<double>& yields) {
    const NSSParams base = nss_initial_guess(taus, yields);
    const SolverConfig cfg = nss_default_config();

    // Decay-scale grid (λ1 < λ2 captures a short and a long hump).
    static constexpr double kL1[] = {0.5, 1.0, 1.5, 2.0, 3.0, 5.0};
    static constexpr double kL2[] = {3.0, 5.0, 8.0, 12.0, 20.0, 30.0};

    NSSFitResult best;
    best.rmse = std::numeric_limits<double>::infinity();
    for (double l1 : kL1) {
        for (double l2 : kL2) {
            if (l2 <= l1) continue;
            NSSParams guess = base;
            guess.beta2 = 0.0;
            guess.beta3 = 0.0;
            guess.lambda1 = l1;
            guess.lambda2 = l2;
            NSSFitResult r = fit_nss(taus, yields, guess, cfg);
            if (r.rmse < best.rmse) best = r;
        }
    }
    return best;
}

}  // namespace fi
