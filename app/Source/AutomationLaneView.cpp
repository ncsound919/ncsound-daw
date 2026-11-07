#include "AutomationLaneView.h"

#include "Theme.h"

namespace nc
{
namespace
{
// Fixed 64 beats ("16 bars") visible. Zoom/pan of the lane is future work;
// the barToX mapping in ArrangeView is the pattern to follow when it lands.
constexpr double visibleBeats = 64.0;
} // namespace

AutomationLaneView::AutomationLaneView (AudioEngine& engineToUse, int trackToEdit)
    : DocumentWindow ("Automation", juce::Colours::darkgrey, DocumentWindow::allButtons, true)
    , engine (engineToUse)
    , track (juce::jlimit (0, AudioEngine::trackCount - 1, trackToEdit))
    , lane (engine.trackAutomation (track, param))
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);

    paramBox.addItem ("Volume", 1);
    paramBox.addItem ("Pan", 2);
    paramBox.setSelectedId (1, juce::dontSendNotification);
    paramBox.onChange = [this]
    {
        param = paramBox.getSelectedId() == 2 ? AutomationParam::Pan : AutomationParam::Volume;
        lane = engine.trackAutomation (track, param);
        undoStack.clear();
        refreshHeader();
        surface.repaint();
    };

    curveBox.addItem ("Linear", 1);
    curveBox.addItem ("Step", 2);
    curveBox.addItem ("Smooth", 3);
    curveBox.setSelectedId (1, juce::dontSendNotification);
    curveBox.onChange = [this]
    {
        if (lane.numPoints() == 0)
            return;

        pushUndo();

        const auto selected = lane.pointAt (lane.numPoints() - 1);
        const auto curve = curveBox.getSelectedId() == 2 ? AutomationLane::Curve::step
                           : curveBox.getSelectedId() == 3 ? AutomationLane::Curve::smooth
                                                           : AutomationLane::Curve::linear;
        lane.addPoint (selected.beats, selected.value, curve);
        publish();
    };

    clearButton.onClick = [this]
    {
        pushUndo();
        lane.clear();
        publish();
    };

    undoButton.onClick = [this]
    {
        if (undoStack.empty())
            return;

        lane = undoStack.back();
        undoStack.pop_back();
        publish();
    };

    headerLabel.setColour (juce::Label::textColourId, theme::paper);

    refreshHeader();
    setContentOwned (new juce::Component(), false);

    auto* content = getContentComponent();
    content->addAndMakeVisible (&surface);
    content->addAndMakeVisible (&paramBox);
    content->addAndMakeVisible (&curveBox);
    content->addAndMakeVisible (&clearButton);
    content->addAndMakeVisible (&undoButton);
    content->addAndMakeVisible (&headerLabel);
    setSize (640, 420);
    setVisible (true);
}

void AutomationLaneView::resized()
{
    auto area = getContentComponent()->getLocalBounds().reduced (12);

    auto header = area.removeFromTop (28);
    headerLabel.setBounds (header.removeFromLeft (300));
    header.removeFromLeft (8);
    paramBox.setBounds (header.removeFromLeft (100));
    header.removeFromLeft (8);
    curveBox.setBounds (header.removeFromLeft (100));
    header.removeFromLeft (8);
    clearButton.setBounds (header.removeFromLeft (70));
    header.removeFromLeft (8);
    undoButton.setBounds (header.removeFromLeft (70));

    area.removeFromTop (8);
    surface.setBounds (area);
}

void AutomationLaneView::closeButtonPressed()
{
    delete this;
}

void AutomationLaneView::refreshHeader()
{
    headerLabel.setText ("TRACK " + juce::String (track + 1) + " / "
                         + (param == AutomationParam::Volume ? "VOLUME" : "PAN") + "  /  "
                         + juce::String (lane.numPoints()) + " PTS",
                         juce::dontSendNotification);
}

AutomationLaneView::Surface::Surface (AutomationLaneView& ownerToUse)
    : owner (ownerToUse)
{
    setWantsKeyboardFocus (false);
}

void AutomationLaneView::pushUndo()
{
    undoStack.push_back (lane);

    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
}

void AutomationLaneView::publish()
{
    engine.setTrackAutomation (track, param, lane);
    refreshHeader();
    surface.repaint();
}

double AutomationLaneView::Surface::xToBeats (float x) const
{
    const auto width = static_cast<double> (getWidth());

    if (width <= 0.0)
        return 0.0;

    return juce::jlimit (0.0, visibleBeats, static_cast<double> (x) / width * visibleBeats);
}

