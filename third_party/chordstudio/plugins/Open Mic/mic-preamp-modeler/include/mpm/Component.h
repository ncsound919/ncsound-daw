#pragma once

#include "mpm/Types.h"

namespace mpm {

/// Interface every swappable internal (capsule, tube stage, transformer, ...) implements.
/// Real-time rules: prepare() may allocate; reset() and process() must not.
class Component {
public:
    virtual ~Component() = default;

    virtual void prepare(const ProcessSpec& spec) = 0;
    virtual void reset() = 0;

    /// In-place, mono. Must accept any numSamples >= 0, including more than maxBlockSize.
    virtual void process(Sample* data, int numSamples) = 0;

    virtual PortInfo inputPort() const = 0;
    virtual PortInfo outputPort() const = 0;

    /// Whole-sample latency at the host rate introduced by this component.
    virtual int latencySamples() const { return 0; }

    /// Smallest oversampling factor the component needs to stay clean.
    virtual int minOversampling() const { return 1; }
};

}  // namespace mpm
