#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_events/juce_events.h>

#include <map>
#include <optional>
#include <string>

namespace nc
{
/** External MIDI controller layer: CC/note -> parameter learn, templates, LED feedback.
    Message thread only. The audio thread never sees this. */
class MidiLearnManager : private juce::MidiInputCallback
{
public:
    enum class Mode { absolute, relative, toggle };

    struct Binding
    {
        int midiChannel = 0;   // 0 = omni
        int controller = -1;   // CC number, or note number when isNote
        bool isNote = false;
        Mode mode = Mode::absolute;
        bool learnArmed = true;
        std::string targetParamId; // "track.<n>.gain" / "transport.play" / plugin APVTS id
    };

    MidiLearnManager();
    ~MidiLearnManager() override;

    static juce::Array<juce::MidiDeviceInfo> availableDevices();
    bool openDevice (const juce::String& id, juce::String& error);
    bool openOutput (const juce::String& id, juce::String& error);
    void closeDevices();
    juce::String activeDeviceId() const { return deviceId; }
    juce::String activeOutputId() const { return outputId; }

    bool isLearnArmed() const noexcept { return learnTarget.has_value(); }
    void armLearn (const std::string& paramId, Mode mode = Mode::absolute);
    void cancelLearn();
    bool consumeNextCc (int channel, int controller, int value, bool isNote);
    std::optional<Binding> bindingFor (int channel, int controller, bool isNote) const;

    void addBinding (const Binding& binding);
    void removeBinding (int channel, int controller, bool isNote);
    std::vector<Binding> bindings() const;

    void saveToFile (const juce::File& file) const;
    bool loadFromFile (const juce::File& file);
    void loadBuiltInTemplate (const std::string& name);

    // LED feedback: call from the message thread when a parameter changes.
    void sendLed (int channel, int controller, int value);
    void sendAllLeds (const std::map<int,int>& state);

    juce::MidiKeyboardState keyboardState;

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override;

    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String deviceId, outputId;
    std::optional<Binding> learnTarget;
    std::map<std::tuple<int,int,bool>, Binding> table;
    juce::CriticalSection lock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearnManager)
};
} // namespace nc
