#pragma once

#include <cstdint>
#include <vector>

namespace nc
{
/** A half-open range of samples inside a loaded audio file. */
struct Slice
{
    std::int64_t start = 0;
    std::int64_t end = 0;
};

/** Splits a sample into at most maxSlices ranges.

    Boundaries sit on an even grid unless a detected transient is close enough,
    which keeps chops musical when the source has clear hits. Never returns a
    slice shorter than 32 samples, and returns empty for a zero-length sample.
*/
std::vector<Slice> planSlices (std::int64_t lengthSamples,
                               const std::vector<std::int64_t>& transients,
                               int maxSlices);

/** Spectral-flux onset detection over planar channel data.

    A half-wave-rectified log-magnitude flux over log-spaced bands is
    peak-picked against a local median/MAD threshold. A slow per-band baseline
    absorbs sustained pads and tones. sensitivity is 0..1; higher values lower
    the threshold and find more onsets. Positions are sample indices into the
    source.
*/
std::vector<std::int64_t> detectTransients (const float* const* channelData,
                                           int numChannels,
                                           std::int64_t numFrames,
                                           double sensitivity);
} // namespace nc
