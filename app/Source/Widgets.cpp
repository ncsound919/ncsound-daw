#include "Widgets.h"

#include <cmath>

namespace nc
{
namespace
{
constexpr double startAngle = -2.356194490192345; // 7:30
constexpr double sweepAngle = 4.71238898038469;   // 270 degrees

juce::Point<float> polar (juce::Point<float> centre, float radius, double angle)
{
    return { centre.x + std::sin (static_cast<float> (angle)) * radius,
             centre.y - std::cos (static_cast<float> (angle)) * radius };
}

juce::String upper (juce::String text)
{
    return text.toUpperCase();
}

juce::String shortInsertName (const juce::String& fullName)
{
    auto name = fullName;

    if (name.startsWithIgnoreCase ("Overlay "))
        name = name.substring (8);

    if (name.equalsIgnoreCase ("Channel Strip")) return "Strip";
    if (name.equalsIgnoreCase ("Retro Color"))   return "Retro";
    if (name.equalsIgnoreCase ("Modulator"))     return "Mod";
    if (name.equalsIgnoreCase ("Reverb"))       return "Verb";
    if (name.equalsIgnoreCase ("Temporal Delay")) return "Delay";
    if (name.equalsIgnoreCase ("Nexus Reverb")) return "Nexus";
    if (name.equalsIgnoreCase ("VoxStudio"))    return "Vox";
    if (name.equalsIgnoreCase ("Supa Level"))   return "Supa";
    if (name.equalsIgnoreCase ("Aero Amp"))     return "Amp";
    if (name.equalsIgnoreCase ("Mixology"))     return "Mix";

    if (name.length() > 9)
        name = name.substring (0, 9).trimEnd();

    return name;
}
} // namespace

//==============================================================================
Knob::Knob (juce::String labelTextIn, int diameterIn)
    : labelText (std::move (labelTextIn)), diameter (diameterIn)
{
    setSize (diameter + 24, diameter + 30);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setWantsKeyboardFocus (true);
    setTitle (labelText);
}

void Knob::setValue (double newValue, bool sendNotification)
{
    const auto clamped = juce::jlimit (0.0, 1.0, newValue);

    if (std::abs (clamped - value) < 0.000001)
        return;

    value = clamped;
    repaint();

    if (sendNotification && onChange)
        onChange (value);
}

void Knob::resized()
{
    const auto width = juce::jmax (getWidth(), diameter + 24);
    const auto height = juce::jmax (getHeight(), diameter + 30);

    if (width != getWidth() || height != getHeight())
        setSize (width, height);
}

void Knob::commit (double next)
{
    setValue (next);

    if (onChange)
        onChange (value);
}

juce::String Knob::displayText() const
{
    if (customText.isNotEmpty())
        return customText;

    if (showValue)
        return juce::String (juce::roundToInt (value * 100.0)) + "%";

    return {};
}

void Knob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    auto knob = area.removeFromTop (static_cast<float> (diameter)).withSizeKeepingCentre (static_cast<float> (diameter), static_cast<float> (diameter));

    const auto centre = knob.getCentre();
    const auto radius = knob.getWidth() * 0.5f;
    const auto active = hovering || dragging;

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (knob.translated (0.0f, 2.0f).reduced (1.5f));

    if (active)
    {
        g.setColour (accent.withAlpha (0.16f));
        g.fillEllipse (knob.expanded (2.0f));
    }

    // Tick ring.
    g.setColour (theme::line);
    for (int i = 0; i <= 10; ++i)
    {
        const auto angle = startAngle + sweepAngle * static_cast<double> (i) / 10.0;
        const auto outer = polar (centre, radius - 1.5f, angle);
        const auto inner = polar (centre, radius - (i % 5 == 0 ? 5.0f : 3.8f), angle);
        g.drawLine ({ inner, outer }, 1.0f);
    }

