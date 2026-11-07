#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "AudioEngine.h"
#include "Session.h"
#include "Theme.h"
#include "Widgets.h"

namespace nc
{
class TransportBar final : public juce::Component,
                           private juce::Timer
{
public:
    explicit TransportBar (AudioEngine& engineToUse);

    std::function<void (bool)> onViewChanged;
    std::function<void()> onBrowserToggled;
    std::function<void (const juce::String&)> onStatus;

    void setView (bool mixView);
    void setBrowserOpen (bool shouldBeOpen);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void refreshTransportState();

    AudioEngine& engine;

    HardwareButton rewindButton { HardwareButton::Icon::rewind };
    HardwareButton stopButton { HardwareButton::Icon::stop };
    HardwareButton playButton { HardwareButton::Icon::play };
    HardwareButton recordButton { HardwareButton::Icon::record };
    HardwareButton loopButton { HardwareButton::Icon::loop };
    HardwareButton metronomeButton { HardwareButton::Icon::metronome };
    HardwareButton browserButton { HardwareButton::Icon::browse, "Browse" };
    SegmentedControl viewSwitch;

    LcdDisplay positionLcd;
    LcdDisplay tempoLcd;
    LcdDisplay signatureLcd;

    Meter masterLeftMeter { false, 24 };
    Meter masterRightMeter { false, 24 };

    juce::Rectangle<int> tempoArea;
    juce::Rectangle<int> masterArea;

    juce::String lastPositionMain, lastPositionSmall, lastTempo, lastMasterDb;
    bool draggingTempo = false;
    double tempoAtDragStart = 120.0;
    bool recording = false;
    bool mixView = false;
    bool browserOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
} // namespace nc
