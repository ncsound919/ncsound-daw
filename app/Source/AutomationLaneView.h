#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Automation.h"
#include "AudioEngine.h"

namespace nc
{
/** Draw surface for one track's volume or pan automation lane.

    The view holds a working copy of the lane and publishes every edit to
    AudioEngine immediately (so playback hears it), with its own undo stack.
    Click adds a point, dragging a point moves it, Alt-click deletes it.
    The curve selector changes the selected point's curve.
    Beats are absolute song beats (bar 1 = beat 0). */
class AutomationLaneView final : public juce::DocumentWindow
{
public:
    AutomationLaneView (AudioEngine& engineToUse, int trackToEdit);
    ~AutomationLaneView() override = default;

    void closeButtonPressed() override;
    void resized() override;

private:
    class Surface final : public juce::Component
    {
    public:
        Surface (AutomationLaneView& ownerToUse);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        double xToBeats (float x) const;
        float beatsToX (double beats) const;
        float valueToY (float value) const;
        float yToValue (float y) const;
        int pointAt (juce::Point<float> point) const;

        AutomationLaneView& owner;
        int draggingPoint = -1;
        bool dragUndoPushed = false;
    };

    void refreshHeader();
    void pushUndo();
    void publish();

    AudioEngine& engine;
    int track = 0;
    AutomationParam param = AutomationParam::Volume;
    AutomationLane lane;
    std::vector<AutomationLane> undoStack;
    Surface surface { *this };
    juce::Label headerLabel;
    juce::ComboBox paramBox;
    juce::ComboBox curveBox;
    juce::TextButton clearButton { "Clear" };
    juce::TextButton undoButton { "Undo" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationLaneView)
};
} // namespace nc
