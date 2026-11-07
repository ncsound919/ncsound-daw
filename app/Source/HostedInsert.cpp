#include "HostedInsert.h"

#include <algorithm>
#include <cmath>

namespace nc
{
HostedInsert::HostedInsert (std::unique_ptr<juce::AudioPluginInstance> instanceToUse)
    : plugin (std::move (instanceToUse))
{
}

std::unique_ptr<HostedInsert> HostedInsert::create (std::unique_ptr<juce::AudioPluginInstance> instance,
                                                    double sampleRate,
                                                    int blockSize,
                                                    juce::String& error,
                                                    bool allowInstruments)
{
    if (instance == nullptr)
    {
        error = "The plugin could not be instantiated";
        return {};
    }

    // JUCE 8 dropped AudioPluginInstance::isInstrument(). Instruments are
    // recognised by having no audio input channels (MIDI in, stereo out);
    // effects keep at least one input channel.
    const auto isInst = instance->getTotalNumInputChannels() == 0;

    if (isInst && ! allowInstruments)
    {
        error = "This is an instrument; inserts are effects-only. Load it as the track instrument instead.";
        return {};
    }

    if (instance->getBusCount (true) == 0 && ! isInst)
    {
        error = "The plugin could not be instantiated";
        return {};
    }

    instance->setPlayHead (nullptr);

    // Effects are stereo in / stereo out. Instruments are MIDI in (no audio
    // input channels) / stereo out; asking for stereo input on an instrument
    // fails the negotiation and every synth would be rejected. The input
    // request tracks the plugin's own input channel count so a mono effect
    // still lands on its own layout instead of being forced stereo.
    const auto desiredInputs = isInst ? 0 : juce::jmax (1, instance->getTotalNumInputChannels());
    instance->setPlayConfigDetails (desiredInputs, 2, sampleRate, blockSize);

    // setPlayConfigDetails returns void in JUCE, so verify the negotiation
    // actually landed on a stereo main bus (JUCE bus-layout tutorial: the
    // first bus is the main bus). Without this, a mono or multi-channel-only
    // plugin silently runs mis-configured instead of being rejected.
    const auto mainIn = instance->getMainBusNumInputChannels();
    const auto mainOut = instance->getMainBusNumOutputChannels();

    if (mainOut != 2 || instance->getTotalNumOutputChannels() != 2)
    {
        error = "The plugin does not support a stereo main bus";
        return {};
    }

    if (! isInst && (mainIn != 2 || instance->getTotalNumInputChannels() != 2))
    {
        error = "The plugin does not support a stereo main bus";
        return {};
    }

    if (instance->getChannelLayoutOfBus (false, 0) != juce::AudioChannelSet::stereo())
    {
        error = "The plugin does not support a stereo main bus";
        return {};
    }

    if (! isInst && instance->getChannelLayoutOfBus (true, 0) != juce::AudioChannelSet::stereo())
    {
        error = "The plugin does not support a stereo main bus";
        return {};
    }

    auto hosted = std::unique_ptr<HostedInsert> (new HostedInsert (std::move (instance)));
    hosted->transport.sampleRate.store (juce::jmax (1.0, sampleRate));
    hosted->plugin->setPlayHead (hosted.get());
    hosted->prepare (sampleRate, blockSize);
    return hosted;
}

HostedInsert::~HostedInsert()
{
    if (plugin != nullptr)
    {
        plugin->setPlayHead (nullptr);
        plugin->releaseResources();
    }
}

void HostedInsert::prepare (double sampleRate, int blockSize)
{
    if (plugin == nullptr)
        return;

    transport.sampleRate.store (juce::jmax (1.0, sampleRate));
    plugin->setRateAndBufferSizeDetails (sampleRate, blockSize);
    plugin->prepareToPlay (sampleRate, blockSize);
    latency.store (juce::jmax (0, plugin->getLatencySamples()));
}

int HostedInsert::latencySamples() const
{
    // Plugins can change latency at runtime via setLatencySamples(), which
    // triggers updateHostDisplay. JUCE's own host guidance is to poll
    // getLatencySamples() (a noexcept getter) at the block boundary rather
    // than trust the value captured in prepare(); TrackInsertRack::beginBlock
    // is that boundary. Refreshing here keeps PDC correct for oversampling
    // toggles and adaptive processors.
    if (plugin != nullptr)
        latency.store (juce::jmax (0, plugin->getLatencySamples()));

    return latency.load (std::memory_order_relaxed);
}

void HostedInsert::setBypassed (bool shouldBeBypassed)
{
    bypassed.store (shouldBeBypassed, std::memory_order_relaxed);
}

bool HostedInsert::isBypassed() const
{
    return bypassed.load (std::memory_order_relaxed);
}

juce::String HostedInsert::name() const
{
    return plugin != nullptr ? plugin->getName() : juce::String();
}

void HostedInsert::process (juce::AudioBuffer<float>& buffer, const nc::engine::TransportBlock& block)
{
    juce::MidiBuffer emptyMidi;
    process (buffer, block, emptyMidi);
}

void HostedInsert::process (juce::AudioBuffer<float>& buffer,
                            const nc::engine::TransportBlock& block,
                            juce::MidiBuffer& midi)
{
    if (plugin == nullptr || buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    const juce::ScopedNoDenormals noDenormals;

    transport.bpm.store (block.tempo, std::memory_order_relaxed);
    transport.ppq.store (block.projectTimeMusic, std::memory_order_relaxed);
    transport.ppqBarStart.store (block.barPositionMusic, std::memory_order_relaxed);
    transport.timeSamples.store (block.projectTimeSamples, std::memory_order_relaxed);
    transport.playing.store (block.isPlaying, std::memory_order_relaxed);
    transport.looping.store (block.isLooping, std::memory_order_relaxed);
    transport.loopStart.store (block.cycleStartMusic, std::memory_order_relaxed);
    transport.loopEnd.store (block.cycleEndMusic, std::memory_order_relaxed);

    if (isBypassed())
        return;

    // Third-party DSP is the least trustworthy code in the process. A throw must
    // never unwind through the audio callback, so contain it here: hand the caller
    // silence, drop the insert out of the signal path, and let the message thread
    // decide what to tell the user.
    try
    {
        plugin->processBlock (buffer, midi);
    }
    catch (...)
    {
        buffer.clear();
        failedProcessing.store (true, std::memory_order_relaxed);
        bypassed.store (true, std::memory_order_relaxed);
        return;
    }

    // A broken plugin must not poison the bus or the project. Replace non-finite
    // output with silence and remember that the block was sanitized.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (! std::isfinite (samples[i]))
            {
                samples[i] = 0.0f;
                sanitized.store (true, std::memory_order_relaxed);
            }
        }
    }
}

