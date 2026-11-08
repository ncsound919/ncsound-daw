#include "mpm/SaturationStage.h"

#include <algorithm>
#include <cmath>

namespace mpm {

void SaturationStage::prepare(const ProcessSpec& spec) {
    maxBlock_ = spec.maxBlockSize;
    gain_ = std::pow(10.0, params_.driveDb / 20.0);
    os_.prepare(spec.sampleRate, params_.log2Oversampling, spec.maxBlockSize,
                params_.stopbandDb);
    adaa_.reset();
}

void SaturationStage::reset() {
    os_.reset();
    adaa_.reset();
}

void SaturationStage::process(Sample* data, int numSamples) {
    // prepare() has not run yet, so there is no oversampler state to process
    // through and maxBlock_ is still 0. Chunking with a 0 limit makes `done`
    // never advance, which is an unbounded loop on the audio thread rather than
    // a wrong answer. Returning leaves the block untouched and lets the caller
    // hear the mistake as missing output instead of a hung host.
    if (maxBlock_ <= 0)
        return;

    int done = 0;
    while (done < numSamples) {
        const int chunk = std::min(numSamples - done, maxBlock_);
        Sample* up = os_.upsample(data + done, chunk);
        const int m = chunk * os_.factor();
        if (params_.adaa) {
            for (int i = 0; i < m; ++i) up[i] = adaa_.process(gain_ * up[i]);
        } else {
            for (int i = 0; i < m; ++i) up[i] = std::tanh(gain_ * up[i]);
        }
        os_.downsample(data + done, chunk);
        done += chunk;
    }
}

}  // namespace mpm