    const auto trackRadius = radius - juce::jmax (4.0f, radius * 0.14f);
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                         static_cast<float> (startAngle), static_cast<float> (startAngle + sweepAngle), true);
    g.setColour (theme::well);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto fromAngle = bipolar ? startAngle + sweepAngle * 0.5 : startAngle;
    const auto toAngle = startAngle + sweepAngle * value;

    if (std::abs (toAngle - fromAngle) > 0.001)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                           static_cast<float> (fromAngle), static_cast<float> (toAngle), true);

        g.setColour (accent.withAlpha (active ? 0.22f : 0.12f));
        g.strokePath (arc, juce::PathStrokeType (6.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (active ? theme::accentHot : accent);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Metal body.
    const auto bodyRadius = radius - juce::jmax (7.0f, radius * 0.20f);
    juce::ColourGradient bodyGradient (juce::Colour (0xff5a5a60), centre.x - bodyRadius * 0.55f, centre.y - bodyRadius * 0.65f,
                                       juce::Colour (0xff151518), centre.x + bodyRadius * 0.55f, centre.y + bodyRadius * 0.65f, false);
    bodyGradient.addColour (0.48, juce::Colour (0xff2d2d31));
    g.setGradientFill (bodyGradient);
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));

    g.setColour (juce::Colours::black);
    g.drawEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre), 1.0f);

    const auto capRadius = bodyRadius * 0.66f;
    juce::ColourGradient capGradient (juce::Colour (0xfff0f0f2), centre.x - capRadius * 0.45f, centre.y - capRadius * 0.55f,
                                      juce::Colour (0xff76767c), centre.x + capRadius * 0.55f, centre.y + capRadius * 0.6f, true);
    capGradient.addColour (0.48, juce::Colour (0xffc4c4c9));
    capGradient.addColour (0.72, juce::Colour (0xff94949a));
    g.setGradientFill (capGradient);
    g.fillEllipse (juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre));

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawEllipse (juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre), 0.8f);

    // Pointer.
    const auto pointerAngle = startAngle + sweepAngle * value;
    const auto pointerOuter = polar (centre, capRadius - 1.0f, pointerAngle);
    const auto pointerInner = polar (centre, capRadius * 0.15f, pointerAngle);
    g.setColour (active ? theme::accentHot : juce::Colour (0xff161619));
    g.drawLine ({ pointerInner, pointerOuter }, active ? 2.0f : 1.8f);

    if (active)
    {
        g.setColour (theme::accentHot);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (pointerOuter));
    }

    auto below = area;
    const auto valueString = displayText();

    if (valueString.isNotEmpty())
    {
        g.setColour (active ? theme::paper : theme::paperDim);
        g.setFont (theme::label ());
        g.drawText (upper (labelText), below.removeFromTop (14.0f).toNearestInt(), juce::Justification::centred, false);

        g.setColour (theme::accentHot.withAlpha (active ? 1.0f : 0.88f));
        g.setFont (theme::clock (13.0f));
        g.drawText (valueString, below.toNearestInt(), juce::Justification::centred, false);
    }
    else
    {
        g.setColour (active ? theme::paper : theme::muted);
        g.setFont (theme::label ());
        g.drawText (upper (labelText), below.toNearestInt(), juce::Justification::centred, false);
    }

    if (hasKeyboardFocus (true))
        theme::drawFocusRing (g, getLocalBounds().toFloat(), theme::radius::control);
}

void Knob::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        return;

    dragging = true;
    dragStartValue = value;
    shiftFine = event.mods.isShiftDown();
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragging)
        return;

    shiftFine = event.mods.isShiftDown();
    const auto travel = shiftFine ? 620.0 : 145.0;
    commit (dragStartValue + static_cast<double> (-event.getDistanceFromDragStartY()) / travel);
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    shiftFine = false;
    repaint();
}

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    commit (defaultValue);
}

void Knob::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto step = event.mods.isShiftDown() ? 0.002 : 0.02;
    commit (value + (wheel.deltaY > 0.0f ? step : -step));
}

void Knob::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    repaint();
}

void Knob::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}

bool Knob::keyPressed (const juce::KeyPress& key)
{
    const auto step = key.getModifiers().isShiftDown() ? 0.002 : 0.02;
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
    {
        commit (value + step);
        return true;
    }

    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
    {
        commit (value - step);
        return true;
    }

    if (code == juce::KeyPress::pageUpKey) { commit (value + 0.1); return true; }
    if (code == juce::KeyPress::pageDownKey) { commit (value - 0.1); return true; }
    if (code == juce::KeyPress::homeKey) { commit (0.0); return true; }
    if (code == juce::KeyPress::endKey) { commit (1.0); return true; }

    if (code == juce::KeyPress::returnKey)
    {
        commit (defaultValue);
        return true;
    }

    return false;
}

//==============================================================================
Fader::Fader()
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setWantsKeyboardFocus (true);
}

void Fader::setValue (double newValue, bool sendNotification)
{
    const auto clamped = juce::jlimit (0.0, 1.0, newValue);

    if (std::abs (clamped - value) < 0.000001)
        return;

    value = clamped;
    repaint();

    if (sendNotification && onChange)
        onChange (value);
}

void Fader::commit (double next)
{
    setValue (next);

    if (onChange)
        onChange (value);
}

