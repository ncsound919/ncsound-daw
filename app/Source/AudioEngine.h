#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>

namespace nc
{
class AudioEngine final : private juce::AudioIODeviceCallback
{
public:
    static constexpr int trackCount = 8;

    AudioEngine();
    ~AudioEngine() override;

    void setPlaying (bool shouldPlay);
    bool isPlaying() const { return playing.load(); }

    void setMetronome (bool on);
    bool isMetronomeOn() const { return metronome.load(); }

    void setBpm (double newBpm);
    double getBpm() const { return bpm.load(); }

    void setTrackGain (int track, float linearGain);
    float trackGain (int track) const;
    void setTrackPan (int track, float pan);
    float trackPan (int track) const;
    void setTrackMuted (int track, bool muted);
    bool trackMuted (int track) const;
    void setTrackSolo (int track, bool soloed);
    bool trackSolo (int track) const;

    void setMasterGain (float linearGain);
    float masterGain() const { return masterGainValue.load(); }

    float trackPeak (int track) const;
    float masterPeak() const { return masterPeakValue.load(); }

    double positionSeconds() const;
    juce::String deviceDescription() const { return deviceInfo; }

    void resetPosition();

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    struct Track
    {
        std::atomic<float> gain { 0.8f };
        std::atomic<float> pan { 0.0f };
        std::atomic<float> peak { 0.0f };
        std::atomic<bool> muted { false };
        std::atomic<bool> soloed { false };
    };

    std::array<Track, trackCount> tracks;
    std::atomic<bool> playing { false };
    std::atomic<bool> metronome { true };
    std::atomic<bool> resetRequested { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int64_t> positionSamples { 0 };
    std::atomic<float> masterGainValue { 0.8f };
    std::atomic<float> masterPeakValue { 0.0f };

    juce::AudioDeviceManager deviceManager;
    juce::String deviceInfo;

    // Audio-thread only.
    double clickPhase = 0.0;
    double clickEnvelope = 0.0;
    double clickOscillator = 0.0;
    int clickBeat = 0;
    float smoothedGain[trackCount] {};
    float smoothedPan[trackCount] {};
    float smoothedMaster = 0.8f;
    bool smoothingPrimed = false;

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};
} // namespace nc
