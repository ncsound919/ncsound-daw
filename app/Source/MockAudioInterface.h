#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace nc::mock
{

/**
 * DeepMockAudioIODevice
 *
 * A high-fidelity mock implementation of juce::AudioIODevice simulating a
 * physical multi-channel studio audio interface (e.g., Focusrite Scarlett 18i8,
 * UA Apollo x4, RME Babyface Pro).
 *
 * Capabilities:
 *  - 4 physical input channels / 4 physical output channels.
 *  - Standard sample rates (44.1k, 48k, 88.2k, 96k, 192k) and buffer sizes (64..2048).
 *  - Signal injection per input channel:
 *      * Calibrated sine wave (default: 1 kHz @ -6 dBFS / 0.501187f on Input 0).
 *      * Harmonic chords / multi-tone (e.g. 440 Hz on Input 1, 250 Hz on Input 2).
 *      * Custom signal generator lambdas.
 *  - Deterministic manual block driving (driveBlock / driveBlocks / driveSeconds).
 *  - Concurrent real-time background streaming thread simulating hardware ASIO clocks.
 *  - Precise cycle counting, timestamp context, and xrun tracking.
 */
class DeepMockAudioIODevice final : public juce::AudioIODevice,
                                    private juce::Thread
{
public:
    static constexpr int kMaxChannels = 4;
    static constexpr double kDefaultSampleRate = 48000.0;
    static constexpr int kDefaultBufferSize = 512;

    enum class SignalType
    {
        CalibratedSine,  // Pure tone with configurable frequency and amplitude
        Silence,         // All zeros
        StepImpulse,     // Impulse at frame 0
        Custom           // Handled by customGenerator
    };

    struct ChannelConfig
    {
        SignalType type = SignalType::CalibratedSine;
        double frequencyHz = 1000.0;
        float amplitude = 0.5011872f; // -6 dBFS
        double phase = 0.0;
    };

    DeepMockAudioIODevice (const juce::String& deviceName = "DeepMock ASIO Audio Interface")
        : juce::AudioIODevice (deviceName, "DeepMock ASIO"),
          juce::Thread ("DeepMock Audio Clock")
    {
        // Channel 0: 1000 Hz @ -6 dBFS
        channelConfigs[0].type = SignalType::CalibratedSine;
        channelConfigs[0].frequencyHz = 1000.0;
        channelConfigs[0].amplitude = 0.5011872f;

        // Channel 1: 440 Hz @ -12 dBFS (0.251189f)
        channelConfigs[1].type = SignalType::CalibratedSine;
        channelConfigs[1].frequencyHz = 440.0;
        channelConfigs[1].amplitude = 0.2511886f;

        // Channel 2: 250 Hz @ -6 dBFS
        channelConfigs[2].type = SignalType::CalibratedSine;
        channelConfigs[2].frequencyHz = 250.0;
        channelConfigs[2].amplitude = 0.5011872f;

        // Channel 3: Silence
        channelConfigs[3].type = SignalType::Silence;
        channelConfigs[3].amplitude = 0.0f;

        activeInputs.setRange (0, kMaxChannels, true);
        activeOutputs.setRange (0, kMaxChannels, true);
    }

    ~DeepMockAudioIODevice() override
    {
        stopStreamingThread();
        close();
    }

    //==============================================================================
    // juce::AudioIODevice Interface Implementation
    //==============================================================================

    juce::StringArray getOutputChannelNames() override
    {
        return { "Out 1 (Main L)", "Out 2 (Main R)", "Out 3 (Cue L)", "Out 4 (Cue R)" };
    }

    juce::StringArray getInputChannelNames() override
    {
        return { "In 1 (Mic/Line 1)", "In 2 (Instrument 2)", "In 3 (Line 3)", "In 4 (Line 4)" };
    }

    juce::Array<double> getAvailableSampleRates() override
    {
        return { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    }

    juce::Array<int> getAvailableBufferSizes() override
    {
        return { 64, 128, 256, 512, 1024, 2048 };
    }

    int getDefaultBufferSize() override { return kDefaultBufferSize; }

    juce::String open (const juce::BigInteger& inputChannels,
                       const juce::BigInteger& outputChannels,
                       double sampleRateToUse,
                       int bufferSizeSamples) override
    {
        activeInputs = inputChannels;
        activeOutputs = outputChannels;
        currentSampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : kDefaultSampleRate;
        currentBufferSize = bufferSizeSamples > 0 ? bufferSizeSamples : kDefaultBufferSize;

        inputScratch.setSize (kMaxChannels, currentBufferSize, false, true, true);
        outputScratch.setSize (kMaxChannels, currentBufferSize, false, true, true);

        deviceIsOpen = true;
        lastError.clear();
        return {};
    }

    void close() override
    {
        stopStreamingThread();
        deviceIsOpen = false;
        deviceIsPlaying = false;
        activeCallback = nullptr;
    }

    bool isOpen() override { return deviceIsOpen; }

    void start (juce::AudioIODeviceCallback* callbackToUse) override
    {
        if (! deviceIsOpen)
            return;

        activeCallback = callbackToUse;
        deviceIsPlaying = true;

        if (activeCallback != nullptr)
            activeCallback->audioDeviceAboutToStart (this);

        if (autoStartBackgroundThread)
            startStreamingThread (backgroundPacingMs > 0);
    }

    void stop() override
    {
        stopStreamingThread();

        if (deviceIsPlaying)
        {
            deviceIsPlaying = false;
            if (activeCallback != nullptr)
                activeCallback->audioDeviceStopped();
        }
    }

    bool isPlaying() override { return deviceIsPlaying; }

    juce::String getLastError() override { return lastError; }

    int getCurrentBufferSizeSamples() override { return currentBufferSize; }
    double getCurrentSampleRate() override { return currentSampleRate; }
    int getCurrentBitDepth() override { return 24; }

    juce::BigInteger getActiveOutputChannels() const override { return activeOutputs; }
    juce::BigInteger getActiveInputChannels() const override { return activeInputs; }

    int getOutputLatencyInSamples() override { return outputLatencySamples; }
    int getInputLatencyInSamples() override { return inputLatencySamples; }

    //==============================================================================
    // Configuration & Customization
    //==============================================================================

    void setChannelSignal (int channel, SignalType type, double freqHz = 1000.0, float amp = 0.5011872f)
    {
        if (channel >= 0 && channel < kMaxChannels)
        {
            channelConfigs[channel].type = type;
            channelConfigs[channel].frequencyHz = freqHz;
            channelConfigs[channel].amplitude = amp;
            channelConfigs[channel].phase = 0.0;
        }
    }

    void setCustomGenerator (std::function<float(int channel, int sampleInBlock, int64_t totalFrames)> gen)
    {
        customGenerator = std::move (gen);
    }

    void setLatencies (int inputLatency, int outputLatency)
    {
        inputLatencySamples = inputLatency;
        outputLatencySamples = outputLatency;
    }

    void setAutoStartBackgroundThread (bool autoStart, int pacingMs = 0)
    {
        autoStartBackgroundThread = autoStart;
        backgroundPacingMs = pacingMs;
    }

    int64_t totalFramesDriven() const noexcept { return totalFramesProcessed.load(); }
    int64_t totalBlocksDriven() const noexcept { return totalBlocksProcessed.load(); }

    //==============================================================================
    // Driving Execution (Synchronous & Asynchronous)
    //==============================================================================

    /** Drives one block through the attached callback. */
    void driveBlock()
    {
        if (! deviceIsOpen || ! deviceIsPlaying || activeCallback == nullptr)
            return;

        const auto numSamples = currentBufferSize;
        const auto rate = currentSampleRate;
        const auto currentFrame = totalFramesProcessed.load (std::memory_order_relaxed);

        inputScratch.clear();
        outputScratch.clear();

        // Synthesize test signal into input scratch buffer
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            if (! activeInputs[ch])
                continue;

            auto* writePtr = inputScratch.getWritePointer (ch);
            auto& cfg = channelConfigs[ch];

            if (cfg.type == SignalType::CalibratedSine)
            {
                const auto phaseIncrement = (2.0 * juce::MathConstants<double>::pi * cfg.frequencyHz) / rate;
                for (int s = 0; s < numSamples; ++s)
                {
                    writePtr[s] = cfg.amplitude * static_cast<float> (std::sin (cfg.phase));
                    cfg.phase += phaseIncrement;
                    if (cfg.phase >= 2.0 * juce::MathConstants<double>::pi)
                        cfg.phase -= 2.0 * juce::MathConstants<double>::pi;
                }
            }
            else if (cfg.type == SignalType::StepImpulse)
            {
                for (int s = 0; s < numSamples; ++s)
                    writePtr[s] = (currentFrame + s == 0) ? cfg.amplitude : 0.0f;
            }
            else if (cfg.type == SignalType::Custom && customGenerator)
            {
                for (int s = 0; s < numSamples; ++s)
                    writePtr[s] = customGenerator (ch, s, currentFrame + s);
            }
            else
            {
                // Silence
                juce::FloatVectorOperations::clear (writePtr, numSamples);
            }
        }

        const float* inPointers[kMaxChannels];
        float* outPointers[kMaxChannels];

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            inPointers[ch] = activeInputs[ch] ? inputScratch.getReadPointer (ch) : nullptr;
            outPointers[ch] = activeOutputs[ch] ? outputScratch.getWritePointer (ch) : nullptr;
        }

        uint64_t hostTimeNs = static_cast<uint64_t> (juce::Time::getHighResolutionTicks());
        juce::AudioIODeviceCallbackContext context;
        context.hostTimeNs = &hostTimeNs;

        activeCallback->audioDeviceIOCallbackWithContext (inPointers, kMaxChannels,
                                                          outPointers, kMaxChannels,
                                                          numSamples, context);

        totalFramesProcessed.fetch_add (numSamples, std::memory_order_relaxed);
        totalBlocksProcessed.fetch_add (1, std::memory_order_relaxed);
    }

    /** Drives a specific number of blocks synchronously. */
    void driveBlocks (int count)
    {
        for (int i = 0; i < count; ++i)
            driveBlock();
    }

    /** Drives enough blocks to cover the requested duration in seconds. */
    void driveDuration (double seconds)
    {
        if (currentBufferSize <= 0 || currentSampleRate <= 0.0)
            return;

        const auto totalSamples = static_cast<int64_t> (std::round (seconds * currentSampleRate));
        const auto blocks = static_cast<int> ((totalSamples + currentBufferSize - 1) / currentBufferSize);
        driveBlocks (blocks);
    }

    /** Spawns a background streaming thread that calls driveBlock() in a loop. */
    void startStreamingThread (bool throttled = true)
    {
        if (isThreadRunning())
            return;

        throttledStreaming = throttled;
        startThread (juce::Thread::Priority::highest);
    }

    void stopStreamingThread()
    {
        if (isThreadRunning())
            stopThread (2000);
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            driveBlock();

            if (throttledStreaming)
            {
                const auto sleepMs = juce::jmax (1, static_cast<int> (
                    std::round (1000.0 * static_cast<double> (currentBufferSize) / currentSampleRate)));
                juce::Thread::sleep (sleepMs);
            }
            else
            {
                juce::Thread::yield();
            }
        }
    }

    bool deviceIsOpen = false;
    bool deviceIsPlaying = false;
    double currentSampleRate = kDefaultSampleRate;
    int currentBufferSize = kDefaultBufferSize;

    juce::BigInteger activeInputs;
    juce::BigInteger activeOutputs;
    int inputLatencySamples = 128;
    int outputLatencySamples = 128;

    juce::AudioBuffer<float> inputScratch;
    juce::AudioBuffer<float> outputScratch;

    juce::AudioIODeviceCallback* activeCallback = nullptr;
    juce::String lastError;

    ChannelConfig channelConfigs[kMaxChannels];
    std::function<float(int, int, int64_t)> customGenerator;

    std::atomic<int64_t> totalFramesProcessed { 0 };
    std::atomic<int64_t> totalBlocksProcessed { 0 };

    bool autoStartBackgroundThread = false;
    int backgroundPacingMs = 0;
    bool throttledStreaming = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeepMockAudioIODevice)
};