void Fader::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto active = hovering || dragging;

    const auto capWidth = juce::jmin (32.0f, area.getWidth() - 4.0f);
    const auto capHeight = 18.0f;
    const auto top = area.getY() + 9.0f;
    const auto bottom = area.getBottom() - 9.0f;
    const auto centreX = area.getCentreX();
    const auto trackTop = top;
    const auto trackBottom = bottom - capHeight * 0.5f;
    const auto trackHeight = trackBottom - trackTop;

    // Scale ticks.
    if (showTicks)
    {
        g.setColour (theme::line);

        for (int i = 0; i <= 10; ++i)
        {
            const auto y = trackTop + trackHeight * static_cast<float> (i) / 10.0f;
            const auto length = i % 5 == 0 ? 6.0f : 3.5f;
            g.drawLine (area.getX() + 1.0f, y, area.getX() + 1.0f + length, y, 1.0f);
            g.drawLine (area.getRight() - 1.0f - length, y, area.getRight() - 1.0f, y, 1.0f);
        }
    }

    // Groove.
    juce::Rectangle<float> groove (centreX - 2.5f, trackTop, 5.0f, trackHeight);
    g.setColour (theme::wellLo);
    g.fillRoundedRectangle (groove, 2.5f);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawRoundedRectangle (groove, 2.5f, 1.0f);

    const auto capCentreY = bottom - capHeight * 0.5f - static_cast<float> (value) * (trackHeight - capHeight * 0.5f);

    // Lit travel.
    if (value > 0.001)
    {
        g.setColour (accent.withAlpha (active ? 0.9f : 0.64f));
        g.fillRoundedRectangle (juce::Rectangle<float> (centreX - 1.5f, capCentreY, 3.0f, trackBottom - capCentreY), 1.5f);

        if (active)
        {
            g.setColour (accent.withAlpha (0.14f));
            g.fillRoundedRectangle (juce::Rectangle<float> (centreX - 5.0f, capCentreY, 10.0f, trackBottom - capCentreY), 5.0f);
        }
    }

    // Cap.
    juce::Rectangle<float> cap (centreX - capWidth * 0.5f, capCentreY - capHeight * 0.5f, capWidth, capHeight);

    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.fillRoundedRectangle (cap.translated (0.0f, 3.0f), 3.0f);

    juce::ColourGradient capGradient (juce::Colour (0xfff0f0f2), cap.getX(), cap.getY(),
                                      juce::Colour (0xff5f5f65), cap.getX(), cap.getBottom(), false);
    capGradient.addColour (0.38, juce::Colour (0xffb7b7bd));
    capGradient.addColour (0.55, juce::Colour (0xff8a8a91));
    g.setGradientFill (capGradient);
    g.fillRoundedRectangle (cap, 3.0f);

    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (cap, 3.0f, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawHorizontalLine (juce::roundToInt (cap.getY() + 1.0f), cap.getX() + 2.0f, cap.getRight() - 2.0f);

    g.setColour (active ? theme::accentHot : juce::Colour (0xff1b1b1e));
    g.drawHorizontalLine (juce::roundToInt (cap.getCentreY()), cap.getX() + 4.0f, cap.getRight() - 4.0f);
    g.drawHorizontalLine (juce::roundToInt (cap.getCentreY() + 1.0f), cap.getX() + 4.0f, cap.getRight() - 4.0f);

    if (active)
    {
        g.setColour (accent.withAlpha (0.22f));
        g.fillRoundedRectangle (cap.expanded (2.0f, 1.0f), 4.0f);
    }

    if (hasKeyboardFocus (true))
        theme::drawFocusRing (g, getLocalBounds().toFloat(), theme::radius::control);
}

void Fader::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    dragStartValue = value;
    repaint();
}

void Fader::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragging)
        return;

    const auto travel = event.mods.isShiftDown() ? 900.0 : 210.0;
    commit (dragStartValue + static_cast<double> (-event.getDistanceFromDragStartY()) / travel);
}

void Fader::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

void Fader::mouseDoubleClick (const juce::MouseEvent&)
{
    commit (defaultValue);
}

void Fader::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    commit (value + (wheel.deltaY > 0.0f ? (event.mods.isShiftDown() ? 0.002 : 0.02)
                                          : (event.mods.isShiftDown() ? -0.002 : -0.02)));
}

void Fader::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    repaint();
}

void Fader::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}

bool Fader::keyPressed (const juce::KeyPress& key)
{
    const auto step = key.getModifiers().isShiftDown() ? 0.002 : 0.02;
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::upKey) { commit (value + step); return true; }
    if (code == juce::KeyPress::downKey) { commit (value - step); return true; }
    if (code == juce::KeyPress::pageUpKey) { commit (value + 0.1); return true; }
    if (code == juce::KeyPress::pageDownKey) { commit (value - 0.1); return true; }

    if (code == juce::KeyPress::returnKey)
    {
        commit (defaultValue);
        return true;
    }

    return false;
}

