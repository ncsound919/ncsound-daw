#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace nc
{
/** Stereo aux return bus. The engine sums post-fader track sends into a
    scratch buffer, calls process() once per block, then adds the wet return
    into the master mix. Tail state (reverb memory, delay feedback) lives here
    across blocks so the tail rings across blocks without allocation.

    Two stock engines ship for each bus and either can be swapped in at any
    time: the Overlay Reverb / Overlay Delay pair, or their swap targets Nexus
    Reverb and Temporal Delay. The swapped engine is chosen on the message
    thread and the buffers are re-fitted there.

    Threading: prepare/reset/setEngine run on the message thread, process on the
    audio thread. A swapped engine is built in a fresh Impl and published by an
    atomic pointer, so process() never observes a half-rebuilt delay line; the
    replaced Impl is retired and freed by collectRetired() once the audio thread
    has started later blocks. No allocation in process(). */
class AuxBus
{
public:
    enum class Engine
    {
        overlayReverb = 0,
        nexusReverb = 1,
        overlayDelay = 2,
        temporalDelay = 3
    };

    static constexpr int numEngines = 4;

    static const char* engineName (Engine engine) noexcept;
    static bool isReverb (Engine engine) noexcept { return static_cast<int> (engine) < 2; }

    AuxBus();
    ~AuxBus();

    AuxBus (const AuxBus&) = delete;
    AuxBus& operator= (const AuxBus&) = delete;

    void prepare (double sampleRate, int maxBlockSize, Engine engine);

    /** Swaps the engine. Message thread; builds the replacement off to the
        side and publishes it. `currentBlock` is the audio block counter at the
        swap, used to delay freeing the old implementation. */
    void setEngine (Engine newEngine, std::uint64_t currentBlock);

    /** Frees implementations retired at least two blocks ago. Message thread. */
    void collectRetired (std::uint64_t currentBlock);

    Engine currentEngine() const noexcept { return engine; }
    juce::String currentEngineName() const { return engineName (engine); }

    void reset();

    /** In-place stereo block. numSamples <= prepared maxBlock. */
    void process (juce::AudioBuffer<float>& buffer, int numSamples);

private:
    struct Impl;

    struct Retired
    {
        std::unique_ptr<Impl> impl;
        std::uint64_t retiredAtBlock = 0;
    };

    std::unique_ptr<Impl> impl;             // message-thread working copy
    std::atomic<Impl*> live { nullptr };    // what process() reads
    std::vector<Retired> retired;           // freed by collectRetired()
    Engine engine = Engine::overlayReverb;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool prepared = false;
};
} // namespace nc
