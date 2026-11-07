#include "PianoRoll.h"

namespace nc
{
PianoRoll::PianoRoll (juce::String clipNameToUse, double lengthBars,
                      std::vector<nc::engine::MidiNote> initialNotes,
                      std::function<void (const std::vector<nc::engine::MidiNote>&)> onEditedToUse)
    : title (clipNameToUse), onEdited (std::move (onEditedToUse))
{
    lengthBeats = nc::engine::clipLengthBeats (juce::jmax (1.0, lengthBars));
    edited = std::move (initialNotes);
    nc::engine::sanitizeNotes (edited, lengthBeats);

    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    setSize (760, 420);
}

PianoRoll::~PianoRoll() = default;

juce::Point<float> PianoRoll::beatPitchToXY (double beat, int pitch) const
{
    const auto area = gridArea();
    const auto x = area.getX() + area.getWidth() * static_cast<float> (juce::jlimit (0.0, lengthBeats, beat) / juce::jmax (0.001, lengthBeats));
    const auto norm = static_cast<float> (juce::jlimit (0, 127, pitch)) / 127.0f;
    const auto y = area.getBottom() - norm * area.getHeight();
    return { x, y };
}

void PianoRoll::xyToBeatPitch (juce::Point<float> point, double& beat, int& pitch) const
{
    const auto area = gridArea();
    const auto clamped = point;
    beat = juce::jlimit (0.0, lengthBeats, static_cast<double> ((clamped.x - area.getX()) / juce::jmax (1.0f, area.getWidth())) * lengthBeats);
    beat = nc::engine::quantizeBeat (beat);
    const auto norm = juce::jlimit (0.0f, 1.0f, (area.getBottom() - clamped.y) / juce::jmax (1.0f, area.getHeight()));
    pitch = juce::jlimit (0, 127, static_cast<int> (std::lround (norm * 127.0f)));
}

int PianoRoll::noteAt (juce::Point<int> point) const
{
    const auto area = gridArea();
    const auto rowH = area.getHeight() / 128.0f;

    for (int i = static_cast<int> (edited.size()) - 1; i >= 0; --i)
    {
        const auto& note = edited[static_cast<size_t> (i)];
        const auto tl = beatPitchToXY (note.startBeat, note.pitch);
        const auto tr = beatPitchToXY (note.startBeat + note.lengthBeats, note.pitch);
        const auto rect = juce::Rectangle<float> (tl.x, tl.y - rowH * 0.5f,
                                                  juce::jmax (4.0f, tr.x - tl.x), rowH);
        if (rect.contains (point.toFloat()))
            return i;
    }

    return -1;
}

bool PianoRoll::nearRightEdge (const nc::engine::MidiNote& note, juce::Point<int> point) const
{
    const auto edge = beatPitchToXY (note.startBeat + note.lengthBeats, note.pitch);
    return std::abs (point.x - edge.x) < 7.0f && std::abs (point.y - edge.y) < gridArea().getHeight() / 128.0f * 2.0f;
}

void PianoRoll::publish()
{
    nc::engine::sanitizeNotes (edited, lengthBeats);

    if (onEdited)
        onEdited (edited);

    repaint();
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff121110));
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText (title + "  /  " + juce::String (edited.size()) + " NOTES  /  CLICK ADD / DRAG MOVE / EDGE RESIZE / RIGHT-CLICK DELETE",
                getLocalBounds().withHeight (44).reduced (14, 10), juce::Justification::centredLeft, false);

    const auto area = gridArea();
    g.setColour (juce::Colour (0xff0a0908));
    g.fillRect (area.toNearestInt());
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRect (area.toNearestInt());

    // Pitch rows.
    for (int pitch = 0; pitch <= 127; ++pitch)
    {
        const auto p = beatPitchToXY (0.0, pitch);
        const auto y = juce::roundToInt (p.y);

        if (nc::engine::isBlackPitch (pitch))
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<int> (static_cast<int> (area.getX()), y - 1,
                                              static_cast<int> (area.getWidth()), 2));
        }

        if (pitch % 12 == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText (nc::engine::midiNoteName (pitch), static_cast<int> (area.getX()) + 3, y - 5, 40, 10,
                        juce::Justification::centredLeft, false);
        }
    }

    // Time grid: bar lines strong, beat lines medium, 16ths faint.
    for (int step = 0; step <= static_cast<int> (lengthBeats * 4.0); ++step)
    {
        const auto beat = static_cast<double> (step) * 0.25;
        const auto x = beatPitchToXY (beat, 0).x;
        const auto isBar = (step % 16 == 0);
        const auto isBeat = (step % 4 == 0);
        g.setColour (juce::Colours::white.withAlpha (isBar ? 0.22f : (isBeat ? 0.12f : 0.05f)));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    }

    const auto rowH = area.getHeight() / 128.0f;

    for (int i = 0; i < static_cast<int> (edited.size()); ++i)
    {
        const auto& note = edited[static_cast<size_t> (i)];
        const auto tl = beatPitchToXY (note.startBeat, note.pitch);
        const auto tr = beatPitchToXY (note.startBeat + note.lengthBeats, note.pitch);
        const auto selectedNote = i == selected;
        g.setColour (selectedNote ? juce::Colour (0xffe07a1a) : juce::Colour (0xff4aa3ff));
        g.fillRoundedRectangle (juce::Rectangle<float> (tl.x, tl.y - rowH * 0.5f,
                                                         juce::jmax (4.0f, tr.x - tl.x), rowH),
                                2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (nc::engine::midiNoteName (note.pitch),
                    static_cast<int> (tl.x) + 2, static_cast<int> (tl.y - rowH * 0.5f), 40, static_cast<int> (rowH),
                    juce::Justification::centredLeft, false);
    }
}