//==============================================================================
Meter::Meter (bool isVertical, int numberOfSegments)
    : vertical (isVertical), segments (juce::jmax (4, numberOfSegments))
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void Meter::setThresholdColours (juce::Colour low, juce::Colour mid, juce::Colour high)
{
    lowColour = low;
    midColour = mid;
    highColour = high;
    repaint();
}

void Meter::timerCallback()
{
    const auto next = source ? juce::jlimit (0.0f, 1.0f, source()) : 0.0f;

    if (std::abs (next - level) > 0.002f)
        level = next;

    if (level >= peak)
    {
        peak = level;
    }
    else
    {
        peakVelocity += 0.0015f;
        peak = juce::jmax (level, peak - peakVelocity);
    }

    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto gap = 2.0f;
    const auto cellCount = static_cast<float> (segments);

    if (vertical)
    {
        const auto cellHeight = (area.getHeight() - gap * (cellCount - 1.0f)) / cellCount;
        const auto lit = juce::roundToInt (level * cellCount);

        for (int i = 0; i < segments; ++i)
        {
            const auto y = area.getBottom() - (static_cast<float> (i) + 1.0f) * cellHeight - static_cast<float> (i) * gap;
            juce::Rectangle<float> cell (area.getX(), y, area.getWidth(), cellHeight);
            const auto on = i < lit;
            const auto colour = i >= segments - 2 ? highColour : i >= segments - 5 ? midColour : lowColour;

            g.setColour (on ? colour : juce::Colour (0xff1c1c1c));
            g.fillRoundedRectangle (cell, 1.0f);

            if (! on)
            {
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.drawRoundedRectangle (cell.reduced (0.5f), 1.0f, 0.6f);
            }
        }

        if (peak > 0.01f)
        {
            const auto peakY = area.getBottom() - peak * area.getHeight();
            g.setColour (theme::paper.withAlpha (0.82f));
            g.fillRect (area.getX(), juce::jmax (area.getY(), peakY - 1.0f), area.getWidth(), 1.6f);
        }
    }
    else
    {
        const auto cellWidth = (area.getWidth() - gap * (cellCount - 1.0f)) / cellCount;
        const auto lit = juce::roundToInt (level * cellCount);

        for (int i = 0; i < segments; ++i)
        {
            juce::Rectangle<float> cell (area.getX() + static_cast<float> (i) * (cellWidth + gap), area.getY(), cellWidth, area.getHeight());
            const auto on = i < lit;
            const auto colour = i >= segments - 2 ? highColour : i >= segments - 5 ? midColour : lowColour;
            g.setColour (on ? colour : juce::Colour (0xff1c1c1c));
            g.fillRoundedRectangle (cell, 1.0f);

            if (! on)
            {
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.drawRoundedRectangle (cell.reduced (0.5f), 1.0f, 0.6f);
            }
        }

        if (peak > 0.01f)
        {
            const auto peakX = area.getX() + peak * area.getWidth();
            g.setColour (theme::paper.withAlpha (0.82f));
            g.fillRect (juce::jmin (area.getRight() - 1.5f, peakX), area.getY(), 1.6f, area.getHeight());
        }
    }
}

