#pragma once

#include <nc/engine/MidiClip.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace nc
{
/** Small piano-roll editor for one MIDI clip. Notes live clip-relative in
    beats (0 == clip start). Click empty space to add a note, drag a note to
    move it, drag its right edge to resize, right-click to delete. The grid
    snaps to 16ths; pitch is 0..127 with octave labels. Shown inside a
    DocumentWindow by ArrangeView. */
class PianoRoll final : public juce::Component
{
public:
    PianoRoll (juce::String clipName, double lengthBars,
               std::vector<nc::engine::MidiNote> initialNotes,
               std::function<void (const std::vector<nc::engine::MidiNote>&)> onEdited);
    ~PianoRoll() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;

    const std::vector<nc::engine::MidiNote>& notes() const { return edited; }

private:
    juce::Rectangle<float> gridArea() const { return getLocalBounds().withTrimmedTop (44).withTrimmedBottom (28).toFloat(); }
    juce::Point<float> beatPitchToXY (double beat, int pitch) const;
    void xyToBeatPitch (juce::Point<float> point, double& beat, int& pitch) const;
    int noteAt (juce::Point<int> point) const;
    bool nearRightEdge (const nc::engine::MidiNote& note, juce::Point<int> point) const;
    void publish();

    juce::String title;
    double lengthBeats = 16.0;
    std::vector<nc::engine::MidiNote> edited;
    std::function<void (const std::vector<nc::engine::MidiNote>&)> onEdited;

    int selected = -1;
    int dragMode = 0; // 0 none, 1 move, 2 resize
    double dragOriginBeat = 0.0;
    int dragOriginPitch = 60;
    double dragOriginLength = 0.25;
    double dragStartBeat = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
} // namespace nc
