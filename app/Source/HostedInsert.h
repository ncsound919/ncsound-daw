#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <nc/engine/ProjectState.hpp>
#include <nc/engine/SyncClock.hpp>

#include <atomic>
#include <memory>

namespace nc
{
/** The small interface a track or master insert has to satisfy. Keeping it
    separate from JUCE lets the rack tests use a deterministic fake processor. */
class InsertProcessor
{
public:
    virtual ~InsertProcessor() = default;

    virtual void prepare (double sampleRate, int blockSize) = 0;
    virtual void process (juce::AudioBuffer<float>&, const nc::engine::TransportBlock&) = 0;

    /** MIDI-aware process. Effects keep the default (MIDI ignored) so a
        track insert never hears notes; a track instrument overrides this
        and receives the scheduled buffer. */
    virtual void process (juce::AudioBuffer<float>& buffer,
                          const nc::engine::TransportBlock& block,
                          juce::MidiBuffer& midi)
    {
        (void) midi;
        process (buffer, block);
    }

    virtual int latencySamples() const = 0;
    virtual void setBypassed (bool) = 0;
    virtual bool isBypassed() const = 0;
    virtual juce::String name() const = 0;
    virtual bool getState (nc::engine::PluginSlotState&) const = 0;
    virtual bool setState (const nc::engine::PluginSlotState&) = 0;
};

/** One external plugin instance. Message thread for creation, preparation, state
    and destruction; the audio callback only calls process(). */
class HostedInsert final : public InsertProcessor,
                           private juce::AudioPlayHead
{
public:
    static std::unique_ptr<HostedInsert> create (std::unique_ptr<juce::AudioPluginInstance>,
                                                 double sampleRate,
                                                 int blockSize,
                                                 juce::String& error,
                                                 bool allowInstruments = false);

    ~HostedInsert() override;

    void prepare (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, const nc::engine::TransportBlock&) override;
    void process (juce::AudioBuffer<float>&, const nc::engine::TransportBlock&,
                  juce::MidiBuffer&) override;
    int latencySamples() const override;
    void setBypassed (bool) override;
    bool isBypassed() const override;
    juce::String name() const override;
    bool getState (nc::engine::PluginSlotState&) const override;
    bool setState (const nc::engine::PluginSlotState&) override;

    juce::AudioPluginInstance* instance() const noexcept { return plugin.get(); }
    bool hadNonFiniteOutput() const noexcept { return sanitized.load(); }

    /** True when the plugin threw inside process. The insert auto-bypassed
        itself; read this on the message thread and surface it there. */
    bool processingFailed() const noexcept { return failedProcessing.load(); }

private:
    explicit HostedInsert (std::unique_ptr<juce::AudioPluginInstance>);

    juce::Optional<PositionInfo> getPosition() const override;

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::atomic<bool> bypassed { false };
    std::atomic<bool> sanitized { false };
    std::atomic<bool> failedProcessing { false };
    mutable std::atomic<int> latency { 0 };

    // The audio callback publishes one block snapshot; the plugin reads it from
    // getPosition() on the same thread. No mutex is held across processBlock.
    struct Transport
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

    Transport transport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedInsert)
};
} // namespace nc