//==============================================================================
HardwareButton::HardwareButton (Icon buttonIcon, juce::String buttonText)
    : juce::Button (std::move (buttonText)), icon (buttonIcon)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void HardwareButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto area = getLocalBounds().toFloat();
    const auto on = getToggleState();
    const auto enabled = isEnabled();
    const auto pressed = down;
    const auto radius = theme::radius::control;

    if (pressed)
        area = area.translated (0.0f, 1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (area.translated (0.0f, 2.0f), radius);

    juce::Colour topColour = juce::Colour (0xff363636);
    juce::Colour bottomColour = juce::Colour (0xff202020);

    if (on)
    {
        topColour = danger ? juce::Colour (0xff51201a) : juce::Colour (0xff4a2a12);
        bottomColour = danger ? juce::Colour (0xff28100c) : juce::Colour (0xff251407);
    }
    else if (highlighted)
    {
        topColour = juce::Colour (0xff414141);
        bottomColour = juce::Colour (0xff282828);
    }

    if (pressed && ! on)
    {
        topColour = juce::Colour (0xff1c1c1c);
        bottomColour = juce::Colour (0xff262626);
    }

    if (! enabled)
    {
        topColour = juce::Colour (0xff1e1e1e);
        bottomColour = juce::Colour (0xff191919);
    }

    juce::ColourGradient gradient (topColour, area.getX(), area.getY(),
                                   bottomColour, area.getX(), area.getBottom(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (area, radius);

    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);

    if (on)
    {
        g.setColour ((danger ? theme::danger : accent).withAlpha (0.65f));
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.2f);
        g.setColour ((danger ? theme::danger : accent).withAlpha (0.18f));
        g.fillRoundedRectangle (area.expanded (2.0f), radius + 2.0f);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.drawHorizontalLine (juce::roundToInt (area.getY() + 1.0f), area.getX() + radius, area.getRight() - radius);
    }

    auto content = area.reduced (compact ? 6.0f : 8.0f, 3.0f);
    auto buttonText = getButtonText();
    const auto colour = ! enabled ? theme::faint
                     : on ? (danger ? theme::danger : theme::accentHot)
                          : (highlighted ? theme::paper : theme::paperDim);

    if (icon != Icon::none)
    {
        const auto size = juce::jmin (content.getHeight() - 2.0f, compact ? 14.0f : 20.0f);
        auto iconArea = juce::Rectangle<float> (size, size).withCentre (content.getCentre());

        if (buttonText.isNotEmpty())
        {
            iconArea.setX (content.getX());
            iconArea = iconArea.withSizeKeepingCentre (size, size);
            content.removeFromLeft (size + 7.0f);
        }

        drawIcon (g, iconArea, colour);
    }

    if (buttonText.isNotEmpty())
    {
        g.setColour (colour);
        g.setFont (theme::ui (compact ? 11.5f : 13.0f, true));
        g.drawText (upper (buttonText), content.toNearestInt(), icon == Icon::none ? juce::Justification::centred : juce::Justification::centredLeft, false);
    }

    if (hasKeyboardFocus (false))
        theme::drawFocusRing (g, getLocalBounds().toFloat(), radius);
}

