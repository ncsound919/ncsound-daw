#include "AudioEngine.h"

#include <cmath>

namespace nc
{
namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr float smoothingCoefficient = 0.0025f;

float softLimit (float value)
{
    return std::tanh (value);
}
} // namespace

AudioEngine::AudioEngine()
{
    const auto error = deviceManager.initialiseWithDefaultDevices (0, 2);

    deviceInfo = error.isNotEmpty() ? "no audio device" : deviceManager.getCurrentAudioDeviceType();
    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
}

void AudioEngine::setPlaying (bool shouldPlay)    { playing.store (shouldPlay); }
void AudioEngine::setMetronome (bool on)          { metronome.store (on); }

void AudioEngine::setBpm (double newBpm)
{
    bpm.store (juce::jlimit (40.0, 240.0, newBpm));
}

void AudioEngine::setTrackGain (int track, float linearGain)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].gain.store (juce::jlimit (0.0f, 2.0f, linearGain));
}

float AudioEngine::trackGain (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].gain.load() : 0.0f;
}

void AudioEngine::setTrackPan (int track, float pan)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].pan.store (juce::jlimit (-1.0f, 1.0f, pan));
}

float AudioEngine::trackPan (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].pan.load() : 0.0f;
}

void AudioEngine::setTrackMuted (int track, bool muted)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].muted.store (muted);
}

bool AudioEngine::trackMuted (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) && tracks[static_cast<size_t> (track)].muted.load();
}

void AudioEngine::setTrackSolo (int track, bool soloed)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].soloed.store (soloed);
}

bool AudioEngine::trackSolo (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) && tracks[static_cast<size_t> (track)].soloed.load();
}

void AudioEngine::setMasterGain (float linearGain) { masterGainValue.store (juce::jlimit (0.0f, 2.0f, linearGain)); }

float AudioEngine::trackPeak (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].peak.load() : 0.0f;
}

double AudioEngine::positionSeconds() const
{
    const auto rate = sampleRate.load();
    return rate > 0.0 ? static_cast<double> (positionSamples.load()) / rate : 0.0;
}

void AudioEngine::resetPosition()
{
    // The callback owns the click state; never write it from the message thread.
    resetRequested.store (true);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate.store (device != nullptr ? device->getCurrentSampleRate() : 48000.0);
    deviceInfo = device != nullptr ? device->getName() : "no audio device";
    smoothingPrimed = false;
    resetRequested.store (true);
}

void AudioEngine::audioDeviceStopped()
{
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*,
                                                    int,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    if (numOutputChannels == 0 || numSamples <= 0)
        return;

    if (resetRequested.exchange (false))
    {
        clickPhase = 0.0;
        clickEnvelope = 0.0;
        clickOscillator = 0.0;
        clickBeat = 0;
        positionSamples.store (0);
    }

    if (! smoothingPrimed)
    {
        for (int track = 0; track < trackCount; ++track)
        {
            smoothedGain[track] = tracks[static_cast<size_t> (track)].gain.load();
            smoothedPan[track] = tracks[static_cast<size_t> (track)].pan.load();
        }

        smoothedMaster = masterGainValue.load();
        smoothingPrimed = true;
    }

    const auto rate = sampleRate.load();
    const auto tempo = bpm.load();
    const auto isPlaying = playing.load();
    const auto clickOn = metronome.load();

    const auto beatSamples = rate * 60.0 / juce::jmax (1.0, tempo);
    const auto loopSamples = beatSamples * 4.0 * 16.0;

    bool anySolo = false;

    for (const auto& track : tracks)
        anySolo = anySolo || track.soloed.load();

    float blockPeaks[trackCount] = {};
    float masterPeak = 0.0f;
    auto position = positionSamples.load();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float click = 0.0f;

        if (isPlaying)
        {
            if (clickPhase >= beatSamples)
            {
                clickPhase -= beatSamples;
                clickEnvelope = 0.85;
                clickOscillator = 0.0;
                ++clickBeat;
            }

            clickPhase += 1.0;

            if (clickOn && clickEnvelope > 0.0001)
            {
                const auto frequency = clickBeat % 4 == 0 ? 1600.0 : 1000.0;
                clickOscillator += twoPi * frequency / rate;
                click = static_cast<float> (std::sin (clickOscillator) * clickEnvelope * 0.3);
                clickEnvelope *= 0.9994;
            }

            ++position;

            if (static_cast<double> (position) >= loopSamples)
            {
                position = 0;
                clickPhase = 0.0;
                clickBeat = 0;
            }
        }

        float left = 0.0f;
        float right = 0.0f;

        for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        {
            const auto& track = tracks[static_cast<size_t> (trackIndex)];

            smoothedGain[trackIndex] += (track.gain.load() - smoothedGain[trackIndex]) * smoothingCoefficient;
            smoothedPan[trackIndex] += (track.pan.load() - smoothedPan[trackIndex]) * smoothingCoefficient;

            blockPeaks[trackIndex] = juce::jmax (blockPeaks[trackIndex], 0.0f);

            // No clip sources exist yet, so the strips read their smoothed gain state.
            juce::ignoreUnused (anySolo);
            juce::ignoreUnused (track);
        }

        // The metronome sits after the mixer: muting a track cannot silence the click.
        left += click;
        right += click;

        smoothedMaster += (masterGainValue.load() - smoothedMaster) * smoothingCoefficient;
        left = softLimit (left * smoothedMaster);
        right = softLimit (right * smoothedMaster);
        masterPeak = juce::jmax (masterPeak, std::abs (left), std::abs (right));

        if (outputChannelData[0] != nullptr)
            outputChannelData[0][sample] = left;

        if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
            outputChannelData[1][sample] = right;
    }

    positionSamples.store (position);

    for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        tracks[static_cast<size_t> (trackIndex)].peak.store (blockPeaks[trackIndex]);

    masterPeakValue.store (masterPeak);
}
} // namespace nc
