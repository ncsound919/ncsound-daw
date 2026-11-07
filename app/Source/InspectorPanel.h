#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "AudioEngine.h"
#include "Session.h"
#include "Theme.h"
#include "Widgets.h"

namespace nc
{
class InspectorPanel final : public juce::Component
{
public:
    explicit InspectorPanel (AudioEngine& engineToUse);

    void setTrack (int trackIndex);
    void syncFromEngine();
    int getTrack() const { return trackIndex; }

    std::function<void (int)> onInsertClicked;
    std::function<void (int)> onStripClicked;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshFromEngine();
    auto track() const -> const TrackSpec&;

    AudioEngine& engine;
    int trackIndex = 0;

    std::array<Slot, 5> insertSlots { Slot (0), Slot (1), Slot (2), Slot (3), Slot (4) };
    Knob panKnob { "PAN", 48 };
    Knob sendAKnob { "SEND A", 46 };
    Knob sendBKnob { "SEND B", 46 };
    Knob widthKnob { "WIDTH", 44 };
    Fader gainFader;
    Meter levelMeter { true, 16 };
    HardwareButton recordButton { HardwareButton::Icon::none, "R" };
    HardwareButton muteButton { HardwareButton::Icon::none, "M" };
    HardwareButton soloButton { HardwareButton::Icon::none, "S" };

    std::array<bool, AudioEngine::trackCount> armed {};
    // Sends/width are owned by AudioEngine (see InspectorPanel.cpp). These
    // members are legacy UI defaults from the pre-engine wiring; kept with
    // full-size arrays so no index can go OOB. setTrack pulls from the engine.

    juce::Rectangle<int> headerArea, instrumentArea, insertsArea, sendsArea, channelArea, outputArea, gainLabelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InspectorPanel)
};
} // namespace nc
