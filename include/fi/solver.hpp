#pragma once

#include <algorithm>
#include <cmath>

namespace fi {

// Convergence controls for the root finders. Nothing is hardcoded at the call
// site — pass a configured instance to override the defaults.
struct SolverConfig {
    double value_tolerance = 1e-10;  // converged when |f(x)| <= this
    double step_tolerance = 1e-14;   // ...or when |Δx| <= this
    int max_iterations = 50;
};

struct SolverResult {
    double root = 0.0;
    int iterations = 0;
    bool converged = false;
};

// Plain Newton-Raphson from an initial guess. Used as the fallback when a root
// is not bracketed. Converges on residual or step size.
template <class F, class DF>
SolverResult newton_pure(F&& f, DF&& df, double guess, const SolverConfig& cfg = {}) {
    double x = guess;
    for (int it = 1; it <= cfg.max_iterations; ++it) {
        const double fx = f(x);
        if (std::abs(fx) <= cfg.value_tolerance) return {x, it, true};
        const double dfx = df(x);
        if (dfx == 0.0) break;  // flat derivative — give up to the caller
        const double step = fx / dfx;
        x -= step;
        if (std::abs(step) <= cfg.step_tolerance) return {x, it, true};
    }
    return {x, cfg.max_iterations, false};
}

// Safeguarded Newton-Raphson (the "rtsafe" scheme): take a Newton step when it
// stays inside the current bracket and makes good progress, otherwise bisect.
// Requires f(x_low) and f(x_high) to straddle zero; if they don't, falls back
// to plain Newton from `guess`.
template <class F, class DF>
SolverResult newton_bisection(F&& f, DF&& df, double guess, double x_low,
                              double x_high, const SolverConfig& cfg = {}) {
    double fl = f(x_low);
    double fh = f(x_high);
    if (fl == 0.0) return {x_low, 0, true};
    if (fh == 0.0) return {x_high, 0, true};
    if ((fl > 0.0) == (fh > 0.0)) {
        // Root not bracketed: best effort with plain Newton.
        return newton_pure(std::forward<F>(f), std::forward<DF>(df), guess, cfg);
    }

    // Orient the bracket so that f(xl) < 0 < f(xh).
    double xl, xh;
    if (fl < 0.0) { xl = x_low; xh = x_high; }
    else          { xl = x_high; xh = x_low; }

    const double bracket_lo = std::min(x_low, x_high);
    const double bracket_hi = std::max(x_low, x_high);
    double rts = (guess > bracket_lo && guess < bracket_hi)
                     ? guess
                     : 0.5 * (x_low + x_high);

    double dx_old = std::abs(x_high - x_low);
    double dx = dx_old;
    double fval = f(rts);
    double dfval = df(rts);

    for (int it = 1; it <= cfg.max_iterations; ++it) {
        const bool out_of_range =
            ((rts - xh) * dfval - fval) * ((rts - xl) * dfval - fval) > 0.0;
        const bool too_slow = std::abs(2.0 * fval) > std::abs(dx_old * dfval);

        if (dfval == 0.0 || out_of_range || too_slow) {
            dx_old = dx;
            dx = 0.5 * (xh - xl);
            rts = xl + dx;
        } else {
            dx_old = dx;
            dx = fval / dfval;
            rts -= dx;
        }

        fval = f(rts);
        dfval = df(rts);
        if (std::abs(fval) <= cfg.value_tolerance ||
            std::abs(dx) <= cfg.step_tolerance) {
            return {rts, it, true};
        }
        if (fval < 0.0) xl = rts; else xh = rts;
    }
    return {rts, cfg.max_iterations, false};
}

}  // namespace fi