float AutomationLaneView::Surface::beatsToX (double beats) const
{
    return static_cast<float> (beats / visibleBeats * static_cast<double> (getWidth()));
}

float AutomationLaneView::Surface::valueToY (float value) const
{
    // Volume 0..1 top-to-bottom; pan -1..1 top-to-bottom.
    const auto normalized = owner.param == AutomationParam::Volume
                                ? 1.0f - juce::jlimit (0.0f, 1.0f, value)
                                : 0.5f - juce::jlimit (-1.0f, 1.0f, value) * 0.5f;
    return normalized * static_cast<float> (getHeight());
}

float AutomationLaneView::Surface::yToValue (float y) const
{
    const auto normalized = getHeight() > 0 ? 1.0f - y / static_cast<float> (getHeight()) : 1.0f;

    return owner.param == AutomationParam::Volume ? juce::jlimit (0.0f, 1.0f, normalized)
                                                  : juce::jlimit (-1.0f, 1.0f, (0.5f - normalized) * 2.0f);
}

int AutomationLaneView::Surface::pointAt (juce::Point<float> point) const
{
    for (int i = 0; i < owner.lane.numPoints(); ++i)
    {
        const auto p = owner.lane.pointAt (i);
        const auto centre = juce::Point<float> (beatsToX (p.beats), valueToY (p.value));

        if (centre.getDistanceFrom (point) < 9.0f)
            return i;
    }

    return -1;
}

void AutomationLaneView::Surface::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::darkgrey.darker (0.7f));
    g.setColour (juce::Colours::white.withAlpha (0.08f));

    for (int bar = 0; bar <= 16; ++bar)
        g.drawVerticalLine (juce::roundToInt (beatsToX (bar * 4.0)), 0.0f, static_cast<float> (getHeight()));

    if (owner.lane.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (14.0f);
        g.drawText ("Click to add a point", getLocalBounds().toFloat(), juce::Justification::centred, false);
        return;
    }

    // Step sampling of the curve so any curve type draws honestly.
    g.setColour (theme::accent);
    const auto steps = juce::jmax (1, getWidth());

    for (int i = 1; i <= steps; ++i)
    {
        const auto b0 = static_cast<double> (i - 1) / steps * visibleBeats;
        const auto b1 = static_cast<double> (i) / steps * visibleBeats;
        g.drawLine (beatsToX (b0), valueToY (owner.lane.valueAt (b0)),
                    beatsToX (b1), valueToY (owner.lane.valueAt (b1)), 2.0f);
    }

    for (int i = 0; i < owner.lane.numPoints(); ++i)
    {
        const auto p = owner.lane.pointAt (i);
        g.setColour (juce::Colours::white);
        g.drawEllipse (beatsToX (p.beats) - 5.0f, valueToY (p.value) - 5.0f, 10.0f, 10.0f, 2.0f);
    }
}

void AutomationLaneView::Surface::mouseDown (const juce::MouseEvent& event)
{
    const auto position = event.getPosition().toFloat();

    if (event.mods.isAltDown())
    {
        if (const auto index = pointAt (position); index >= 0)
        {
            owner.pushUndo();
            owner.lane.removePoint (index);
            owner.publish();
        }

        return;
    }

    const auto existing = pointAt (position);

    if (existing >= 0)
    {
        draggingPoint = existing;
        dragUndoPushed = false;
        return;
    }

    owner.pushUndo();
    owner.lane.addPoint (xToBeats (position.x), yToValue (position.y));
    owner.publish();
}

void AutomationLaneView::Surface::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingPoint < 0 || draggingPoint >= owner.lane.numPoints())
        return;

    const auto position = event.getPosition().toFloat();

    if (! dragUndoPushed)
    {
        owner.pushUndo();
        dragUndoPushed = true;
    }

    const auto current = owner.lane.pointAt (draggingPoint);
    owner.lane.removePoint (draggingPoint);
    owner.lane.addPoint (xToBeats (position.x), yToValue (position.y), current.curve);
    draggingPoint = pointAt (position);

    if (draggingPoint < 0)
        draggingPoint = 0;

    owner.publish();
}

void AutomationLaneView::Surface::mouseUp (const juce::MouseEvent&)
{
    draggingPoint = -1;
    dragUndoPushed = false;
}
} // namespace nc
