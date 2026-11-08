#pragma once

#include <cmath>

#include "mpm/Types.h"

namespace mpm {

/// First-order antiderivative anti-aliasing for y = tanh(x).
/// Antiderivative: F1(x) = log(cosh(x)). Adds half a sample of delay at the rate it runs at.
class AdaaTanh {
public:
    void reset() noexcept {
        xPrev_ = 0.0;
        f1Prev_ = 0.0;
    }

    Sample process(Sample x) noexcept {
        const double f1 = logCosh(x);
        const double d = x - xPrev_;
        double y;
        if (std::abs(d) < 1e-6) {
            y = std::tanh(0.5 * (x + xPrev_));  // ill-conditioned limit
        } else {
            y = (f1 - f1Prev_) / d;
        }
        xPrev_ = x;
        f1Prev_ = f1;
        return y;
    }

    /// Numerically stable log(cosh(x)).
    static double logCosh(double x) noexcept {
        const double ax = std::abs(x);
        return ax + std::log1p(std::exp(-2.0 * ax)) - 0.6931471805599453;
    }

private:
    double xPrev_ = 0.0;
    double f1Prev_ = 0.0;
};

}  // namespace mpm
