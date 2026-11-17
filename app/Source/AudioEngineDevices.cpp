// The JUCE audio device callbacks. Split out of AudioEngineRender.cpp so the
// render path file stays readable; bodies moved verbatim.

#include "AudioEngine.h"
#include "AudioEngineInternal.h"

namespace nc
{

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate.store (device != nullptr ? device->getCurrentSampleRate() : 48000.0);
    bufferSize.store (device != nullptr ? device->getCurrentBufferSizeSamples() : 0);
    deviceOpen.store (device != nullptr);
    deviceInfo = device != nullptr ? device->getName() : "no audio device";
    capture.captureAvailable.store (device != nullptr
                            && device->getActiveInputChannels().countNumberOfSetBits() > 0);
    scratch.smoothingPrimed = false;
    clock.requestReset();

    mixBuffer.setSize (2, juce::jmax (bufferSize.load(), 4096), false, true, false);
    instrumentBlock.setSize (2, juce::jmax (bufferSize.load(), 4096), false, true, false);
    trackBlock.setSize (2 * trackCount, juce::jmax (bufferSize.load(), 16384), false, true, false);
    auxABuffer.setSize (2, juce::jmax (bufferSize.load(), 4096), false, true, false);
    auxBBuffer.setSize (2, juce::jmax (bufferSize.load(), 4096), false, true, false);
    midiEventScratch.clear();
    midiEventScratch.reserve (4096);

    const auto maxBlock = juce::jmax (1, bufferSize.load());
    const auto active = juce::jlimit (0, trackCount, activeTrackCount.load());

    for (int i = 0; i < active; ++i)
        mixRack[static_cast<size_t> (i)].prepare (sampleRate.load(), maxBlock);

    masterRack.prepare (sampleRate.load(), maxBlock);
    preparedRackCount.store (active);
    inserts.prepare (sampleRate.load(), maxBlock, active);
    auxReverb.prepare (sampleRate.load(), maxBlock,
                       static_cast<AuxBus::Engine> (auxReverbEngine.load()));
    auxDelay.prepare (sampleRate.load(), maxBlock,
                      static_cast<AuxBus::Engine> (auxDelayEngine.load()));

    if (juce::SystemStats::getEnvironmentVariable ("NC_AUDIO_DUMP", {}).isNotEmpty())
        juce::File (juce::SystemStats::getEnvironmentVariable ("NC_AUDIO_DUMP", {}))
            .getSiblingFile ("audio-diag.txt")
            .replaceWithText ("device=" + deviceInfo
                              + "\nsampleRate=" + juce::String (sampleRate.load())
                              + "\nbufferSize=" + juce::String (bufferSize.load())
                              + "\nactiveTracks=" + juce::String (activeTrackCount.load())
                              + "\ninputsAvailable=" + juce::String (capture.captureAvailable.load() ? 1 : 0));
}

void AudioEngine::audioDeviceStopped()
{
    state.automationRetired.clear();
    clock.reclaimAll();
    midiRetired.clear();
    deviceOpen.store (false);
    bufferSize.store (0);
    dspLoadValue.store (0.0f);
    masterPeakValue.store (0.0f);

    for (auto& track : tracks)
        track.peak.store (0.0f);

    for (auto& rack : mixRack)
        rack.reset();

    masterRack.reset();
    preparedRackCount.store (0);
    inserts.collectGarbage();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    // One tick per callback. Retired snapshots are stamped with this value at
    // the swap and freed only after a later callback has started.
    clock.countBlock();
    // An offline mixdown owns the engine. Render silence rather than run a
    // second pass into the device, and touch no shared state while doing it.
    if (offlineRender.load (std::memory_order_acquire))
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

        // Export waits on this instead of guessing how long a block takes.
        offlineBlockDone.signal();
        return;
    }

    const auto startTicks = juce::Time::getHighResolutionTicks();

    renderBlock (inputChannelData, numInputChannels, outputChannelData,
                 numOutputChannels, numSamples);

    // Measured callback load, not a sine wave pretending to be a DSP meter.
    const auto elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const auto budget = static_cast<double> (numSamples) / juce::jmax (1.0, sampleRate.load());
    const auto ratio = budget > 0.0 ? static_cast<float> (elapsed / budget) : 0.0f;
    dspLoadValue.store (juce::jlimit (0.0f, 1.0f, dspLoadValue.load() * 0.82f + juce::jlimit (0.0f, 1.0f, ratio) * 0.18f));
    dspBlockCount.fetch_add (1, std::memory_order_relaxed);
    if (ratio > 1.0f)
        dspOverrunCount.fetch_add (1, std::memory_order_relaxed);
}

} // namespace nc
