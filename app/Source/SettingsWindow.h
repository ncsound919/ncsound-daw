#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioEngine.h"

namespace nc
{
/** Audio + MIDI device settings plus the aux bus engine swap.

    The device selector is bound straight to the engine's AudioDeviceManager, so
    input/output device, sample rate and buffer size changes apply live to the
    running session. Below it, the two aux returns can be switched between the
    stock Overlay engines and their swap targets (Nexus Reverb, Temporal Delay).
    Self-deleting on close, like AutomationLaneView. */
class SettingsWindow final : public juce::DocumentWindow
{
public:
    explicit SettingsWindow (AudioEngine& engineToUse);
    ~SettingsWindow() override = default;

    void closeButtonPressed() override;
    void resized() override;

private:
    void refreshAuxBoxes();

    AudioEngine& engine;
    juce::Component body;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label auxTitle, reverbLabel, delayLabel;
    juce::ComboBox reverbBox, delayBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};
} // namespace nc