void PianoRoll::resized()
{
}

void PianoRoll::mouseDown (const juce::MouseEvent& event)
{
    const auto local = event.getPosition();

    if (event.mods.isRightButtonDown())
    {
        const auto index = noteAt (local);

        if (index >= 0)
        {
            edited.erase (edited.begin() + index);
            selected = -1;
            publish();
        }

        return;
    }

    const auto index = noteAt (local);

    if (index >= 0)
    {
        selected = index;
        const auto& note = edited[static_cast<size_t> (index)];
        dragMode = nearRightEdge (note, local) ? 2 : 1;
        dragOriginBeat = note.startBeat;
        dragOriginPitch = note.pitch;
        dragOriginLength = note.lengthBeats;
        dragStartBeat = local.x;
        repaint();
        return;
    }

    // Add a note at the click.
    double beat = 0.0;
    int pitch = 60;
    xyToBeatPitch (local.toFloat(), beat, pitch);
    nc::engine::MidiNote note;
    note.startBeat = beat;
    note.lengthBeats = 0.25;
    note.pitch = pitch;
    note.velocity = 100;
    edited.push_back (note);
    selected = static_cast<int> (edited.size()) - 1;
    publish();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& event)
{
    if (selected < 0 || selected >= static_cast<int> (edited.size()) || dragMode == 0)
        return;

    auto& note = edited[static_cast<size_t> (selected)];
    const auto area = gridArea();
    const auto dxBeats = (event.getPosition().x - dragStartBeat) / juce::jmax (1.0f, area.getWidth()) * lengthBeats;

    if (dragMode == 1)
    {
        double ignoredBeat = 0.0;
        int ignoredPitch = 60;
        xyToBeatPitch (event.getPosition().toFloat(), ignoredBeat, ignoredPitch);
        note.startBeat = juce::jmax (0.0, nc::engine::quantizeBeat (dragOriginBeat + dxBeats));
        note.pitch = ignoredPitch;
    }
    else
    {
        note.lengthBeats = juce::jmax (nc::engine::minMidiNoteBeats,
                                       nc::engine::quantizeBeat (dragOriginLength + dxBeats));
    }

    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    if (dragMode != 0 && selected >= 0)
        publish();

    dragMode = 0;
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if ((key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
        && selected >= 0 && selected < static_cast<int> (edited.size()))
    {
        edited.erase (edited.begin() + selected);
        selected = -1;
        publish();
        return true;
    }

    return false;
}

bool PianoRoll::keyStateChanged (bool)
{
    return false;
}
} // namespace nc
