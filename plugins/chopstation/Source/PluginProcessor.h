#pragma once

#include <nc/engine/SlicePlanner.hpp>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace nc
{
/** Immutable sample plus the chop map derived from it. Voices hold a shared
    reference, so replacing the sample on the message thread never pulls memory
    out from under the audio thread.
*/
struct SampleData
{
    juce::AudioBuffer<float> buffer;
    double sourceRate = 44100.0;
    std::vector<Slice> slices;
};

using SamplePtr = std::shared_ptr<const SampleData>;

struct SampleHolder
{
    std::atomic<SamplePtr> current;
};

class ChopVoice final : public juce::SynthesiserVoice
{
public:
    explicit ChopVoice (SampleHolder& holderToUse) : holder (holderToUse) {}

    bool canPlaySound (juce::SynthesiserSound*) override { return true; }
    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote (float, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;

private:
    SampleHolder& holder;
    SamplePtr activeSample;
    std::int64_t position = 0;
    std::int64_t sliceEnd = 0;
    double increment = 1.0;
    float velocityGain = 0.0f;
};

class ChopStationProcessor final : public juce::AudioProcessor
{
public:
    ChopStationProcessor();
    ~ChopStationProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Chop Station"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Loads an audio file, detects onsets and rebuilds the chop map.

        This is the synchronous entry point used by tests and state restoration.
        The editor uses loadSampleAsync so large files do not block the message
        thread.
    */
    bool loadSample (const juce::File& file);

    /** Starts decoding and onset analysis on a worker thread. Returns false if
        another load is already running. The existing sample remains playable
        until the new one is committed.
    */
    bool loadSampleAsync (const juce::File& file);
    bool isLoadingSample() const noexcept { return loadingSample.load (std::memory_order_acquire); }
    bool didLastLoadFail() const noexcept { return lastLoadFailed.load (std::memory_order_acquire); }

    juce::String getSampleName() const;
    int getSliceCount() const;
    juce::String getSamplePath() const;

    static constexpr int64_t maxSampleFrames = 3 * 44100 * 60; // 3 minutes-ish ceiling
    static constexpr int maxSampleChannels = 2;

    // Factory presets (index → name). Apply via APVTS so automation/host sees them.
    static int numPresets() { return 3; }
    static juce::String presetName (int index);
    void applyPreset (int index);

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void rebuildSlices();
    SamplePtr decodeSample (const juce::File& file, int chopCount);
    void commitSample (SamplePtr data, const juce::File& file);
    void cancelPendingLoad() noexcept;
    int currentChopCount() const;

    SampleHolder holder;
    juce::Synthesiser synth;
    juce::AudioFormatManager formats;

    double currentSampleRate = 44100.0;
    mutable juce::CriticalSection metadataLock;
    juce::CriticalSection formatLock;
    juce::CriticalSection loadStateLock;
    juce::String sampleName;
    juce::String activePath;
    std::atomic<bool> loadingSample { false };
    std::atomic<bool> lastLoadFailed { false };
    std::atomic<std::uint64_t> loadGeneration { 0 };

    // Simple state-variable filter, one per channel.
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

        void set (float cutoffHz, float q, float sampleRate) noexcept
        {
            const auto g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, sampleRate * 0.45f, cutoffHz) / sampleRate);
            const auto k = 1.0f / juce::jmax (0.1f, q);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        float process (float v0) noexcept
        {
            const auto v3 = v0 - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v2;
        }
    };

    std::vector<Svf> filters;
    juce::AudioBuffer<float> delayBuffer;
    int delayWrite = 0;
    juce::SmoothedValue<float> gainSmoother;
    juce::Reverb reverb;

    // Declared last so its destructor waits for an in-flight decode before the
    // members it uses are destroyed.
    juce::ThreadPool loadPool { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopStationProcessor)
};
} // namespace nc
