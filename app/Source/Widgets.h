#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "Theme.h"

namespace nc
{
/** Compact hardware rotary. Values are always normalised 0..1. */
class Knob final : public juce::Component
{
public:
    explicit Knob (juce::String labelText = {}, int diameter = 40);

    void setValue (double newValue, bool sendNotification = false);
    double getValue() const { return value; }

    void setBipolar (bool shouldBeBipolar) { bipolar = shouldBeBipolar; repaint(); }
    void setDefaultValue (double newDefault) { defaultValue = juce::jlimit (0.0, 1.0, newDefault); }
    void setLabel (juce::String newLabel) { labelText = std::move (newLabel); repaint(); }
    void setShowValue (bool shouldShow) { showValue = shouldShow; repaint(); }
    void setValueText (juce::String text) { customText = std::move (text); repaint(); }
    void setAccent (juce::Colour colour) { accent = colour; repaint(); }
    void setDiameter (int newDiameter) { diameter = newDiameter; resized(); repaint(); }

    std::function<void (double)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void commit (double next);
    juce::String displayText() const;

    juce::String labelText;
    juce::String customText;
    int diameter = 40;
    double value = 0.5;
    double defaultValue = 0.5;
    double dragStartValue = 0.0;
    bool bipolar = false;
    bool showValue = false;
    bool hovering = false;
    bool dragging = false;
    bool shiftFine = false;
    juce::Colour accent = theme::accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** Console fader. Values are normalised 0..1. */
class Fader final : public juce::Component
{
public:
    Fader();

    void setValue (double newValue, bool sendNotification = false);
    double getValue() const { return value; }
    void setDefaultValue (double newDefault) { defaultValue = juce::jlimit (0.0, 1.0, newDefault); }
    void setAccent (juce::Colour colour) { accent = colour; repaint(); }
    void setShowTicks (bool shouldShow) { showTicks = shouldShow; repaint(); }

    std::function<void (double)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void commit (double next);

    double value = 0.85;
    double defaultValue = 0.85;
    double dragStartValue = 0.0;
    bool showTicks = true;
    bool hovering = false;
    bool dragging = false;
    juce::Colour accent = theme::accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Fader)
};

/** Segmented LED meter, vertical or horizontal. */
class Meter final : public juce::Component,
                    private juce::Timer
{
public:
    explicit Meter (bool isVertical = true, int numberOfSegments = 18);
    ~Meter() override = default;

    void setVertical (bool shouldBeVertical) { vertical = shouldBeVertical; repaint(); }
    void setSegments (int numberOfSegments) { segments = juce::jmax (4, numberOfSegments); repaint(); }
    void setSource (std::function<float()> newSource) { source = std::move (newSource); }
    void setThresholdColours (juce::Colour low, juce::Colour mid, juce::Colour high);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    std::function<float()> source;
    bool vertical = true;
    int segments = 18;
    float level = 0.0f;
    float peak = 0.0f;
    float peakVelocity = 0.0f;
    juce::Colour lowColour = theme::meterLow;
    juce::Colour midColour = theme::meterMid;
    juce::Colour highColour = theme::meterHigh;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Meter)
};

class HardwareButton final : public juce::Button
{
public:
    enum class Icon
    {
        none, play, stop, record, rewind, forward, loop, metronome,
        settings, close, plus, minus, search, chevronDown, power, browse
    };

    explicit HardwareButton (Icon buttonIcon = Icon::none, juce::String buttonText = {});

    void setIcon (Icon newIcon) { icon = newIcon; repaint(); }
    void setCompact (bool shouldBeCompact) { compact = shouldBeCompact; repaint(); }
    void setDanger (bool shouldBeDanger) { danger = shouldBeDanger; repaint(); }
    void setAccent (juce::Colour colour) { accent = colour; repaint(); }

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    void drawIcon (juce::Graphics&, juce::Rectangle<float>, juce::Colour) const;

    Icon icon = Icon::none;
    bool compact = false;
    bool danger = false;
    juce::Colour accent = theme::accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwareButton)
};

class SegmentedControl final : public juce::Component
{
public:
    SegmentedControl();

    void setItems (juce::StringArray newItems);
    void setSelected (int index, bool sendNotification = false);
    int getSelected() const { return selected; }
    void setCompact (bool shouldBeCompact) { compact = shouldBeCompact; repaint(); }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::StringArray items;
    int selected = 0;
    bool compact = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedControl)
};

/** Static LCD readout with an optional caption and sub-line. */
class LcdDisplay final : public juce::Component
{
public:
    LcdDisplay() = default;

    void setText (juce::String mainText, juce::String smallText = {}, juce::String captionText = {});
    void setFontHeight (float height) { mainHeight = height; repaint(); }
    void setAccent (juce::Colour colour) { accent = colour; repaint(); }

    void paint (juce::Graphics&) override;

private:
    juce::String main, small, caption;
    float mainHeight = 22.0f;
    juce::Colour accent = theme::accentHot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LcdDisplay)
};

/** One insert / send slot. */
class Slot final : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    explicit Slot (int slotIndex = 0);

    void setPluginName (juce::String name);
    void setEmptyText (juce::String text) { emptyText = std::move (text); repaint(); }
    void setAccent (juce::Colour colour) { accent = colour; repaint(); }
    void setSelected (bool shouldBeSelected) { selected = shouldBeSelected; repaint(); }
    void setBypassed (bool shouldBeBypassed) { bypassed = shouldBeBypassed; repaint(); }
    bool isBypassed() const { return bypassed; }
    void setCompact (bool shouldBeCompact) { compact = shouldBeCompact; repaint(); }
    void setNative (bool shouldBeNative);

    std::function<void()> onClick;
    std::function<void()> onBypass;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    int index = 0;
    juce::String name;
    juce::String emptyText { "a  insert" };
    bool selected = false;
    bool bypassed = false;
    bool compact = false;
    bool native = false;
    bool bypassPress = false;
    bool hovering = false;
    juce::Colour accent { theme::accent };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Slot)
};

/** Draws a deterministic, musical-looking waveform for a clip. */
void paintWaveform (juce::Graphics&,
                    juce::Rectangle<float> area,
                    juce::uint32 seed,
                    juce::Colour colour,
                    float opacity = 0.55f,
                    bool mirrored = true);

/** Draws the NCSound record-and-wave monogram. */
void paintMonogram (juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour);
} // namespace nc
