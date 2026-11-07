#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "ChannelStrip.h"
#include "Theme.h"
#include "Widgets.h"

namespace nc
{
class MasterStrip final : public juce::Component
{
public:
    explicit MasterStrip (AudioEngine& engineToUse);

    void syncFromEngine();
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onRackClicked;
    std::function<void()> onInsertClicked;

private:
    AudioEngine& engine;
    Fader fader;
    Meter leftMeter { true, 20 };
    Meter rightMeter { true, 20 };
    Slot insertOne { 0 };
    Slot insertTwo { 1 };
    HardwareButton dimButton { HardwareButton::Icon::none, "DIM" };
    HardwareButton monoButton { HardwareButton::Icon::none, "MONO" };
    HardwareButton abButton { HardwareButton::Icon::none, "A/B" };
    HardwareButton automixButton { HardwareButton::Icon::none, "AUTO" };

    juce::Rectangle<int> headerArea, insertArea, faderArea, meterArea, buttonArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterStrip)
};

class MixerPanel final : public juce::Component
{
public:
    explicit MixerPanel (AudioEngine& engineToUse);

    std::function<void (int, int)> onInsertClicked;
    std::function<void (int)> onTrackSelected;
    std::function<void (int)> onStripClicked;
    std::function<void()> onMasterRackClicked;
    std::function<void()> onMasterInsertClicked;

    void setSelectedTrack (int track);
    int getSelectedTrack() const { return selectedTrack; }
    void syncFromEngine();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void updateScrollThumb();
    void scrollToX (int x);
    // Narrower than this a strip stops being operable, so wide sessions scroll
    // horizontally instead of shrinking every control past readability.
    static constexpr int minStripWidth = 132;

    AudioEngine& engine;
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    int firstVisibleStrip = 0;
    int visibleStripCount = 1;
    int shownTrackCount = 1;
    MasterStrip master;
    int selectedTrack = 0;
    juce::Rectangle<int> headerArea, scrollBarArea;
    juce::Rectangle<float> scrollThumbArea;
    bool draggingScrollThumb = false;
    int scrollDragStartX = 0;
    int scrollDragStartIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
};
} // namespace nc