void HardwareButton::drawIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) const
{
    const auto centre = area.getCentre();
    const auto w = area.getWidth();
    const auto h = area.getHeight();
    juce::Path path;

    g.setColour (colour);

    switch (icon)
    {
        case Icon::play:
            path.addTriangle (centre.x - w * 0.22f, centre.y - h * 0.34f,
                              centre.x - w * 0.22f, centre.y + h * 0.34f,
                              centre.x + w * 0.34f, centre.y);
            g.fillPath (path);
            break;

        case Icon::stop:
            g.fillRoundedRectangle (juce::Rectangle<float> (w * 0.62f, h * 0.62f).withCentre (centre), 1.5f);
            break;

        case Icon::record:
            g.setColour (danger ? theme::danger : colour);
            g.fillEllipse (juce::Rectangle<float> (w * 0.56f, h * 0.56f).withCentre (centre));
            g.setColour ((danger ? theme::danger : colour).withAlpha (0.25f));
            g.drawEllipse (juce::Rectangle<float> (w * 0.78f, h * 0.78f).withCentre (centre), 1.0f);
            break;

        case Icon::rewind:
        case Icon::forward:
        {
            const auto direction = icon == Icon::rewind ? -1.0f : 1.0f;
            path.addTriangle (centre.x - direction * w * 0.02f, centre.y - h * 0.30f,
                              centre.x - direction * w * 0.02f, centre.y + h * 0.30f,
                              centre.x - direction * w * 0.38f, centre.y);
            path.addTriangle (centre.x + direction * w * 0.30f, centre.y - h * 0.30f,
                              centre.x + direction * w * 0.30f, centre.y + h * 0.30f,
                              centre.x - direction * w * 0.06f, centre.y);
            g.fillPath (path);
            g.fillRect (juce::Rectangle<float> (direction > 0.0f ? area.getRight() - 2.0f : area.getX(),
                                                centre.y - h * 0.32f, 2.0f, h * 0.64f));
            break;
        }

        case Icon::loop:
        {
            const auto r = juce::Rectangle<float> (w * 0.72f, h * 0.52f).withCentre (centre);
            g.drawRoundedRectangle (r, h * 0.22f, 1.6f);
            path.addTriangle (r.getRight() - 3.0f, r.getY() - 3.0f, r.getRight() + 3.0f, r.getY() + 2.0f, r.getRight() - 3.0f, r.getY() + 7.0f);
            g.fillPath (path);
            break;
        }

        case Icon::metronome:
            path.addTriangle (centre.x, centre.y - h * 0.38f,
                              centre.x - w * 0.32f, centre.y + h * 0.34f,
                              centre.x + w * 0.32f, centre.y + h * 0.34f);
            g.strokePath (path, juce::PathStrokeType (1.6f));
            g.drawLine (centre.x, centre.y + h * 0.18f, centre.x + w * 0.18f, centre.y - h * 0.22f, 1.6f);
            break;

        case Icon::settings:
        {
            g.drawEllipse (juce::Rectangle<float> (w * 0.52f, h * 0.52f).withCentre (centre), 1.5f);

            for (int i = 0; i < 8; ++i)
            {
                const auto angle = juce::MathConstants<float>::twoPi * static_cast<float> (i) / 8.0f;
                const auto p1 = centre + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (w * 0.36f);
                const auto p2 = centre + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (w * 0.48f);
                g.drawLine ({ p1, p2 }, 1.5f);
            }

            break;
        }

        case Icon::close:
            g.drawLine (area.getX() + w * 0.22f, area.getY() + h * 0.22f, area.getRight() - w * 0.22f, area.getBottom() - h * 0.22f, 1.8f);
            g.drawLine (area.getRight() - w * 0.22f, area.getY() + h * 0.22f, area.getX() + w * 0.22f, area.getBottom() - h * 0.22f, 1.8f);
            break;

        case Icon::plus:
            g.drawLine (centre.x, area.getY() + h * 0.20f, centre.x, area.getBottom() - h * 0.20f, 1.8f);
            g.drawLine (area.getX() + w * 0.20f, centre.y, area.getRight() - w * 0.20f, centre.y, 1.8f);
            break;

        case Icon::minus:
            g.drawLine (area.getX() + w * 0.20f, centre.y, area.getRight() - w * 0.20f, centre.y, 1.8f);
            break;

        case Icon::search:
            g.drawEllipse (juce::Rectangle<float> (w * 0.54f, h * 0.54f).withCentre (centre.translated (-w * 0.05f, -h * 0.05f)), 1.6f);
            g.drawLine (centre.x + w * 0.16f, centre.y + h * 0.16f, area.getRight() - w * 0.18f, area.getBottom() - h * 0.18f, 1.6f);
            break;

        case Icon::chevronDown:
            path.startNewSubPath (area.getX() + w * 0.25f, area.getY() + h * 0.38f);
            path.lineTo (centre.x, area.getY() + h * 0.64f);
            path.lineTo (area.getRight() - w * 0.25f, area.getY() + h * 0.38f);
            g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;

        case Icon::power:
            g.drawEllipse (juce::Rectangle<float> (w * 0.62f, h * 0.62f).withCentre (centre.translated (0.0f, h * 0.04f)), 1.5f);
            g.drawLine (centre.x, area.getY() + h * 0.12f, centre.x, centre.y - h * 0.12f, 1.8f);
            break;

        case Icon::browse:
            g.drawRoundedRectangle (juce::Rectangle<float> (w * 0.66f, h * 0.54f).withCentre (centre.translated (0.0f, h * 0.08f)), 2.0f, 1.4f);
            g.drawLine (area.getX() + w * 0.17f, area.getY() + h * 0.35f, area.getX() + w * 0.38f, area.getY() + h * 0.22f, 1.4f);
            g.drawLine (area.getX() + w * 0.17f, area.getY() + h * 0.35f, area.getX() + w * 0.17f, area.getY() + h * 0.62f, 1.4f);
            break;

        case Icon::none:
        default:
            break;
    }
}

//==============================================================================
SegmentedControl::SegmentedControl()
{
    setWantsKeyboardFocus (true);
}

void SegmentedControl::setItems (juce::StringArray newItems)
{
    items = std::move (newItems);
    selected = juce::jlimit (0, juce::jmax (0, items.size() - 1), selected);
    repaint();
}

void SegmentedControl::setSelected (int index, bool sendNotification)
{
    const auto next = juce::jlimit (0, juce::jmax (0, items.size() - 1), index);

    if (next == selected)
        return;

    selected = next;
    repaint();

    if (sendNotification && onChange)
        onChange (selected);
}

