#include "mpm/Oversampler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

namespace mpm {

namespace {

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double q = 0.25 * x * x;
    for (int k = 1; k < 200; ++k) {
        term *= q / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < 1e-18 * sum) break;
    }
    return sum;
}

double kaiserBeta(double a) {
    if (a > 50.0) return 0.1102 * (a - 8.7);
    if (a >= 21.0) return 0.5842 * std::pow(a - 21.0, 0.4) + 0.07886 * (a - 21.0);
    return 0.0;
}

}  // namespace

int HalfbandFilter::requiredHalfLength(double fsLow, double stopbandDb, double passbandHz) {
    const double fp = std::min(passbandHz, 0.4535 * fsLow);
    const double fHigh = 2.0 * fsLow;
    const double dfNorm = (fsLow - 2.0 * fp) / fHigh;  // transition width / high rate
    const double n =
        (stopbandDb - 7.95) / (2.285 * 2.0 * std::numbers::pi * dfNorm) + 1.0;
    const int k = static_cast<int>(std::ceil((n - 1.0) / 2.0));
    return std::max(k, 3);
}

void HalfbandFilter::design(int halfLength, double stopbandDb, double gain) {
    assert(halfLength >= 1);
    halfLength_ = halfLength;
    len_ = 2 * halfLength + 1;
    gain_ = gain;
    const double beta = kaiserBeta(stopbandDb);
    const double i0Beta = besselI0(beta);

    std::vector<double> h(static_cast<size_t>(len_), 0.0);
    double sum = 0.0;
    for (int k = -halfLength; k <= halfLength; ++k) {
        double v;
        if (k == 0) {
            v = 0.5;
        } else if (k % 2 == 0) {
            v = 0.0;
        } else {
            v = std::sin(std::numbers::pi * k / 2.0) / (std::numbers::pi * k);
        }
        const double r = static_cast<double>(k) / static_cast<double>(halfLength);
        const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
        v *= w;
        h[static_cast<size_t>(k + halfLength)] = v;
        sum += v;
    }
    idx_.clear();
    coef_.clear();
    for (int j = 0; j < len_; ++j) {
        const double v = h[static_cast<size_t>(j)] / sum;  // unity DC gain
        if (v != 0.0) {
            idx_.push_back(j);
            coef_.push_back(v);
        }
    }
    hist_.assign(static_cast<size_t>(2 * len_), 0.0);
    pos_ = 0;
}

void HalfbandFilter::reset() {
    std::fill(hist_.begin(), hist_.end(), 0.0);
    pos_ = 0;
}

Sample HalfbandFilter::process(Sample x) noexcept {
    pos_ = (pos_ + 1 == len_) ? 0 : pos_ + 1;
    hist_[static_cast<size_t>(pos_)] = x;
    hist_[static_cast<size_t>(pos_ + len_)] = x;
    const double* base = hist_.data() + pos_ + len_;
    double acc = 0.0;
    for (size_t i = 0; i < idx_.size(); ++i) acc += coef_[i] * base[-idx_[i]];
    return acc * gain_;
}

void Oversampler::prepare(double sampleRate, int log2Factor, int maxBlockSize,
                          double stopbandDb) {
    assert(log2Factor >= 0 && log2Factor <= 4);
    log2Factor_ = std::clamp(log2Factor, 0, 4);
    maxBlock_ = maxBlockSize;
    up_.assign(static_cast<size_t>(log2Factor_), HalfbandFilter{});
    down_.assign(static_cast<size_t>(log2Factor_), HalfbandFilter{});
    bufs_.clear();
    latency_ = 0;

    for (int s = 0; s < log2Factor_; ++s) {
        const double fsLow = sampleRate * static_cast<double>(1 << s);
        int k = HalfbandFilter::requiredHalfLength(fsLow, stopbandDb, 20000.0);
        // Stage delay at the host rate is K / 2^s samples; make it an integer.
        const int m = 1 << s;
        k = ((k + m - 1) / m) * m;
        up_[static_cast<size_t>(s)].design(k, stopbandDb, 2.0);
        down_[static_cast<size_t>(s)].design(k, stopbandDb, 1.0);
        latency_ += k / m;
        bufs_.emplace_back(static_cast<size_t>(maxBlockSize) * static_cast<size_t>(2 << s), 0.0);
    }
    pass_.assign(static_cast<size_t>(maxBlockSize), 0.0);
}

void Oversampler::reset() {
    for (auto& f : up_) f.reset();
    for (auto& f : down_) f.reset();
    for (auto& b : bufs_) std::fill(b.begin(), b.end(), 0.0);
    std::fill(pass_.begin(), pass_.end(), 0.0);
}

Sample* Oversampler::upsample(const Sample* in, int n) noexcept {
    assert(n >= 0 && n <= maxBlock_);
    if (log2Factor_ == 0) {
        std::copy(in, in + n, pass_.begin());
        return pass_.data();
    }
    for (int s = 0; s < log2Factor_; ++s) {
        const Sample* src = (s == 0) ? in : bufs_[static_cast<size_t>(s - 1)].data();
        Sample* dst = bufs_[static_cast<size_t>(s)].data();
        const int nIn = n << s;
        HalfbandFilter& f = up_[static_cast<size_t>(s)];
        for (int i = 0; i < nIn; ++i) {
            dst[2 * i] = f.process(src[i]);
            dst[2 * i + 1] = f.process(0.0);
        }
    }
    return bufs_[static_cast<size_t>(log2Factor_ - 1)].data();
}

void Oversampler::downsample(Sample* out, int n) noexcept {
    assert(n >= 0 && n <= maxBlock_);
    if (log2Factor_ == 0) {
        std::copy(pass_.begin(), pass_.begin() + n, out);
        return;
    }
    for (int s = log2Factor_ - 1; s >= 0; --s) {
        const Sample* src = bufs_[static_cast<size_t>(s)].data();
        Sample* dst = (s == 0) ? out : bufs_[static_cast<size_t>(s - 1)].data();
        const int nOut = n << s;
        HalfbandFilter& f = down_[static_cast<size_t>(s)];
        for (int i = 0; i < nOut; ++i) {
            dst[i] = f.process(src[2 * i]);
            f.process(src[2 * i + 1]);
        }
    }
}

}  // namespace mpm
