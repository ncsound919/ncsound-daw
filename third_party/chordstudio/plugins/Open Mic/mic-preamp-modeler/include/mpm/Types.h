#pragma once

namespace mpm {

using Sample = double;

/// Passed to Component::prepare(). One Component instance processes one channel.
struct ProcessSpec {
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
};

struct Range {
    double min = 0.0;
    double max = 0.0;
};

/// Nominal impedance window of a component port, in ohms.
struct PortInfo {
    Range impedanceOhm;
};

}  // namespace mpm
