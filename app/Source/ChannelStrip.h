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
class ChannelStrip final : public juce::Component
{
public:
    ChannelStrip (AudioEngine& engineToUse, int trackIndex);

    void setSelected (bool shouldBeSelected);
    void syncFromEngine();
    int getTrackIndex() const { return index; }

    std::function<void (int)> onSelected;
    std::function<void (int, int)> onInsertClicked;
    std::function<void (int)> onStripClicked;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    auto spec() const -> const TrackSpec&;
    float faderValueForGain (float gain) const;

    AudioEngine& engine;
    int index = 0;
    bool selected = false;
    bool hovering = false;
    bool armed = false;

    std::array<Slot, 5> insertSlots { Slot (0), Slot (1), Slot (2), Slot (3), Slot (4) };
    Knob panKnob { "PAN", 46 };
    Knob sendAKnob { "A", 44 };
    Knob sendBKnob { "B", 44 };
    Fader fader;
    Meter meter { true, 18 };
    HardwareButton recordButton { HardwareButton::Icon::none, "R" };
    HardwareButton muteButton { HardwareButton::Icon::none, "M" };
    HardwareButton soloButton { HardwareButton::Icon::none, "S" };

    float sendA = 0.2f;
    float sendB = 0.1f;

    juce::Rectangle<int> nameArea, insertArea, sendArea, panArea, faderArea, meterArea, buttonArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};
} // namespace nc
