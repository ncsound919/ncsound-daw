#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <vector>

namespace nc
{
/** Stereo aux return bus (reverb or delay). The engine sums post-fader track
    sends into a scratch buffer, calls process() once per block, then adds the
    wet return into the master mix. Tail state (reverb memory, delay feedback)
    lives here across blocks so the tail rings across blocks without allocation.

    Threading: prepare/reset/process run on the audio thread (process) and the
    message thread (prepare/reset) with the usual "no concurrent calls" rule of
    this codebase. No allocation in process(). */
class AuxBus
{
public:
    enum class Kind { reverb, delay };

    AuxBus();
    ~AuxBus();

    AuxBus (const AuxBus&) = delete;
    AuxBus& operator= (const AuxBus&) = delete;

    void prepare (double sampleRate, int maxBlockSize, Kind kind);
    void reset();

    /** In-place stereo block. numSamples <= prepared maxBlock. */
    void process (juce::AudioBuffer<float>& buffer, int numSamples);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Kind kind = Kind::reverb;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool prepared = false;
};
} // namespace nc
