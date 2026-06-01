#pragma once

#include <vector>

#include "fi/solver.hpp"

namespace fi {

// Nelson-Siegel-Svensson parametric yield curve:
//
//   y(τ) = β0
//        + β1 · [(1−e^{−τ/λ1}) / (τ/λ1)]
//        + β2 · [(1−e^{−τ/λ1}) / (τ/λ1) − e^{−τ/λ1}]
//        + β3 · [(1−e^{−τ/λ2}) / (τ/λ2) − e^{−τ/λ2}]
//
// β0 is the long-run level, β0+β1 the short rate, β2/β3 the two humps, and
// λ1/λ2 their decay time scales.
struct NSSParams {
    double beta0 = 0.0;
    double beta1 = 0.0;
    double beta2 = 0.0;
    double beta3 = 0.0;
    double lambda1 = 1.0;
    double lambda2 = 1.0;

    // Model yield at maturity τ (years). τ must be > 0.
    double yield(double tau) const;
};

struct NSSFitResult {
    NSSParams params;
    double rmse = 0.0;  // root-mean-square residual, in yield units
    int iterations = 0;
    bool converged = false;
};

// Rule-of-thumb starting point: β0 ≈ longest-tenor yield, β1 ≈ short − long,
// β2 = β3 = 0, λ1 = 2, λ2 = 5. (taus/yields need not be sorted.)
NSSParams nss_initial_guess(const std::vector<double>& taus,
                            const std::vector<double>& yields);

// Fit the six NSS parameters to (τ_i, y_i) by Levenberg-Marquardt, minimizing
// Σ (y(τ_i) − y_i)². Eigen solves the damped normal equations each step.
NSSFitResult fit_nss(const std::vector<double>& taus,
                     const std::vector<double>& yields,
                     const NSSParams& initial_guess, const SolverConfig& cfg);

// Convenience overloads: an NSS-tuned config (200 iters, 1e-12 tol) and/or the
// rule-of-thumb initial guess.
inline SolverConfig nss_default_config() {
    SolverConfig c;
    c.value_tolerance = 1e-12;
    c.step_tolerance = 1e-14;
    c.max_iterations = 200;
    return c;
}
inline NSSFitResult fit_nss(const std::vector<double>& taus,
                            const std::vector<double>& yields,
                            const NSSParams& initial_guess) {
    return fit_nss(taus, yields, initial_guess, nss_default_config());
}
// No-guess fit: a multi-start sweep over a grid of decay scales (λ1, λ2),
// keeping the lowest-RMSE result. NSS is non-convex in the λ's, so a single
// rule-of-thumb start can stall in a poor local minimum; the sweep finds a
// near-global fit.
NSSFitResult fit_nss(const std::vector<double>& taus,
                     const std::vector<double>& yields);

}  // namespace fi