bool HostedInsert::getState (nc::engine::PluginSlotState& state) const
{
    if (plugin == nullptr)
        return false;

    const auto description = plugin->getPluginDescription();
    state.identifier = description.createIdentifierString().toStdString();
    state.format = description.pluginFormatName.toStdString();

    if (const auto xml = description.createXml())
        state.descriptionXml = xml->toString().toStdString();

    juce::MemoryBlock memory;
    plugin->getStateInformation (memory);
    state.stateBase64 = memory.toBase64Encoding().toStdString();
    state.bypassed = isBypassed();
    return true;
}

bool HostedInsert::setState (const nc::engine::PluginSlotState& state)
{
    if (plugin == nullptr)
        return false;

    if (! state.stateBase64.empty())
    {
        juce::MemoryBlock memory;

        if (! memory.fromBase64Encoding (juce::String (state.stateBase64)))
            return false;

        if (memory.getSize() > 0)
            plugin->setStateInformation (memory.getData(), static_cast<int> (memory.getSize()));
    }

    setBypassed (state.bypassed);
    return true;
}

juce::Optional<HostedInsert::PositionInfo> HostedInsert::getPosition() const
{
    PositionInfo info;
    const auto rate = juce::jmax (1.0, transport.sampleRate.load (std::memory_order_relaxed));
    const auto samples = transport.timeSamples.load (std::memory_order_relaxed);

    info.setBpm (transport.bpm.load (std::memory_order_relaxed));
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    info.setTimeInSamples (samples);
    info.setTimeInSeconds (static_cast<double> (samples) / rate);
    info.setPpqPosition (transport.ppq.load (std::memory_order_relaxed));
    info.setPpqPositionOfLastBarStart (transport.ppqBarStart.load (std::memory_order_relaxed));
    info.setIsPlaying (transport.playing.load (std::memory_order_relaxed));
    info.setIsRecording (false);
    info.setIsLooping (transport.looping.load (std::memory_order_relaxed));

    if (transport.looping.load (std::memory_order_relaxed))
        info.setLoopPoints (juce::AudioPlayHead::LoopPoints {
            transport.loopStart.load (std::memory_order_relaxed),
            transport.loopEnd.load (std::memory_order_relaxed) });

    return info;
}
} // namespace nc