/**
 * DeepMockAudioDeviceType
 *
 * Exposes DeepMockAudioIODevice to JUCE's AudioDeviceManager as a first-class
 * device type ("DeepMock ASIO").
 */
class DeepMockAudioDeviceType final : public juce::AudioIODeviceType
{
public:
    explicit DeepMockAudioDeviceType (const juce::String& typeName = "DeepMock ASIO")
        : juce::AudioIODeviceType (typeName)
    {
    }

    ~DeepMockAudioDeviceType() override = default;

    void scanForDevices() override
    {
        // Static device definition
    }

    juce::StringArray getDeviceNames (bool /*wantInputNames*/) const override
    {
        return { "DeepMock Hardware Audio Interface" };
    }

    int getDefaultDeviceIndex (bool /*forInput*/) const override { return 0; }

    int getIndexOfDevice (juce::AudioIODevice* device, bool /*asInput*/) const override
    {
        return (device != nullptr && device->getName() == "DeepMock Hardware Audio Interface") ? 0 : -1;
    }

    bool hasSeparateInputsAndOutputs() const override { return false; }

    juce::AudioIODevice* createDevice (const juce::String& outputDeviceName,
                                       const juce::String& inputDeviceName) override
    {
        auto device = std::make_unique<DeepMockAudioIODevice> ("DeepMock Hardware Audio Interface");
        lastCreatedDevice = device.get();
        return device.release();
    }

    DeepMockAudioIODevice* getLastCreatedDevice() const noexcept { return lastCreatedDevice; }

private:
    DeepMockAudioIODevice* lastCreatedDevice = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeepMockAudioDeviceType)
};

} // namespace nc::mock
