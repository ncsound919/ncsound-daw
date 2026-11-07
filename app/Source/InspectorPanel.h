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

    std::array<bool, 8> armed {};
    std::array<float, 8> sendA { 0.18f, 0.06f, 0.30f, 0.22f, 0.10f, 0.16f, 0.0f, 0.0f };
    std::array<float, 8> sendB { 0.08f, 0.0f, 0.12f, 0.18f, 0.0f, 0.08f, 0.0f, 0.0f };
    std::array<float, 8> widths { 0.62f, 0.42f, 0.78f, 0.66f, 0.54f, 0.70f, 0.9f, 0.85f };

    juce::Rectangle<int> headerArea, instrumentArea, insertsArea, sendsArea, channelArea, outputArea, gainLabelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InspectorPanel)
};
} // namespace nc