void SegmentedControl::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    theme::wellFace (g, area, theme::radius::control);

    if (items.isEmpty())
        return;

    const auto inner = area.reduced (2.0f);
    const auto itemWidth = inner.getWidth() / static_cast<float> (items.size());
    const auto fontHeight = compact ? 11.5f : 12.5f;

    for (int i = 0; i < items.size(); ++i)
    {
        auto item = juce::Rectangle<float> (inner.getX() + itemWidth * static_cast<float> (i), inner.getY(),
                                            itemWidth, inner.getHeight()).reduced (1.0f, 0.0f);

        if (i == selected)
        {
            juce::ColourGradient gradient (juce::Colour (0xff4b2a12), item.getX(), item.getY(),
                                           juce::Colour (0xff261407), item.getX(), item.getBottom(), false);
            g.setGradientFill (gradient);
            g.fillRoundedRectangle (item, 2.0f);
            g.setColour (theme::accent.withAlpha (0.68f));
            g.drawRoundedRectangle (item.reduced (0.5f), 2.0f, 1.0f);
            g.setColour (theme::accent.withAlpha (0.13f));
            g.fillRoundedRectangle (item.expanded (1.0f), 3.0f);
            g.setColour (theme::accentHot);
            g.setFont (theme::label (fontHeight));
            g.drawText (upper (items[i]), item.toNearestInt(), juce::Justification::centred, false);
        }
        else
        {
            g.setColour (theme::muted);
            g.setFont (theme::label (fontHeight));
            g.drawText (upper (items[i]), item.toNearestInt(), juce::Justification::centred, false);
        }
    }

    if (hasKeyboardFocus (true))
        theme::drawFocusRing (g, area, theme::radius::control);
}

void SegmentedControl::mouseDown (const juce::MouseEvent& event)
{
    if (items.isEmpty())
        return;

    const auto index = juce::jlimit (0, items.size() - 1,
                                     static_cast<int> (static_cast<float> (event.x) / (static_cast<float> (getWidth()) / static_cast<float> (items.size()))));
    setSelected (index, true);
}

bool SegmentedControl::keyPressed (const juce::KeyPress& key)
{
    if (items.isEmpty())
        return false;

    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::upKey)
    {
        setSelected (selected - 1, true);
        return true;
    }

    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey)
    {
        setSelected (selected + 1, true);
        return true;
    }

    return false;
}

//==============================================================================
void LcdDisplay::setText (juce::String mainText, juce::String smallText, juce::String captionText)
{
    main = std::move (mainText);
    small = std::move (smallText);
    caption = std::move (captionText);
    repaint();
}

void LcdDisplay::paint (juce::Graphics& g)
{
    theme::lcd (g, getLocalBounds().toFloat(), main, small, caption, mainHeight, accent);
}

//==============================================================================
Slot::Slot (int slotIndex) : index (slotIndex)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
}

void Slot::setPluginName (juce::String pluginName)
{
    name = std::move (pluginName);
    setTitle (name.isNotEmpty() ? name : emptyText);
    setDescription (name.isNotEmpty() ? (native ? "Native insert: " + name : "Insert: " + name)
                                     : "Empty insert slot");
    setTooltip (name.isNotEmpty() ? (native ? "Native insert: " + name : "Insert: " + name)
                                  : "Click to add a plugin insert");
    repaint();
}

void Slot::setNative (bool shouldBeNative)
{
    native = shouldBeNative;
    setDescription (name.isNotEmpty() ? (native ? "Native insert: " + name : "Insert: " + name)
                                     : "Empty insert slot");
    setTooltip (name.isNotEmpty() ? (native ? "Native insert: " + name : "Insert: " + name)
                                  : "Click to add a plugin insert");
    repaint();
}

void Slot::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto filled = name.isNotEmpty();
    const auto alpha = bypassed ? 0.36f : 1.0f;

    if (hovering)
    {
        g.setColour (theme::paper.withAlpha (0.035f));
        g.fillRoundedRectangle (area.reduced (0.5f), 4.0f);
    }

    theme::wellFace (g, area, 3.0f);

    if (filled)
    {
        g.setColour (accent.withAlpha (bypassed ? 0.20f : 0.42f));
        g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (accent.withAlpha (bypassed ? 0.35f : 0.85f));
        g.fillRoundedRectangle (juce::Rectangle<float> (area.getX(), area.getY() + 2.0f, 2.0f, area.getHeight() - 4.0f), 1.0f);
    }

    auto inner = area.reduced (6.0f, 1.0f);
    const auto ledCentre = juce::Point<float> (inner.getX() + 3.0f, inner.getCentreY());
    theme::led (g, ledCentre, 3.0f, filled ? accent : theme::faint, filled && ! bypassed, filled);

    inner.removeFromLeft (12.0f);

    // The name owns the card. The old layout reserved a 34 px strip for it on
    // the right and painted FX over the remaining width, so every long plug-in
    // name collapsed to something like "FXOVE...". Compact console slots
    // fall back to a short but recognisable name when the full one will not fit;
    // the full name remains in the tooltip and accessibility title.
    auto statusArea = inner.removeFromRight (compact ? 26.0f : 30.0f);
    auto label = filled ? name : emptyText;

    g.setColour (filled ? theme::paper.withMultipliedAlpha (alpha) : theme::faint);
    auto labelFont = theme::ui (compact ? 12.5f : 13.0f, filled);
    g.setFont (labelFont);

    if (compact && juce::GlyphArrangement::getStringWidth (labelFont, label) > inner.getWidth())
    {
        if (filled)
            label = shortInsertName (name);

        if (juce::GlyphArrangement::getStringWidth (labelFont, label) > inner.getWidth())
        {
            labelFont = theme::ui (11.0f, filled);
            g.setFont (labelFont);
        }
    }

    g.drawText (label, inner.toNearestInt(), juce::Justification::centredLeft, true);

    if (filled)
    {
        const auto status = bypassed ? juce::String ("BYP")
                                     : native ? juce::String (compact ? "NAT" : "NATIVE")
                                              : juce::String ("FX");
        g.setColour (accent.withMultipliedAlpha (bypassed ? 0.55f : 0.95f));
        g.setFont (theme::label (10.0f));
        g.drawText (status, statusArea.toNearestInt(), juce::Justification::centredRight, false);
    }

    if (hasKeyboardFocus (true))
        theme::drawFocusRing (g, area, 3.0f);
}

