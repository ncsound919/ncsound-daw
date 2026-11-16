// Tempo, position, loop and markers. Split out of AudioEngine (plan 07-05);
// bodies moved verbatim, with the engine's sample rate passed in.

#include "TransportClock.h"

#include <juce_core/juce_core.h>
#include <nc/engine/MidiClip.hpp>
#include <nc/engine/ProjectLimits.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace nc
{
TransportClock::TransportClock()
{
    tempoSlot.owned = std::make_unique<nc::engine::TempoMap> (nc::engine::constantTempoMap (120.0));
    tempoSlot.live.store (tempoSlot.owned.get(), std::memory_order_release);
}

void TransportClock::setBpm (double newBpm)
{
    const auto clamped = nc::limits::clampBpm (newBpm);
    bpmValue.store (clamped);
    setTempoMap (nc::engine::constantTempoMap (clamped));
}

void TransportClock::setTempoMap (nc::engine::TempoMap map)
{
    nc::engine::sanitizeTempoMap (map);
    auto fresh = std::make_unique<nc::engine::TempoMap> (std::move (map));

    retireSnapshot (tempoRetired, tempoSlot.owned, blocks.load (std::memory_order_acquire));

    tempoSlot.owned = std::move (fresh);
    tempoSlot.live.store (tempoSlot.owned.get(), std::memory_order_release);
    bpmValue.store (tempoSlot.owned->points.front().bpm);
}

nc::engine::TempoMap TransportClock::tempoMap() const
{
    if (const auto* live = tempoSlot.live.load (std::memory_order_acquire))
        return *live;

    return fallback;
}

void TransportClock::setMarkers (std::vector<nc::engine::Marker> markers)
{
    // Project files can contain any JSON; the engine is the last boundary that
    // can reject non-finite PPQ, oversized names and absurd counts.
    nc::engine::sanitizeMarkers (markers);
    markers_ = std::move (markers);
}

double TransportClock::positionSeconds (double sampleRate) const
{
    const auto rate = sampleRate;
    return rate > 0.0 ? static_cast<double> (position.load()) / rate : 0.0;
}

void TransportClock::resetPosition()
{
    // The callback owns the click state; never write it from the message thread.
    resetRequest.store (true);
}

void TransportClock::setPositionSeconds (double seconds, double sampleRate)
{
    const auto rate = juce::jmax (1.0, sampleRate);
    const auto samples = static_cast<int64_t> (juce::jmax (0.0, seconds) * rate);
    position.store (samples);
    seekRequest.store (true);
}

void TransportClock::setLoopBars (double startBar, double endBar)
{
    const auto start = juce::jlimit (1.0, static_cast<double> (nc::limits::maxBars), startBar);
    const auto end = juce::jlimit (start + 1.0, static_cast<double> (nc::limits::maxBars + 1), endBar);
    loopStart.store (start);
    loopEnd.store (end);
}

void TransportClock::loopPoints (const nc::engine::TempoMap& map, double rate,
                                 int64_t& start, int64_t& end) const
{
    start = static_cast<int64_t> (std::llround (
        engine::ppqToSeconds (map, engine::clipStartBeat (loopStart.load())) * rate));
    end = static_cast<int64_t> (std::llround (
        engine::ppqToSeconds (map, engine::clipStartBeat (loopEnd.load())) * rate));
}

nc::engine::TransportBlock TransportClock::block (double sampleRate, int blockSize) const
{
    const auto rate = juce::jmax (1.0, sampleRate);
    const auto map = tempoMap();
    const auto ppq = engine::ppqAtSamplesWithMap (map, position.load(), rate);
    const auto tempo = juce::jmax (1.0, engine::bpmAt (map, ppq));

    int64_t loopStartSamples = 0;
    int64_t loopEndSamples = 0;
    loopPoints (map, rate, loopStartSamples, loopEndSamples);

    return engine::blockContext (position.load(), blockSize, tempo, rate,
                                 playing.load(), loopOn.load(), loopStartSamples, loopEndSamples);
}

void TransportClock::reclaimTempo (uint64_t blockNow, bool callbackRunning)
{
    tempoRetired.erase (std::remove_if (tempoRetired.begin(), tempoRetired.end(),
                                        [callbackRunning, blockNow] (const auto& entry)
                                        {
                                            return ! callbackRunning
                                                   || entry.retiredAtBlock + 2 <= blockNow;
                                        }),
                        tempoRetired.end());
}
} // namespace nc
