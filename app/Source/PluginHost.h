#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <nc/engine/LatencyComp.hpp>
#include <nc/engine/SyncClock.hpp>

#include <atomic>
#include <memory>
#include <vector>

namespace nc
{
/** Hosts one external plugin on the master insert.

    A plugin only reports a latency in samples and asks for transport state; the
    host owns the rest. This class supplies the block-start ProcessContext every
    VST3 plugin expects, so tempo-synced delays, LFOs and plugin-internal
    sequencers lock to the project clock instead of free-running.
*/
class PluginHost final : public juce::AudioPlayHead
{
public:
    PluginHost();
    ~PluginHost() override;

    bool loadDescription (const juce::PluginDescription&, double sampleRate, int blockSize,
                          juce::String& error);
    void releasePlugin();
    bool hasPlugin() const { return instance != nullptr; }
    juce::String pluginName() const { return instance != nullptr ? instance->getName() : juce::String(); }

    /** Identifier of the loaded plugin, empty when nothing is loaded. */
    juce::String loadedIdentifier() const;

    /** The insert's full state, base64 encoded for the project file. */
    juce::String stateBase64() const;

    bool applyStateBase64 (const juce::String& base64);

    /** Plugin latency in samples, as reported. Never an estimate. */
    int latencySamples() const;

    void prepare (double sampleRate, int blockSize);

    /** Processes the mixed block through the insert, with block-start context. */
    void processBlock (juce::AudioBuffer<float>& buffer, const nc::engine::TransportBlock& block);

    juce::Optional<PositionInfo> getPosition() const override;

private:
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::String loadedIdentifierValue;

    /** One block snapshot: the audio thread writes every field once per block,
        and the plugin reads them back from getPosition() on that same thread.
        Plain atomics, so processBlock never takes a lock a plugin could block on
        - the old CriticalSection could deadlock against
        AudioProcessor::getCallbackLock(). */
    struct TransportState
    {
        std::atomic<double> sampleRate { 48000.0 };
        std::atomic<double> bpm { 120.0 };
        std::atomic<double> ppq { 0.0 };
        std::atomic<double> ppqBarStart { 0.0 };
        std::atomic<double> loopStart { 0.0 };
        std::atomic<double> loopEnd { 0.0 };
        std::atomic<juce::int64> timeSamples { 0 };
        std::atomic<bool> playing { false };
        std::atomic<bool> looping { false };
    };

    TransportState transport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHost)
};
} // namespace nc
