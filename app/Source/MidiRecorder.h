#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <nc/engine/MidiClip.hpp>
#include <nc/engine/ProjectState.hpp>
#include <nc/engine/SpscQueue.hpp>
#include <nc/engine/TempoMap.hpp>

#include <atomic>
#include <memory>
#include <vector>

namespace nc
{
/** Captures live MIDI input and turns it into a quantized clip.

    The MidiInputCallback runs on a high-priority system thread, so it
    does the minimum: stamp and try-push into a lock-free queue. The
    message thread drains via drain(), converts host timestamps to PPQ
    through the tempo map, quantizes, and pairs note-ons/offs. No
    allocation or lock in the callback. */
class MidiRecorder final : private juce::MidiInputCallback
{
public:
    MidiRecorder();
    ~MidiRecorder() override;

    // Devices (message thread)
    static juce::Array<juce::MidiDeviceInfo> availableDevices();
    bool openDevice (const juce::String& deviceIdentifier, juce::String& error);
    void closeDevice();
    juce::String activeDeviceId() const { return activeDeviceIdentifier; }

    // Recording session (message thread)
    void startRecording (int track, double startPpq, const nc::engine::TempoMap& tempoMap);
    bool isRecording() const noexcept { return recording.load (std::memory_order_acquire); }

    /** Drains captured messages into notes/CCs/bends. Call periodically
        while recording and once at stop. Returns false if nothing was
        captured yet. */
    bool drain();

    /** Finalises and returns the recorded clip (empty when nothing played). */
    nc::engine::ClipState stopRecording();

    int droppedMessages() const noexcept { return dropped.load (std::memory_order_relaxed); }

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override;

    std::unique_ptr<juce::MidiInput> input;
    juce::String activeDeviceIdentifier;
    nc::engine::SpscQueue<juce::MidiMessage> queue { 8192 };
    std::atomic<bool> recording { false };
    std::atomic<int> dropped { 0 };

    int track = 0;
    double startPpq = 0.0;
    double hostBaseSeconds = 0.0;      // MIDI host clock at record start
    double transportBaseSeconds = 0.0; // transport seconds at record start
    nc::engine::TempoMap tempoMap;

    struct OpenNote { double beat; int velocity; int channel; int pitch; };
    std::vector<std::vector<OpenNote>> openNotes;
    std::vector<nc::engine::MidiNote> notes;
    std::vector<nc::engine::MidiCC> ccs;
    std::vector<nc::engine::MidiBend> bends;
    double lastBeat = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRecorder)
};
} // namespace nc
