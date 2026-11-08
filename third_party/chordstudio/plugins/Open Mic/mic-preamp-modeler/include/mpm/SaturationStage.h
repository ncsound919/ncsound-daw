#pragma once

#include "mpm/Adaa.h"
#include "mpm/Component.h"
#include "mpm/Oversampler.h"

namespace mpm {

/// Memoryless tanh saturation run inside an oversampler, optionally with ADAA.
/// First concrete Component: it proves the interface, the oversampling path and the
/// latency reporting. It is not a model of any real circuit.
class SaturationStage final : public Component {
public:
    struct Params {
        double driveDb = 0.0;
        bool adaa = true;
        int log2Oversampling = 2;   // 0..4
        double stopbandDb = 100.0;
    };

    SaturationStage() = default;
    explicit SaturationStage(const Params& p) : params_(p) {}

    void prepare(const ProcessSpec& spec) override;
    void reset() override;
    /// Must be preceded by prepare(). Called before prepare() it is a no-op that
    /// leaves the block untouched rather than looping forever.
    void process(Sample* data, int numSamples) override;

    PortInfo inputPort() const override { return {{100000.0, 1.0e9}}; }
    PortInfo outputPort() const override { return {{1.0, 100.0}}; }
    int latencySamples() const override { return os_.latencySamples(); }
    int minOversampling() const override { return 1 << params_.log2Oversampling; }

private:
    Params params_;
    Oversampler os_;
    AdaaTanh adaa_;
    double gain_ = 1.0;
    int maxBlock_ = 0;
};

}  // namespace mpm
