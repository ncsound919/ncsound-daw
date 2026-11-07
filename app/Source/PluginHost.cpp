#include "PluginHost.h"

#include <juce_audio_processors/format/juce_AudioPluginFormatManagerHelpers.h>

namespace nc
{
PluginHost::PluginHost()
{
    // JUCE 8 replaced AudioPluginFormatManager::addDefaultFormats() with this helper.
    juce::addDefaultFormatsToManager (formats);
}

PluginHost::~PluginHost()
{
    releasePlugin();
}

bool PluginHost::loadDescription (const juce::PluginDescription& description,
                                  double sampleRate, int blockSize,
                                  juce::String& error)
{
    releasePlugin();

    for (auto* format : formats.getFormats())
    {
        if (! format->getName().equalsIgnoreCase (description.pluginFormatName))
            continue;

        instance = format->createInstanceFromDescription (description, sampleRate, blockSize, error);
        break;
    }

    if (instance == nullptr)
        return false;

    instance->setPlayHead (this);
    instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
    transport.sampleRate.store (juce::jmax (1.0, sampleRate), std::memory_order_relaxed);
    instance->prepareToPlay (sampleRate, blockSize);
    loadedIdentifierValue = description.createIdentifierString();
    return true;
}

void PluginHost::releasePlugin()
{
    if (instance != nullptr)
    {
        instance->releaseResources();
        instance.reset();
    }

    loadedIdentifierValue.clear();
}

int PluginHost::latencySamples() const
{
    return instance != nullptr ? juce::jmax (0, instance->getLatencySamples()) : 0;
}

void PluginHost::prepare (double sampleRate, int blockSize)
{
    transport.sampleRate.store (juce::jmax (1.0, sampleRate), std::memory_order_relaxed);

    if (instance != nullptr)
        instance->prepareToPlay (sampleRate, blockSize);
}

void PluginHost::processBlock (juce::AudioBuffer<float>& buffer,
                               const nc::engine::TransportBlock& block)
{
    if (instance == nullptr || buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    transport.bpm.store (block.tempo, std::memory_order_relaxed);
    transport.ppq.store (block.projectTimeMusic, std::memory_order_relaxed);
    transport.ppqBarStart.store (block.barPositionMusic, std::memory_order_relaxed);
    transport.timeSamples.store (block.projectTimeSamples, std::memory_order_relaxed);
    transport.playing.store (block.isPlaying, std::memory_order_relaxed);
    transport.looping.store (block.isLooping, std::memory_order_relaxed);
    transport.loopStart.store (block.cycleStartMusic, std::memory_order_relaxed);
    transport.loopEnd.store (block.cycleEndMusic, std::memory_order_relaxed);

    const juce::ScopedNoDenormals noDenormals;

    juce::AudioBuffer<float> pluginBuffer (buffer.getArrayOfWritePointers(),
                                          buffer.getNumChannels(), buffer.getNumSamples());
    juce::MidiBuffer midi;
    instance->processBlock (pluginBuffer, midi);
}

juce::Optional<PluginHost::PositionInfo> PluginHost::getPosition() const
{
    PositionInfo info;
    const auto rate = juce::jmax (1.0, transport.sampleRate.load (std::memory_order_relaxed));
    const auto samples = transport.timeSamples.load (std::memory_order_relaxed);

    info.setBpm (transport.bpm.load (std::memory_order_relaxed));
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    info.setTimeInSamples (samples);
    // Seconds come from the real sample rate. This used to divide by a hardcoded
    // 48000 while also multiplying by tempo, so a 44.1 kHz session reported a
    // tempo-scaled position that was wrong twice over.
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

juce::String PluginHost::loadedIdentifier() const
{
    if (instance == nullptr)
        return {};

    return loadedIdentifierValue;
}

juce::String PluginHost::stateBase64() const
{
    if (instance == nullptr)
        return {};

    juce::MemoryBlock state;
    instance->getStateInformation (state);
    return state.toBase64Encoding();
}

bool PluginHost::applyStateBase64 (const juce::String& base64)
{
    if (instance == nullptr || base64.isEmpty())
        return false;

    juce::MemoryBlock state;

    if (! state.fromBase64Encoding (base64) || state.getSize() == 0)
        return false;

    instance->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    return true;
}
} // namespace nc
