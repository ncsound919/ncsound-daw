#pragma once

#include <vector>

#include "mpm/Types.h"

namespace mpm {

/// Symmetric linear-phase halfband FIR (Kaiser window), odd taps plus the centre tap only.
class HalfbandFilter {
public:
    /// halfLength K gives 2K+1 taps. gain scales the output (2 for interpolation).
    void design(int halfLength, double stopbandDb, double gain);
    void reset();
    Sample process(Sample x) noexcept;
    int halfLength() const noexcept { return halfLength_; }

    /// Smallest K meeting the spec for a stage whose input rate is fsLow.
    static int requiredHalfLength(double fsLow, double stopbandDb, double passbandHz);

private:
    int halfLength_ = 0;
    int len_ = 0;
    int pos_ = 0;
    double gain_ = 1.0;
    std::vector<int> idx_;
    std::vector<double> coef_;
    std::vector<double> hist_;  // doubled ring buffer
};

/// Cascade of 2x linear-phase halfband stages: 1x, 2x, 4x, 8x or 16x.
/// Latency is an exact whole number of host-rate samples (stage lengths are
/// chosen so that each stage's delay is an integer at the host rate).
class Oversampler {
public:
    /// log2Factor in [0, 4]. Allocates; call from the message thread.
    void prepare(double sampleRate, int log2Factor, int maxBlockSize, double stopbandDb = 100.0);
    void reset();

    int factor() const noexcept { return 1 << log2Factor_; }
    int latencySamples() const noexcept { return latency_; }

    /// Returns a pointer to n * factor() samples owned by the oversampler. n <= maxBlockSize.
    Sample* upsample(const Sample* in, int n) noexcept;
    /// Writes n host-rate samples to out from the buffer last returned by upsample().
    void downsample(Sample* out, int n) noexcept;

private:
    int log2Factor_ = 0;
    int maxBlock_ = 0;
    int latency_ = 0;
    std::vector<HalfbandFilter> up_;
    std::vector<HalfbandFilter> down_;
    std::vector<std::vector<Sample>> bufs_;
    std::vector<Sample> pass_;
};

}  // namespace mpm