void Slot::mouseDown (const juce::MouseEvent& event)
{
    bypassPress = false;

    if (event.x < 14 && onBypass)
    {
        bypassPress = true;
        bypassed = ! bypassed;
        repaint();
        onBypass();
    }
}

void Slot::mouseUp (const juce::MouseEvent& event)
{
    // Open the browser or rack on release. Launching a CallOutBox from
    // mouseDown let the matching mouseUp dismiss it immediately.
    if (bypassPress)
    {
        bypassPress = false;
        return;
    }

    if (event.mouseWasDraggedSinceMouseDown())
        return;

    if (onClick)
        onClick();
}

void Slot::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    repaint();
}

void Slot::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}

bool Slot::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey)
    {
        if (onClick)
            onClick();

        return true;
    }

    return false;
}

//==============================================================================
void paintWaveform (juce::Graphics& g,
                    juce::Rectangle<float> area,
                    juce::uint32 seed,
                    juce::Colour colour,
                    float opacity,
                    bool mirrored)
{
    if (area.getWidth() < 3.0f || area.getHeight() < 3.0f)
        return;

    juce::Random random (static_cast<int> (seed));
    const auto centreY = area.getCentreY();
    const auto maxHeight = area.getHeight() * 0.46f;
    auto amplitude = 0.2f;
    auto target = 0.2f;

    g.setColour (colour.withAlpha (opacity));

    for (float x = area.getX() + 1.0f; x < area.getRight() - 1.0f; x += 2.0f)
    {
        if (static_cast<int> (x - area.getX()) % 14 == 0)
            target = 0.12f + 0.88f * random.nextFloat();

        amplitude += (target - amplitude) * 0.16f;
        const auto wobble = 0.62f + 0.38f * std::sin ((x - area.getX()) * 0.11f + static_cast<float> (seed % 31));
        const auto height = juce::jmax (0.6f, amplitude * wobble * maxHeight);

        if (mirrored)
            g.fillRect (x, centreY - height, 1.0f, height * 2.0f);
        else
            g.fillRect (x, centreY - height * 0.45f, 1.0f, height);
    }

    g.setColour (colour.withAlpha (opacity * 0.45f));
    g.drawHorizontalLine (juce::roundToInt (centreY), area.getX(), area.getRight());
}

void paintMonogram (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto centre = area.getCentre();
    const auto radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;

    g.setColour (colour.withAlpha (0.18f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    g.setColour (colour);
    g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre).reduced (0.8f), 1.4f);

    g.setColour (colour.withAlpha (0.55f));
    g.drawEllipse (juce::Rectangle<float> (radius * 1.45f, radius * 1.45f).withCentre (centre), 0.8f);
    g.setColour (colour.withAlpha (0.35f));
    g.drawEllipse (juce::Rectangle<float> (radius * 0.9f, radius * 0.9f).withCentre (centre), 0.8f);

    const auto barWidth = radius * 0.12f;
    const auto gap = radius * 0.09f;
    const float heights[] { 0.42f, 0.68f, 1.0f, 0.62f, 0.82f, 0.38f };
    auto x = centre.x - (barWidth * 6.0f + gap * 5.0f) * 0.5f;

    for (const auto h : heights)
    {
        const auto barHeight = radius * h;
        g.setColour (colour.withAlpha (0.92f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x, centre.y - barHeight * 0.5f, barWidth, barHeight), barWidth * 0.5f);
        x += barWidth + gap;
    }

    g.setColour (colour.withAlpha (0.65f));
    g.drawLine (centre.x, area.getY() + radius * 0.15f, centre.x, area.getY() + radius * 0.42f, 1.0f);
}
} // namespace nc
