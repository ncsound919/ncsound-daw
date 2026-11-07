#pragma once

#include <nc/engine/ClipTimeline.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nc::engine
{
/** Transport-to-plugin sync math, kept pure so it can be tested without a
    device or a plugin instance.

    VST3 hands every plugin a ProcessContext once per process call: project
    time in samples (which wraps at the loop), continuous time (which does
    not), quarter-note position, bar start, tempo, cycle range and the
    distance to the next 24-PPQN clock. The values describe the start of the
    block only, so a host must derive them from the playhead every block
    rather than accumulate them. See Steinberg's ProcessContext reference.
*/
inline constexpr double beatsPerBar = 4.0;
inline constexpr double clockTicksPerQuarter = 24.0;

struct TransportBlock
{
    int64_t projectTimeSamples = 0;     // wraps at the loop
    int64_t continuousTimeSamples = 0;  // monotonic
    double projectTimeMusic = 0.0;      // quarter notes, 1.0 == one quarter
    double barPositionMusic = 0.0;      // quarter notes to the current bar start
    double tempo = 120.0;
    double cycleStartMusic = 0.0;
    double cycleEndMusic = 0.0;
    int samplesToNextClock = 0;
    int numSamples = 0;
    bool isPlaying = false;
    bool isLooping = false;
};

/** A block that crosses the loop end is split at the boundary so the plugin
    never sees one block spanning a jump. secondSegment == 0 means no split. */
struct LoopSplit
{
    int firstSegment = 0;
    int secondSegment = 0;
};

inline double samplesPerBar (double bpm, double sampleRate) noexcept
{
    return secondsPerBar (bpm) * std::max (0.0, sampleRate);
}

inline double ppqAt (int64_t samples, double bpm, double sampleRate) noexcept
{
    if (bpm <= 0.0 || sampleRate <= 0.0)
        return 0.0;

    const auto seconds = static_cast<double> (std::max<int64_t> (0, samples)) / sampleRate;
    return seconds * (bpm / 60.0);
}

inline int64_t samplesAtPpq (double ppq, double bpm, double sampleRate) noexcept
{
    if (bpm <= 0.0 || sampleRate <= 0.0)
        return 0;

    return static_cast<int64_t> (std::llround (ppq * (60.0 / bpm) * sampleRate));
}

/** Samples until the next 24-PPQN tick; 0 when the block starts on a tick. */
inline int samplesToNextClock (int64_t samples, double bpm, double sampleRate) noexcept
{
    if (bpm <= 0.0 || sampleRate <= 0.0)
        return 0;

    const auto tick = (60.0 / bpm) * sampleRate / clockTicksPerQuarter;

    if (tick < 1.0)
        return 0;

    const auto tickSamples = static_cast<int64_t> (std::llround (tick));
    const auto remainder = std::max<int64_t> (0, samples) % tickSamples;
    return remainder == 0 ? 0 : static_cast<int> (tickSamples - remainder);
}

/** Project time wraps inside the loop; before the loop start it does not. */
inline int64_t projectSamplesAt (int64_t continuousTimeSamples,
                                 int64_t loopStartSamples,
                                 int64_t loopEndSamples,
                                 bool looping) noexcept
{
    const auto position = std::max<int64_t> (0, continuousTimeSamples);

    if (! looping || loopEndSamples <= loopStartSamples || position < loopStartSamples)
        return position;

    return loopStartSamples + (position - loopStartSamples) % (loopEndSamples - loopStartSamples);
}

inline LoopSplit splitAtLoop (int64_t continuousTimeSamples, int blockSize,
                              int64_t loopStartSamples, int64_t loopEndSamples,
                              bool looping) noexcept
{
    const auto size = std::max (0, blockSize);
    LoopSplit split { size, 0 };

    if (! looping || loopEndSamples <= loopStartSamples || size == 0)
        return split;

    const auto projected = projectSamplesAt (continuousTimeSamples, loopStartSamples, loopEndSamples, looping);

    if (projected < loopStartSamples)
        return split;

    const auto untilLoopEnd = loopEndSamples - projected;

    if (untilLoopEnd > 0 && untilLoopEnd < size)
        split = { static_cast<int> (untilLoopEnd), size - static_cast<int> (untilLoopEnd) };

    return split;
}

inline TransportBlock blockContext (int64_t continuousTimeSamples, int blockSize,
                                    double bpm, double sampleRate, bool playing,
                                    bool looping, int64_t loopStartSamples,
                                    int64_t loopEndSamples) noexcept
{
    TransportBlock block;
    block.numSamples = std::max (0, blockSize);
    block.continuousTimeSamples = std::max<int64_t> (0, continuousTimeSamples);
    block.tempo = bpm > 0.0 ? bpm : 0.0;
    block.isPlaying = playing;
    block.isLooping = looping && loopEndSamples > loopStartSamples;

    block.projectTimeSamples = projectSamplesAt (block.continuousTimeSamples,
                                                 loopStartSamples, loopEndSamples, block.isLooping);
    block.projectTimeMusic = ppqAt (block.projectTimeSamples, bpm, sampleRate);

    const auto bar = beatsPerBar > 0.0 ? std::floor (block.projectTimeMusic / beatsPerBar) : 0.0;
    block.barPositionMusic = bar * beatsPerBar;
    block.samplesToNextClock = samplesToNextClock (block.projectTimeSamples, bpm, sampleRate);

    if (block.isLooping)
    {
        block.cycleStartMusic = ppqAt (std::max<int64_t> (0, loopStartSamples), bpm, sampleRate);
        block.cycleEndMusic = ppqAt (loopEndSamples, bpm, sampleRate);
    }

    return block;
}

/** Bar, beat and tick, all 1-based except the tick. */
struct BarBeat
{
    int bar = 1;
    int beat = 1;
    int tick = 0;
};

inline constexpr int ticksPerBeat = 960;

inline BarBeat barBeatAt (double ppq) noexcept
{
    if (! (ppq > 0.0))
        return {};

    const auto barIndex = static_cast<int> (std::floor (ppq / beatsPerBar));
    const auto withinBar = ppq - static_cast<double> (barIndex) * beatsPerBar;
    const auto beatIndex = static_cast<int> (std::floor (withinBar));
    const auto withinBeat = withinBar - static_cast<double> (beatIndex);
    const auto tickIndex = static_cast<int> (std::floor (withinBeat * ticksPerBeat + 1.0e-6));

    return { barIndex + 1, beatIndex + 1, std::min (ticksPerBeat - 1, tickIndex) };
}
} // namespace nc::engine
