#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fi/nss.hpp"

using Catch::Approx;
using fi::fit_nss;
using fi::NSSParams;

namespace {
const std::vector<double> kTenors{0.25, 0.5, 1, 2, 3, 5, 7, 10, 20, 30};
}  // namespace

TEST_CASE("NSS model evaluates its factor loadings", "[nss]") {
    NSSParams p;
    p.beta0 = 0.05;
    p.beta1 = -0.02;
    p.beta2 = 0.0;
    p.beta3 = 0.0;
    p.lambda1 = 2.0;
    p.lambda2 = 5.0;

    // τ→0 limit: y → β0 + β1.
    REQUIRE(p.yield(1e-8) == Approx(0.03).margin(1e-6));
    REQUIRE(p.yield(0.0) == Approx(0.03));

    // Spot check the level term at a finite tenor.
    const double tau = 4.0, x = tau / p.lambda1, e = std::exp(-x);
    const double expected = 0.05 + (-0.02) * (1.0 - e) / x;
    REQUIRE(p.yield(tau) == Approx(expected));
}

TEST_CASE("Levenberg-Marquardt recovers synthetic NSS parameters", "[nss]") {
    const NSSParams truth{0.05, -0.02, 0.02, 0.015, 1.5, 8.0};

    std::vector<double> ys;
    for (double t : kTenors) ys.push_back(truth.yield(t));

    // Start from a perturbed (but ordered) guess.
    const NSSParams guess{0.052, -0.018, 0.018, 0.016, 1.7, 7.0};
    auto r = fit_nss(kTenors, ys, guess);

    REQUIRE(r.converged);
    REQUIRE(r.rmse < 1e-8);
    REQUIRE(r.params.beta0 == Approx(truth.beta0).margin(1e-4));
    REQUIRE(r.params.beta1 == Approx(truth.beta1).margin(1e-4));
    REQUIRE(r.params.beta2 == Approx(truth.beta2).margin(1e-4));
    REQUIRE(r.params.beta3 == Approx(truth.beta3).margin(1e-4));
    REQUIRE(r.params.lambda1 == Approx(truth.lambda1).margin(1e-4));
    REQUIRE(r.params.lambda2 == Approx(truth.lambda2).margin(1e-4));
}

TEST_CASE("Fit reproduces the input yields", "[nss]") {
    const NSSParams truth{0.04, -0.01, 0.025, 0.01, 2.0, 6.0};
    std::vector<double> ys;
    for (double t : kTenors) ys.push_back(truth.yield(t));

    auto r = fit_nss(kTenors, ys, truth);  // start at the truth
    for (std::size_t i = 0; i < kTenors.size(); ++i) {
        REQUIRE(r.params.yield(kTenors[i]) == Approx(ys[i]).margin(1e-8));
    }
}

TEST_CASE("Fit to real Treasury yields has small residuals", "[nss]") {
    std::ifstream file(std::string(FI_DATA_DIR) + "/treasury_yields.csv");
    REQUIRE(file.is_open());

    std::vector<double> taus, yields;
    std::string line;
    std::getline(file, line);  // header
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string tenor_s, yield_s;
        std::getline(ss, tenor_s, ',');
        std::getline(ss, yield_s, ',');
        taus.push_back(std::stod(tenor_s));
        yields.push_back(std::stod(yield_s) / 100.0);  // percent → decimal
    }
    REQUIRE(taus.size() == 10);

    auto r = fit_nss(taus, yields);  // multi-start

    // RMSE well under 5bp...
    REQUIRE(r.rmse < 5e-4);
    // ...and no single tenor mispriced by more than 10bp.
    double max_abs = 0.0;
    for (std::size_t i = 0; i < taus.size(); ++i) {
        max_abs = std::max(max_abs, std::abs(r.params.yield(taus[i]) - yields[i]));
    }
    REQUIRE(max_abs < 1e-3);
}
