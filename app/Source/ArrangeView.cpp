#include "ArrangeView.h"
#include "PianoRoll.h"

#include <nc/engine/MediaCollect.hpp>

#include <map>

#include <cmath>

namespace nc
{
namespace
{
constexpr int toolbarHeight = 54;
constexpr int rulerHeight = 50;
const juce::StringArray toolNames { "SELECT", "TRIM", "FADE", "GAIN", "SPLIT", "ERASE" };
const juce::StringArray functionNames { "SPLIT", "DUP", "DEL" };

juce::Colour tintForTrack (int track)
{
    const auto clamped = nc::limits::clampTrack (track, AudioEngine::trackCount);
    return theme::trackTints[static_cast<size_t> (clamped % 8)];
}

juce::String clipKindName (int kind)
{
    return kind == 0 ? "MIDI" : "AUDIO";
}

void drawSmallButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text,
                      bool on, juce::Colour colour)
{
    const auto r = area.toFloat();

    g.setColour (on ? colour.withAlpha (0.92f) : theme::panelLo);
    g.fillRoundedRectangle (r, theme::radius::tight);
    g.setColour (on ? colour.brighter (0.25f) : theme::lineStrong);
    g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::tight, 1.0f);

    if (! on)
    {
        g.setColour (juce::Colours::white.withAlpha (0.055f));
        g.drawHorizontalLine (area.getY() + 1, r.getX() + 2.0f, r.getRight() - 2.0f);
    }

    g.setColour (on ? theme::well : theme::paperDim);
    g.setFont (theme::label ());
    g.drawText (text, area, juce::Justification::centred, false);
}

void drawToolIcon (juce::Graphics& g, juce::Rectangle<float> area, int toolIndex, juce::Colour colour)
{
    const auto x = area.getX();
    const auto y = area.getY();
    const auto w = area.getWidth();
    const auto h = area.getHeight();

    g.setColour (colour);
    juce::Path path;

    switch (toolIndex)
    {
        case 0: // pointer
            path.startNewSubPath (x + w * 0.28f, y + h * 0.14f);
            path.lineTo (x + w * 0.28f, y + h * 0.80f);
            path.lineTo (x + w * 0.45f, y + h * 0.62f);
            path.lineTo (x + w * 0.57f, y + h * 0.90f);
            path.lineTo (x + w * 0.69f, y + h * 0.82f);
            path.lineTo (x + w * 0.57f, y + h * 0.57f);
            path.lineTo (x + w * 0.78f, y + h * 0.51f);
            path.closeSubPath();
            g.fillPath (path);
            break;

        case 1: // trim
        {
            g.drawLine (x + w * 0.30f, y + h * 0.16f, x + w * 0.30f, y + h * 0.84f, 1.7f);
            g.drawLine (x + w * 0.70f, y + h * 0.16f, x + w * 0.70f, y + h * 0.84f, 1.7f);
            path.addTriangle (x + w * 0.30f, y + h * 0.50f, x + w * 0.12f, y + h * 0.32f, x + w * 0.12f, y + h * 0.68f);
            path.addTriangle (x + w * 0.70f, y + h * 0.50f, x + w * 0.88f, y + h * 0.32f, x + w * 0.88f, y + h * 0.68f);
            g.fillPath (path);
            break;
        }

        case 2: // fade curve
        {
            path.startNewSubPath (x + w * 0.14f, y + h * 0.82f);
            path.cubicTo (x + w * 0.40f, y + h * 0.82f, x + w * 0.56f, y + h * 0.18f, x + w * 0.86f, y + h * 0.18f);
            g.strokePath (path, juce::PathStrokeType (1.8f));
            break;
        }

        case 3: // clip gain
        {
            g.drawLine (x + w * 0.14f, y + h * 0.60f, x + w * 0.86f, y + h * 0.60f, 1.6f);
            g.fillEllipse (juce::Rectangle<float> (w * 0.20f, w * 0.20f).withCentre ({ x + w * 0.5f, y + h * 0.60f }));
            g.drawLine (x + w * 0.5f, y + h * 0.16f, x + w * 0.5f, y + h * 0.36f, 1.3f);
            g.drawLine (x + w * 0.5f, y + h * 0.78f, x + w * 0.5f, y + h * 0.92f, 1.3f);
            break;
        }

        case 4: // split
            g.drawLine (x + w * 0.5f, y + h * 0.12f, x + w * 0.5f, y + h * 0.88f, 1.7f);
            path.addTriangle (x + w * 0.5f, y + h * 0.18f, x + w * 0.20f, y + h * 0.48f, x + w * 0.50f, y + h * 0.48f);
            path.addTriangle (x + w * 0.5f, y + h * 0.82f, x + w * 0.80f, y + h * 0.52f, x + w * 0.50f, y + h * 0.52f);
            g.fillPath (path);
            break;

        case 5: // eraser
        default:
            g.saveState();
            g.addTransform (juce::AffineTransform::rotation (-0.48f, area.getCentreX(), area.getCentreY()));
            g.fillRoundedRectangle (juce::Rectangle<float> (w * 0.30f, h * 0.20f, w * 0.46f, h * 0.58f), 2.0f);
            g.setColour (theme::well.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (w * 0.30f, h * 0.20f, w * 0.46f, h * 0.13f));
            g.restoreState();
            break;
    }
}
} // namespace

ArrangeView::ArrangeView (AudioEngine& engineToUse)
    : engine (engineToUse), inspector (engineToUse)
{
    addAndMakeVisible (inspector);

    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());

    for (const auto& source : sessionClips())
    {
        Clip clip;
        clip.track = source.track;
        clip.startBar = source.startBar;
        clip.lengthBars = source.lengthBars;
        clip.name = source.name;
        clip.kind = source.kind;
        clip.seed = source.seed;
        clip.fadeInBars = 0.25f;
        clip.fadeOutBars = 0.25f;
        clip.assetId = clip.kind == 0 ? -1 : engine.demoAssetId (source.track);
        clip.sourceOffsetSeconds = static_cast<double> (source.startBar - 1) * secondsPerBar;

        if (clip.kind == 0 && clip.notes.empty())
            clip.notes = nc::engine::demoMidiNotes (clip.seed, static_cast<double> (clip.lengthBars), clip.track);

        clips.push_back (std::move (clip));
    }

    inspector.onInsertClicked = [this] (int slot)
    {
        if (onInsertClicked)
            onInsertClicked (selectedTrack, slot);
    };
    inspector.onStripClicked = [this] (int track)
    {
        if (onStripClicked)
            onStripClicked (track);
    };

    setSelectedTrack (0);
    setTool (0);
    engine.setLoopBars (loopStartBar, loopEndBar);
    publishTimeline();
}

int ArrangeView::shownTracks() const
{
    return juce::jlimit (1, AudioEngine::trackCount, engine.getActiveTrackCount());
}

int ArrangeView::visibleTrackRows() const
{
    const auto shown = shownTracks();
    const auto height = laneArea.getHeight();

    if (height <= 0)
        return 1;

    return juce::jmin (shown, juce::jmax (1, height / minLaneHeight));
}

void ArrangeView::ensureTrackVisible (int track)
{
    const auto shown = shownTracks();

    if (track < 0 || track >= shown)
        return;

    const auto rows = visibleTrackRows();
    const auto last = juce::jmax (0, shown - rows);

    firstVisibleTrack = juce::jlimit (0, last, firstVisibleTrack);

    if (track < firstVisibleTrack)
        firstVisibleTrack = track;
    else if (track >= firstVisibleTrack + rows)
        firstVisibleTrack = track - rows + 1;

    firstVisibleTrack = juce::jlimit (0, last, firstVisibleTrack);
}

void ArrangeView::setSelectedTrack (int track)
{
    selectedTrack = juce::jlimit (0, shownTracks() - 1, track);
    ensureTrackVisible (selectedTrack);
    inspector.setTrack (selectedTrack);
    repaint();
}

void ArrangeView::setTool (int toolIndex)
{
    tool = juce::jlimit (0, 5, toolIndex);
    setMouseCursor (tool == 0 ? juce::MouseCursor::NormalCursor : juce::MouseCursor::CrosshairCursor);
    repaint (toolbarArea);
}

void ArrangeView::zoomIn()
{
    visibleBars = juce::jmax (4, juce::roundToInt (static_cast<float> (visibleBars) * 0.75f));
    repaint();
}

void ArrangeView::zoomOut()
{
    visibleBars = juce::jmin (128, juce::roundToInt (static_cast<float> (visibleBars) * 1.333f));
    repaint();
}

void ArrangeView::refresh()
{
    inspector.syncFromEngine();

    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());
    const auto bar = 1.0 + engine.positionSeconds() / secondsPerBar;
    const auto x = static_cast<float> (barToX (bar));

    if (std::abs (x - lastPlayheadX) > 0.15f)
    {
        if (lastPlayheadX > 0.0f)
        {
            auto old = juce::Rectangle<int> (juce::roundToInt (lastPlayheadX) - 12,
                                             juce::jmax (0, loopArea.getY()),
                                             24, laneArea.getBottom() - juce::jmax (0, loopArea.getY()));
            repaint (old.getIntersection (getLocalBounds()));
        }

        lastPlayheadX = x;

        auto now = juce::Rectangle<int> (juce::roundToInt (x) - 12,
                                         juce::jmax (0, loopArea.getY()),
                                         24, laneArea.getBottom() - juce::jmax (0, loopArea.getY()));
        repaint (now.getIntersection (getLocalBounds()));
    }

    // Mini meters only need repainting while they can move.
    const auto playing = engine.isPlaying();

    if (playing || wasPlaying)
        repaint (trackHeaderColumn);

    wasPlaying = playing;
}

//==============================================================================
void ArrangeView::layoutAreas()
{
    auto area = getLocalBounds();

    toolbarArea = area.removeFromTop (toolbarHeight);

    // A mixer-width selected channel leaves the timeline the room it needs.
    const auto inspectorWidth = juce::jlimit (212, 232, area.getWidth() / 6);
    inspectorArea = area.removeFromRight (inspectorWidth);
    area.removeFromRight (6);

    const auto headerWidth = juce::jlimit (196, 246, area.getWidth() / 5);
    trackHeaderColumn = area.removeFromLeft (headerWidth);
    timelineArea = area;

    tracksHeaderArea = trackHeaderColumn.removeFromTop (rulerHeight);
    rulerArea = timelineArea.removeFromTop (rulerHeight);
    loopArea = rulerArea.removeFromTop (12);
    laneArea = timelineArea;

    auto tools = toolbarArea.reduced (18, 10);

    for (auto& rect : toolRects)
    {
        rect = tools.removeFromLeft (34);
        tools.removeFromLeft (6);
    }

    tools.removeFromLeft (8);

    for (auto& rect : functionRects)
    {
        rect = tools.removeFromLeft (46);
        tools.removeFromLeft (4);
    }

    tools.removeFromLeft (10);
    toolNameArea = tools.removeFromLeft (104);
    tools.removeFromLeft (10);
    snapArea = tools.removeFromLeft (104);
    tools.removeFromLeft (8);
    gridArea = tools.removeFromLeft (96);

    auto right = toolbarArea.reduced (18, 10);
    zoomInArea = right.removeFromRight (34);
    right.removeFromRight (6);
    zoomOutArea = right.removeFromRight (34);
    right.removeFromRight (10);
    barsReadoutArea = right.removeFromRight (96);

    inspector.setBounds (inspectorArea);
}

void ArrangeView::resized()
{
    layoutAreas();
    lastPlayheadX = -1.0f;
    repaint();
}

double ArrangeView::barToX (double bar) const
{
    if (laneArea.getWidth() <= 0 || visibleBars <= 0)
        return static_cast<double> (laneArea.getX());

    return static_cast<double> (laneArea.getX())
         + (bar - viewStartBar) / static_cast<double> (visibleBars) * static_cast<double> (laneArea.getWidth());
}

double ArrangeView::xToBar (double x) const
{
    if (laneArea.getWidth() <= 0 || visibleBars <= 0)
        return viewStartBar;

    return viewStartBar + (x - static_cast<double> (laneArea.getX()))
         / static_cast<double> (laneArea.getWidth()) * static_cast<double> (visibleBars);
}

juce::Rectangle<int> ArrangeView::laneBounds (int trackIndex) const
{
    const auto shown = shownTracks();

    if (trackIndex < 0 || trackIndex >= shown)
        return {};

    const auto rows = visibleTrackRows();
    const auto first = juce::jlimit (0, juce::jmax (0, shown - rows), firstVisibleTrack);
    const auto offset = trackIndex - first;

    if (offset < 0 || offset >= rows)
        return {};

    const auto laneHeight = laneArea.getHeight() / rows;
    const auto y = laneArea.getY() + offset * laneHeight;
    const auto height = offset == rows - 1 ? laneArea.getBottom() - y : laneHeight;

    return { laneArea.getX(), y, laneArea.getWidth(), height };
}

int ArrangeView::trackAt (juce::Point<int> position) const
{
    if (position.y < laneArea.getY() || position.y >= laneArea.getBottom())
        return -1;

    if (position.x >= inspectorArea.getX())
        return -1;

    const auto rows = visibleTrackRows();
    const auto shown = shownTracks();
    const auto first = juce::jlimit (0, juce::jmax (0, shown - rows), firstVisibleTrack);
    const auto laneHeight = juce::jmax (1, laneArea.getHeight() / rows);

    return juce::jlimit (0, shown - 1, first + (position.y - laneArea.getY()) / laneHeight);
}

juce::Rectangle<int> ArrangeView::headerControl (int trackIndex, int control) const
{
    const auto lane = laneBounds (trackIndex);
    const auto header = juce::Rectangle<int> (trackHeaderColumn.getX(), lane.getY(),
                                              trackHeaderColumn.getWidth(), lane.getHeight());
    auto row = header.reduced (12, 6).removeFromBottom (28);

    // 28 x 28 minimum: below this the targets stop being reliably clickable.
    const auto buttonWidth = 28;
    juce::Rectangle<int> record (row.getRight() - buttonWidth, row.getY(), buttonWidth, row.getHeight());
    juce::Rectangle<int> solo (record.getX() - buttonWidth - 6, row.getY(), buttonWidth, row.getHeight());
    juce::Rectangle<int> mute (solo.getX() - buttonWidth - 6, row.getY(), buttonWidth, row.getHeight());

    switch (control)
    {
        case 1: return mute;
        case 2: return solo;
        case 3: return record;
        default: return {};
    }
}

juce::Rectangle<float> ArrangeView::clipBounds (const Clip& clip) const
{
    const auto lane = laneBounds (clip.track).toFloat();
    const auto x1 = static_cast<float> (barToX (clip.startBar));
    const auto x2 = static_cast<float> (barToX (clip.startBar + clip.lengthBars));
    const auto top = lane.getY() + 5.0f;

    return { x1 + 2.0f, top, juce::jmax (3.0f, x2 - x1 - 4.0f),
             juce::jmax (20.0f, lane.getHeight() - 10.0f) };
}

int ArrangeView::clipAt (juce::Point<int> position) const
{
    for (int i = static_cast<int> (clips.size()) - 1; i >= 0; --i)
        if (clipBounds (clips[static_cast<size_t> (i)]).contains (position.toFloat()))
            return i;

    return -1;
}

int ArrangeView::markerAt (juce::Point<int> position) const
{
    const auto markerRow = rulerArea.toFloat().withHeight (15.0f).withTrimmedTop (2.0f);

    if (position.x < laneArea.getX() || position.x > laneArea.getRight()
        || ! markerRow.contains (position.toFloat()))
        return -1;

    const auto& markers = engine.markers();

    for (int i = 0; i < static_cast<int> (markers.size()); ++i)
    {
        const auto x = static_cast<float> (barToX (1.0 + markers[static_cast<size_t> (i)].ppq / 4.0));

        if (std::abs (static_cast<float> (position.x) - x) <= 8.0f
            || (position.x >= x + 8.0f && position.x <= x + 76.0f))
            return i;
    }

    return -1;
}

void ArrangeView::renameMarker (int markerIndex)
{
    const auto& markers = engine.markers();

    if (! juce::isPositiveAndBelow (markerIndex, static_cast<int> (markers.size())))
        return;

    auto safe = juce::Component::SafePointer<ArrangeView> (this);
    auto* editor = new juce::AlertWindow ("Rename marker", "Marker name",
                                          juce::MessageBoxIconType::NoIcon);
    editor->addTextEditor ("name", juce::String (markers[static_cast<size_t> (markerIndex)].name), "Name");
    editor->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    editor->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    editor->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe, editor, markerIndex] (int result)
        {
            if (safe == nullptr || result != 1)
                return;

            const auto name = editor->getTextEditorContents ("name").trim();

            if (name.isEmpty())
                return;

            auto updated = safe->engine.markers();

            if (! juce::isPositiveAndBelow (markerIndex, static_cast<int> (updated.size())))
                return;

            updated[static_cast<size_t> (markerIndex)].name = name.toStdString();
            safe->engine.setMarkers (std::move (updated));
            safe->repaint (safe->rulerArea);

            if (safe->onStatus)
                safe->onStatus ("MARKER RENAMED / " + name);
        }), true);
}

int ArrangeView::toolAt (juce::Point<int> position) const
{
    for (int i = 0; i < static_cast<int> (toolRects.size()); ++i)
        if (toolRects[static_cast<size_t> (i)].contains (position))
            return i;

    return -1;
}

int ArrangeView::functionAt (juce::Point<int> position) const
{
    for (int i = 0; i < static_cast<int> (functionRects.size()); ++i)
        if (functionRects[static_cast<size_t> (i)].contains (position))
            return i;

    return -1;
}

juce::Rectangle<float> ArrangeView::fadeHandleBounds (const Clip& clip, bool fadeIn) const
{
    const auto r = clipBounds (clip);

    if (r.getWidth() < 26.0f)
        return {};

    const auto headerHeight = juce::jmin (22.0f, r.getHeight() * 0.36f);
    const auto size = 12.0f;
    return { fadeIn ? r.getX() : r.getRight() - size, r.getY() + headerHeight - 4.0f, size, size };
}

int ArrangeView::fadeHandleAt (const Clip& clip, juce::Point<int> position) const
{
    if (fadeHandleBounds (clip, true).contains (position.toFloat()))
        return 0;

    if (fadeHandleBounds (clip, false).contains (position.toFloat()))
        return 1;

    return -1;
}

float ArrangeView::clipGainY (const Clip& clip, juce::Rectangle<float> body) const
{
    const auto normalized = juce::jlimit (0.0f, 1.0f, (clip.gainDb + 24.0f) / 36.0f);
    return body.getBottom() - normalized * body.getHeight();
}

//==============================================================================
void ArrangeView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    juce::ColourGradient background (juce::Colour (0xff151311), area.getX(), area.getY(),
                                     theme::ink, area.getX(), area.getBottom(), false);
    g.setGradientFill (background);
    g.fillRect (area);

    // Warm room light from the top.
    juce::ColourGradient light (theme::accent.withAlpha (0.05f), area.getCentreX(), 0.0f,
                                theme::accent.withAlpha (0.0f), area.getCentreX(), area.getHeight() * 0.7f, true);
    g.setGradientFill (light);
    g.fillRect (area);

    paintLanes (g);
    paintClips (g);
    paintTrackHeaders (g);
    paintRuler (g);
    paintPlayhead (g);
    paintToolbar (g);

    g.setTiledImageFill (theme::grainTexture(), 0, 0, 0.3f);
    g.fillRect (area);

    // Panel separations.
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawVerticalLine (laneArea.getX() - 1, static_cast<float> (rulerArea.getY()),
                        static_cast<float> (laneArea.getBottom()));
    g.drawVerticalLine (inspectorArea.getX() - 1, static_cast<float> (toolbarArea.getBottom()),
                        static_cast<float> (inspectorArea.getBottom()));
    g.setColour (theme::line);
    g.drawVerticalLine (laneArea.getX(), static_cast<float> (rulerArea.getY()),
                        static_cast<float> (laneArea.getBottom()));
}

void ArrangeView::paintToolbar (juce::Graphics& g)
{
    const auto area = toolbarArea.toFloat();

    juce::ColourGradient background (juce::Colour (0xff262524), area.getX(), area.getY(),
                                     juce::Colour (0xff1b1a19), area.getX(), area.getBottom(), false);
    g.setGradientFill (background);
    g.fillRect (area);

    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine (toolbarArea.getY(), area.getX(), area.getRight());
    theme::hairline (g, area.getX(), area.getBottom() - 1.0f, area.getRight(), area.getBottom() - 1.0f);

    for (int i = 0; i < static_cast<int> (toolRects.size()); ++i)
    {
        const auto r = toolRects[static_cast<size_t> (i)].toFloat();
        const auto selected = i == tool;

        if (selected)
        {
            g.setColour (theme::accent.withAlpha (0.16f));
            g.fillRoundedRectangle (r.expanded (2.0f), 3.0f);
        }

        g.setColour (selected ? theme::accent : juce::Colour (0xff1d1c1b));
        g.fillRoundedRectangle (r, theme::radius::tight);
        g.setColour (selected ? theme::accentHot.withAlpha (0.85f) : juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::tight, 1.0f);

        if (! selected)
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + 4.0f, r.getRight() - 4.0f);
        }

        drawToolIcon (g, r.reduced (7.0f, 6.0f), i,
                      selected ? theme::well : (i == 3 ? theme::paperDim : theme::paperDim));

        g.setColour (selected ? theme::well.withAlpha (0.75f) : theme::faint);
        g.setFont (theme::label (10.0f));
        g.drawText (juce::String (i + 1), toolRects[static_cast<size_t> (i)].reduced (5, 4),
                    juce::Justification::bottomRight, false);
    }

    // Edit functions. They act on the selected clip, so the whole cluster is
    // visibly disabled when nothing is selected.
    for (int i = 0; i < static_cast<int> (functionRects.size()); ++i)
    {
        const auto r = functionRects[static_cast<size_t> (i)].toFloat();
        const auto enabled = selectedClip >= 0;

        g.setColour (enabled ? juce::Colour (0xff282320) : juce::Colour (0xff1d1c1b));
        g.fillRoundedRectangle (r, theme::radius::tight);
        g.setColour (enabled ? theme::accent.withAlpha (0.62f) : juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::tight, 1.0f);

        g.setColour (enabled ? theme::paper : theme::faint);
        g.setFont (theme::label (10.5f));
        g.drawText (functionNames[static_cast<size_t> (i)], functionRects[static_cast<size_t> (i)],
                    juce::Justification::centred, false);
    }

    // Snap + grid chips.
    auto paintChip = [&g] (juce::Rectangle<int> chipArea, const juce::String& caption,
                           const juce::String& value, bool on)
    {
        theme::wellFace (g, chipArea.toFloat(), theme::radius::tight);
        auto inner = chipArea.reduced (8, 4);
        auto captionArea = inner.removeFromLeft (34);

        g.setColour (theme::muted);
        g.setFont (theme::label ());
        g.drawText (caption, captionArea, juce::Justification::centredLeft, false);

        g.setColour (on ? theme::accentHot : theme::muted);
        g.setFont (theme::clock (12.0f));
        g.drawText (value, inner, juce::Justification::centredRight, false);
    };

    // Clips land on bar lines only, so the chip says so.
    paintChip (snapArea, "SNAP", snap ? "BAR" : "OFF", snap);
    paintChip (gridArea, "GRID", "1 BAR", false);

    // Zoom cluster.
    auto paintZoom = [&g] (juce::Rectangle<int> button, const juce::String& glyph)
    {
        g.setColour (juce::Colour (0xff1d1c1b));
        g.fillRoundedRectangle (button.toFloat(), theme::radius::tight);
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.drawRoundedRectangle (button.toFloat().reduced (0.5f), theme::radius::tight, 1.0f);
        g.setColour (theme::paperDim);
        g.setFont (theme::ui (15.0f, true));
        g.drawText (glyph, button, juce::Justification::centred, false);
    };

    paintZoom (zoomOutArea, "-");
    paintZoom (zoomInArea, "+");

    auto readout = barsReadoutArea;
    g.setColour (theme::muted);
    g.setFont (theme::label ());
    g.drawText ("VIEW", readout.removeFromTop (11), juce::Justification::centredRight, false);
    g.setColour (theme::paperDim);
    g.setFont (theme::clock (12.0f));
    g.drawText (juce::String (visibleBars) + " BARS", readout, juce::Justification::centredRight, false);

    // Current tool name.
    if (toolNameArea.getWidth() >= 70)
    {
        g.setColour (theme::muted);
        g.setFont (theme::label ());
        g.drawText ("EDIT MODE", juce::Rectangle<int> (toolNameArea.getX(), toolbarArea.getY() + 9,
                                                      toolNameArea.getWidth(), 11),
                    juce::Justification::centredLeft, false);
        g.setColour (theme::paper);
        g.setFont (theme::display (16.0f));
        g.drawText (toolNames[tool], juce::Rectangle<int> (toolNameArea.getX(), toolbarArea.getY() + 21,
                                                          toolNameArea.getWidth(), 20),
                    juce::Justification::centredLeft, false);
    }
}

void ArrangeView::paintLanes (juce::Graphics& g)
{
    const auto area = laneArea.toFloat();
    g.setColour (juce::Colour (0xff0d0c0b));
    g.fillRect (area);

    for (int i = 0; i < shownTracks(); ++i)
    {
        auto lane = laneBounds (i).toFloat();
        const auto tint = tintForTrack (i);

        g.setColour (i % 2 == 0 ? juce::Colour (0xff12100e) : juce::Colour (0xff0f0e0c));
        g.fillRect (lane);

        if (i == selectedTrack)
        {
            g.setColour (tint.withAlpha (0.10f));
            g.fillRect (lane);
        }
        else if (i == hoverTrack)
        {
            g.setColour (juce::Colours::white.withAlpha (0.018f));
            g.fillRect (lane);
        }

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawHorizontalLine (juce::roundToInt (lane.getBottom()) - 1, lane.getX(), lane.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        g.drawHorizontalLine (juce::roundToInt (lane.getBottom()), lane.getX(), lane.getRight());
    }

    // Loop shading in the lanes.
    {
        const auto x1 = juce::jmax (area.getX(), static_cast<float> (barToX (loopStartBar)));
        const auto x2 = juce::jmin (area.getRight(), static_cast<float> (barToX (loopEndBar)));

        if (x2 > x1)
        {
            g.setColour (theme::accent.withAlpha (0.028f));
            g.fillRect (juce::Rectangle<float> (x1, area.getY(), x2 - x1, area.getHeight()));
        }
    }

    const auto spacing = static_cast<float> (laneArea.getWidth()) / static_cast<float> (juce::jmax (1, visibleBars));
    const auto firstBar = static_cast<int> (std::floor (viewStartBar));
    const auto lastBar = static_cast<int> (std::ceil (viewStartBar + static_cast<double> (visibleBars)));

    for (int bar = firstBar; bar <= lastBar; ++bar)
    {
        const auto x = static_cast<float> (barToX (bar));

        if (x < area.getX() - 1.0f || x > area.getRight() + 1.0f)
            continue;

        g.setColour (theme::paper.withAlpha (bar % 4 == 0 ? 0.15f : 0.09f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

        if (spacing > 34.0f)
        {
            for (int beat = 1; beat < 4; ++beat)
            {
                const auto bx = static_cast<float> (barToX (bar + beat * 0.25));
                g.setColour (theme::paper.withAlpha (spacing > 90.0f ? 0.055f : 0.035f));
                g.drawVerticalLine (juce::roundToInt (bx), area.getY(), area.getBottom());
            }
        }
    }

    // A very faint centre guide per lane, like a console scribble strip.
    for (int i = 0; i < shownTracks(); ++i)
    {
        auto lane = laneBounds (i).toFloat();

        if (lane.isEmpty())
            continue;

        g.setColour (tintForTrack (i).withAlpha (0.16f));
        g.drawHorizontalLine (juce::roundToInt (lane.getCentreY()), lane.getX(), lane.getRight());
    }
}

void ArrangeView::paintClips (juce::Graphics& g)
{
    const auto viewLeft = static_cast<float> (laneArea.getX());
    const auto viewRight = static_cast<float> (laneArea.getRight());

    for (int i = 0; i < static_cast<int> (clips.size()); ++i)
    {
        const auto& clip = clips[static_cast<size_t> (i)];
        auto r = clipBounds (clip);

        if (r.getRight() < viewLeft || r.getX() > viewRight)
            continue;

        const auto tint = tintForTrack (clip.track);
        const auto selected = i == selectedClip;
        const auto hovered = i == hoverClip;

        juce::Path body;
        body.addRoundedRectangle (r, 4.0f);

        g.saveState();
        g.reduceClipRegion (body);

        juce::ColourGradient fill (tint.withAlpha (selected ? 0.34f : 0.22f), r.getX(), r.getY(),
                                   juce::Colour (0xff121110), r.getX(), r.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRect (r);

        const auto headerHeight = juce::jmin (22.0f, r.getHeight() * 0.36f);
        auto header = r.withHeight (headerHeight);
        g.setColour (tint.withAlpha (selected ? 0.62f : 0.45f));
        g.fillRect (header);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine (juce::roundToInt (header.getBottom()) - 1, header.getX(), header.getRight());

        auto title = header.reduced (7.0f, 2.0f);
        auto kindArea = title.removeFromRight (38.0f);
        const auto showKind = r.getWidth() > 70.0f;

        if (showKind)
        {
            g.setColour (theme::paper.withAlpha (0.82f));
            g.setFont (theme::label (10.5f));
            g.drawText (clipKindName (clip.kind), kindArea.toNearestInt(),
                        juce::Justification::centredRight, false);
        }

        g.setColour (theme::paper);
        g.setFont (theme::ui (13.0f, true));
        g.drawText (clip.name, title.toNearestInt(), juce::Justification::centredLeft, true);

        auto content = r.withTrimmedTop (headerHeight + 1.0f).reduced (3.0f, 2.0f);

        if (content.getWidth() > 5.0f && content.getHeight() > 4.0f)
            paintClipContents (g, clip, content);

        paintClipFades (g, clip, r, selected || hovered);
        paintClipGain (g, clip, r, selected);

        g.restoreState();

        if (selected)
        {
            g.setColour (tint.withAlpha (0.22f));
            g.drawRoundedRectangle (r.expanded (1.5f), 3.0f, 1.5f);
            g.setColour (tint);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::tight, 1.8f);

            g.setColour (tint.brighter (0.5f));
            g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() - 1.0f, r.getCentreY() - 7.0f, 3.0f, 14.0f), 1.0f);
            g.fillRoundedRectangle (juce::Rectangle<float> (r.getRight() - 2.0f, r.getCentreY() - 7.0f, 3.0f, 14.0f), 1.0f);
        }
        else if (hovered)
        {
            g.setColour (theme::paper.withAlpha (0.24f));
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::tight, 1.0f);
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.65f));
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::tight, 1.0f);
        }
    }
}

void ArrangeView::paintClipContents (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> area)
{
    const auto tint = tintForTrack (clip.track);

    if (clip.kind == 1)
    {
        g.setColour (juce::Colour (0xff080706).withAlpha (0.55f));
        g.fillRoundedRectangle (area, 2.0f);
        paintWaveform (g, area.reduced (1.0f, 1.0f), clip.seed, tint, 0.58f, true);

        g.setColour (theme::paper.withAlpha (0.18f));
        g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    }
    else
    {
        // Real notes: pitch maps vertically (C-1 at bottom to C9 at top
        // compressed to the lane), time maps horizontally. Rows are the
        // 12 pitch classes, shaded on black-key rows like a piano-roll.
        const auto beats = nc::engine::clipLengthBeats (static_cast<double> (clip.lengthBars));

        for (int row = 0; row < 12; ++row)
        {
            const auto y = area.getY() + area.getHeight() * static_cast<float> (row) / 12.0f;
            if (nc::engine::isBlackPitch (row))
            {
                g.setColour (theme::paper.withAlpha (0.06f));
                g.fillRect (juce::Rectangle<float> (area.getX(), y, area.getWidth(), area.getHeight() / 12.0f));
            }
            else
            {
                g.setColour (theme::paper.withAlpha (0.035f));
                g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
            }
        }

        if (beats > 0.0)
        {
            const auto widthFor = [&](double beat)
            {
                return area.getX() + area.getWidth() * static_cast<float> (beat / beats);
            };

            const auto yFor = [&](int pitch)
            {
                const auto norm = static_cast<float> (juce::jlimit (0, 127, pitch)) / 127.0f;
                return area.getBottom() - norm * area.getHeight();
            };

            const auto rowH = area.getHeight() / 12.0f;

            for (const auto& note : clip.notes)
            {
                const auto x = widthFor (note.startBeat);
                const auto w = juce::jmax (1.5f, widthFor (note.startBeat + note.lengthBeats) - x);
                const auto y = yFor (note.pitch) - rowH * 0.3f;
                g.setColour ((nc::engine::isBlackPitch (note.pitch) ? theme::accentHot : tint).withAlpha (0.75f));
                g.fillRoundedRectangle (juce::Rectangle<float> (x, y, w, rowH * 0.6f), 1.0f);
            }
        }

        g.setColour (theme::paper.withAlpha (0.42f));
        g.setFont (theme::label (10.5f));
        g.drawText (juce::String (clip.lengthBars) + " BAR / " + juce::String (clip.notes.size()) + " NOTES",
                    area.toNearestInt().withTrimmedRight (4).withTrimmedBottom (2),
                    juce::Justification::bottomRight, false);
    }
}

void ArrangeView::paintClipFades (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> r, bool showHandles)
{
    const auto headerHeight = juce::jmin (22.0f, r.getHeight() * 0.36f);
    const auto body = r.withTrimmedTop (headerHeight + 1.0f).reduced (1.0f, 1.0f);

    if (body.getWidth() < 10.0f || body.getHeight() < 6.0f)
        return;

    const auto pixelsPerBar = static_cast<float> (barToX (clip.startBar + 1.0) - barToX (clip.startBar));

    if (pixelsPerBar <= 0.0f)
        return;

    const auto tint = tintForTrack (clip.track);
    const auto maxFade = body.getWidth() * 0.48f;
    const auto fadeIn = juce::jlimit (0.0f, maxFade, clip.fadeInBars * pixelsPerBar);
    const auto fadeOut = juce::jlimit (0.0f, maxFade, clip.fadeOutBars * pixelsPerBar);

    auto drawFade = [&] (float width, bool isFadeIn)
    {
        if (width < 2.0f)
            return;

        const auto x0 = isFadeIn ? body.getX() : body.getRight() - width;
        const auto x1 = isFadeIn ? body.getX() + width : body.getRight();
        juce::Path curve;
        curve.startNewSubPath (x0, body.getBottom());
        curve.cubicTo (x0 + (x1 - x0) * 0.42f, body.getBottom(),
                       x0 + (x1 - x0) * 0.58f, body.getY(), x1, body.getY());

        juce::Path shade = curve;
        shade.lineTo (x1, body.getY());
        shade.lineTo (x0, body.getY());
        shade.closeSubPath();

        g.setColour (theme::well.withAlpha (0.58f));
        g.fillPath (shade);
        g.setColour (tint.withAlpha (0.92f));
        g.strokePath (curve, juce::PathStrokeType (1.2f));
    };

    drawFade (fadeIn, true);
    drawFade (fadeOut, false);

    if (showHandles)
    {
        g.setColour (tint.brighter (0.5f));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ body.getX() + 3.0f, body.getY() + 3.0f }));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ body.getRight() - 3.0f, body.getY() + 3.0f }));
    }
}

void ArrangeView::paintClipGain (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> r, bool selected)
{
    const auto headerHeight = juce::jmin (22.0f, r.getHeight() * 0.36f);
    const auto body = r.withTrimmedTop (headerHeight + 1.0f).reduced (1.0f, 1.0f);

    if (body.getWidth() < 12.0f || body.getHeight() < 8.0f)
        return;

    const auto y = clipGainY (clip, body);
    const auto hasGain = std::abs (clip.gainDb) > 0.05f;

    if (! hasGain && ! selected)
        return;

    g.setColour (theme::paper.withAlpha (selected ? 0.82f : 0.34f));
    g.drawHorizontalLine (juce::roundToInt (y), body.getX() + 2.0f, body.getRight() - 2.0f);

    if (selected)
    {
        juce::Path handle;
        handle.startNewSubPath (body.getCentreX(), y - 4.0f);
        handle.lineTo (body.getCentreX() + 4.0f, y);
        handle.lineTo (body.getCentreX(), y + 4.0f);
        handle.lineTo (body.getCentreX() - 4.0f, y);
        handle.closeSubPath();
        g.setColour (theme::paper);
        g.fillPath (handle);

        if (body.getWidth() > 74.0f && body.getHeight() > 18.0f)
        {
            auto chip = juce::Rectangle<float> (body.getX() + 2.0f, body.getBottom() - 13.0f,
                                                juce::jmin (76.0f, body.getWidth() - 4.0f), 12.0f);
            g.setColour (theme::wellLo.withAlpha (0.84f));
            g.fillRoundedRectangle (chip, 1.5f);
            g.setColour (theme::paper);
            g.setFont (theme::label (10.0f));
            g.drawText ("GAIN " + juce::String (clip.gainDb, 1) + " dB", chip.toNearestInt().reduced (4, 0),
                        juce::Justification::centredLeft, false);
        }
    }
}

void ArrangeView::paintTrackHeaders (juce::Graphics& g)
{
    // Column header.
    {
        const auto top = tracksHeaderArea.toFloat();
        juce::ColourGradient background (juce::Colour (0xff242220), top.getX(), top.getY(),
                                         juce::Colour (0xff191817), top.getX(), top.getBottom(), false);
        g.setGradientFill (background);
        g.fillRect (top);

        // One bold line only: the old second line clipped against the first
        // lane edge on a 720 px-high window.
        auto inner = tracksHeaderArea.reduced (12, 8).toFloat();
        const auto shown = shownTracks();
        const auto rows = visibleTrackRows();
        const auto first = juce::jlimit (0, juce::jmax (0, shown - rows), firstVisibleTrack);
        const auto rangeText = rows < shown
                                   ? juce::String (shown) + " CHANNELS  /  "
                                         + juce::String (first + 1) + "-" + juce::String (first + rows) + " SHOWN"
                                   : juce::String (shown) + " CHANNELS  /  "
                                         + juce::String (engine.getActiveTrackCount()) + " ACTIVE";
        g.setColour (theme::paper);
        g.setFont (theme::ui (13.5f, true));
        g.drawText (rangeText, inner.removeFromTop (18.0f).toNearestInt(),
                    juce::Justification::centredLeft, false);

        theme::hairline (g, top.getX(), top.getBottom() - 1.0f, top.getRight(), top.getBottom() - 1.0f);
    }

    for (int i = 0; i < shownTracks(); ++i)
    {
        const auto lane = laneBounds (i);

        // Headers own the left column. Lane bounds are the timeline; painting the
        // strip there covered every clip, because headers draw after clips.
        const auto r = juce::Rectangle<int> (trackHeaderColumn.getX(), lane.getY(),
                                             trackHeaderColumn.getWidth(), lane.getHeight()).toFloat();
        const auto& spec = sessionTracks()[static_cast<size_t> (i)];
        const auto tint = tintForTrack (i);
        const auto selected = i == selectedTrack;

        juce::ColourGradient background (selected ? juce::Colour (0xff2b2724) : juce::Colour (0xff232120),
                                         r.getX(), r.getY(),
                                         selected ? juce::Colour (0xff1d1a18) : juce::Colour (0xff191817),
                                         r.getX(), r.getBottom(), false);
        g.setGradientFill (background);
        g.fillRect (r);

        if (i == hoverTrack && ! selected)
        {
            g.setColour (juce::Colours::white.withAlpha (0.022f));
            g.fillRect (r);
        }

        g.setColour (tint.withAlpha (selected ? 1.0f : 0.55f));
        g.fillRect (juce::Rectangle<float> (r.getX(), r.getY() + 1.0f, 3.0f, r.getHeight() - 2.0f));

        if (selected)
        {
            g.setColour (tint.withAlpha (0.92f));
            g.drawRoundedRectangle (r.reduced (3.5f), theme::radius::tight, 1.2f);
            g.setColour (tint.withAlpha (0.10f));
            g.fillRect (r.withWidth (4.0f));
        }

        const auto compact = lane.getHeight() < 84;

        auto inner = r.reduced (12.0f, 5.0f);
        inner.removeFromLeft (3.0f);

        auto topRow = inner.removeFromTop (compact ? 22.0f : 26.0f);
        auto number = topRow.removeFromLeft (32.0f);
        g.setColour (selected ? tint.brighter (0.35f) : tint);
        g.setFont (theme::clock (16.0f));
        g.drawText (juce::String (i + 1).paddedLeft ('0', 2), number.toNearestInt(),
                    juce::Justification::centredLeft, false);

        auto nameArea = topRow.withTrimmedRight (compact ? 0.0f : 14.0f);
        g.setColour (theme::paper);
        g.setFont (theme::display (16.0f));
        g.drawText (spec.name.toUpperCase(), nameArea.toNearestInt(), juce::Justification::centredLeft, true);

        if (selected && ! compact)
        {
            g.setColour (tint.brighter (0.35f));
            g.setFont (theme::label (10.0f));
            g.drawText ("SEL", nameArea.toNearestInt(), juce::Justification::centredRight, false);
        }

        if (! compact)
        {
            auto sub = inner.removeFromTop (16.0f);
            auto kindArea = sub.removeFromRight (54.0f);
            g.setColour (theme::paperDim);
            g.setFont (theme::ui (12.5f));
            g.drawText (spec.instrument, sub.toNearestInt(), juce::Justification::centredLeft, true);

            g.setColour (theme::muted);
            g.setFont (theme::label (10.0f));
            g.drawText (spec.kind, kindArea.toNearestInt(), juce::Justification::centredRight, false);
        }

        if (lane.getHeight() >= 96)
        {
            auto detail = inner.removeFromTop (14.0f);
            g.setColour (theme::muted);
            g.setFont (theme::label (10.0f));
            g.drawText ("INS  /  " + spec.inserts[0], detail.toNearestInt(),
                        juce::Justification::centredLeft, true);
        }

        auto controlRow = r.reduced (12.0f, 6.0f).removeFromBottom (28.0f);
        g.setColour (theme::muted);
        g.setFont (theme::label (10.0f));
        g.drawText ("OUT 1-2", controlRow.toNearestInt().withWidth (56), juce::Justification::centredLeft, false);

        paintTrackControls (g, lane, i);

        const auto meterHeight = juce::jmax (10, lane.getHeight() - 64);
        paintMiniMeter (g, { juce::roundToInt (r.getRight()) - 18, lane.getY() + 32, 7, meterHeight }, i);

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawHorizontalLine (lane.getBottom() - 1, r.getX(), r.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.026f));
        g.drawHorizontalLine (lane.getBottom(), r.getX(), r.getRight());
    }
}

void ArrangeView::paintTrackControls (juce::Graphics& g, juce::Rectangle<int> lane, int trackIndex)
{
    const auto mute = headerControl (trackIndex, 1);
    const auto solo = headerControl (trackIndex, 2);
    const auto record = headerControl (trackIndex, 3);

    drawSmallButton (g, mute, "M", engine.trackMuted (trackIndex), theme::mod);
    drawSmallButton (g, solo, "S", engine.trackSolo (trackIndex), theme::accentHot);
    drawSmallButton (g, record, "R", engine.isRecordArmed (trackIndex), theme::danger);

    juce::ignoreUnused (lane);
}

void ArrangeView::paintMiniMeter (juce::Graphics& g, juce::Rectangle<int> area, int trackIndex)
{
    if (area.getHeight() < 8)
        return;

    theme::wellFace (g, area.toFloat(), 2.0f);

    auto level = theme::levelFromGain (engine.trackPeak (trackIndex));

    if (engine.trackMuted (trackIndex))
        level = 0.0f;

    const auto segments = juce::jlimit (4, 14, area.getHeight() / 4);
    const auto gap = 1.0f;
    const auto cellHeight = (static_cast<float> (area.getHeight()) - gap * (segments - 1)) / static_cast<float> (segments);

    for (int i = 0; i < segments; ++i)
    {
        const auto fraction = static_cast<float> (i + 1) / static_cast<float> (segments);
        const auto on = level >= fraction - 0.03f;
        const auto colour = i >= segments - 2 ? theme::meterHigh
                          : i >= segments - 5 ? theme::meterMid
                                              : theme::meterLow;

        const auto y = area.getBottom() - (i + 1) * (cellHeight + gap) + gap;
        g.setColour (on ? colour.withAlpha (0.95f) : juce::Colour (0xff1b1b1b));
        g.fillRect (juce::Rectangle<float> (static_cast<float> (area.getX()), y,
                                            static_cast<float> (area.getWidth()), cellHeight));
    }
}

void ArrangeView::paintRuler (juce::Graphics& g)
{
    const auto area = rulerArea.toFloat();

    juce::ColourGradient background (juce::Colour (0xff201e1c), area.getX(), area.getY(),
                                     juce::Colour (0xff161514), area.getX(), area.getBottom(), false);
    g.setGradientFill (background);
    g.fillRect (area);

    // Loop bar.
    {
        auto loop = loopArea.toFloat();
        g.setColour (juce::Colour (0xff0a0a0a));
        g.fillRect (loop);

        const auto x1 = static_cast<float> (barToX (loopStartBar));
        const auto x2 = static_cast<float> (barToX (loopEndBar));

        g.saveState();
        g.reduceClipRegion (laneArea);

        if (x2 > x1)
        {
            g.setColour (theme::accent.withAlpha (0.15f));
            g.fillRect (juce::Rectangle<float> (x1, loop.getY() + 2.0f, x2 - x1, loop.getHeight() - 4.0f));
            g.setColour (theme::accentHot.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (x1, loop.getY() + 2.0f, x2 - x1, 2.0f));
            g.fillRect (juce::Rectangle<float> (x1, loop.getY() + 2.0f, 3.0f, loop.getHeight() - 4.0f));
            g.fillRect (juce::Rectangle<float> (x2 - 3.0f, loop.getY() + 2.0f, 3.0f, loop.getHeight() - 4.0f));

            if (x2 - x1 > 110.0f)
            {
                g.setColour (theme::paperDim);
                g.setFont (theme::label (10.0f));
                g.drawText ("LOOP  " + juce::String (loopStartBar).paddedLeft ('0', 3)
                                + "  -  " + juce::String (loopEndBar).paddedLeft ('0', 3),
                            juce::Rectangle<float> (x1 + 8.0f, loop.getY() + 2.0f,
                                                    x2 - x1 - 16.0f, loop.getHeight() - 4.0f).toNearestInt(),
                            juce::Justification::centredLeft, false);
            }
        }

        g.restoreState();
    }

    // Section markers come from the project (engine), not a decorative set:
    // a saved marker must survive save/load and appear at its real PPQ.
    const auto& markers = engine.markers();
    auto markerRow = rulerArea.toFloat().withHeight (15.0f).withTrimmedTop (2.0f);
    auto lastLabelRight = static_cast<float> (laneArea.getX()) - 20.0f;

    for (const auto& marker : markers)
    {
        const auto bar = 1.0 + marker.ppq / nc::engine::beatsPerBarMidi;
        const auto x = static_cast<float> (barToX (bar));

        if (x < laneArea.getX() - 1 || x > laneArea.getRight())
            continue;

        g.setColour (theme::paper.withAlpha (0.10f));
        g.drawVerticalLine (juce::roundToInt (x), markerRow.getY(), markerRow.getBottom());

        juce::Path flag;
        flag.addTriangle (x, markerRow.getY() + 1.0f, x + 7.0f, markerRow.getY() + 1.0f, x, markerRow.getY() + 9.0f);
        g.setColour (theme::accent.withAlpha (0.75f));
        g.fillPath (flag);

        if (x + 10.0f > lastLabelRight + 6.0f)
        {
            g.setColour (theme::paperDim);
            g.setFont (theme::label (10.0f));
            const auto labelArea = juce::Rectangle<float> (x + 10.0f, markerRow.getY(),
                                                           juce::jmin (70.0f, laneArea.getRight() - x - 10.0f),
                                                           markerRow.getHeight()).toNearestInt();
            g.drawText (juce::String (marker.name), labelArea, juce::Justification::centredLeft, true);
            lastLabelRight = x + 58.0f;
        }
    }

    // Bar numbers and ticks.
    const auto spacing = static_cast<float> (laneArea.getWidth()) / static_cast<float> (juce::jmax (1, visibleBars));
    const auto step = spacing >= 62.0f ? 1 : spacing >= 33.0f ? 2 : spacing >= 17.0f ? 4 : 8;
    const auto firstBar = static_cast<int> (std::floor (viewStartBar));
    const auto lastBar = static_cast<int> (std::ceil (viewStartBar + static_cast<double> (visibleBars)));
    const auto numberRow = rulerArea.toFloat().removeFromBottom (19.0f);
    const auto playBar = 1 + static_cast<int> (engine.positionSeconds()
                                              / juce::jmax (0.001, 240.0 / juce::jmax (1.0, engine.getBpm())));

    for (int bar = firstBar; bar <= lastBar; ++bar)
    {
        if (bar < 1 || (bar - 1) % step != 0)
            continue;

        const auto x = static_cast<float> (barToX (bar));

        if (x < laneArea.getX() - 1 || x > laneArea.getRight() + 1)
            continue;

        const auto current = bar == playBar;
        g.setColour (current ? theme::accentHot : theme::paper.withAlpha (0.22f));
        g.drawVerticalLine (juce::roundToInt (x), numberRow.getY(), rulerArea.getBottom());
        g.setColour (current ? theme::accentHot : theme::muted);
        g.setFont (theme::clock (current ? 13.0f : 12.0f));
        g.drawText (juce::String (bar), juce::Rectangle<int> (juce::roundToInt (x) + 4, juce::roundToInt (numberRow.getY()), 36, 17),
                    juce::Justification::centredLeft, false);
    }

    if (spacing > 62.0f)
    {
        for (int bar = firstBar; bar <= lastBar; ++bar)
        {
            for (int beat = 1; beat < 4; ++beat)
            {
                const auto x = static_cast<float> (barToX (bar + beat * 0.25));

                if (x < laneArea.getX() || x > laneArea.getRight())
                    continue;

                g.setColour (theme::paper.withAlpha (0.10f));
                g.drawVerticalLine (juce::roundToInt (x), rulerArea.getY() + 19.0f, rulerArea.getY() + 26.0f);
            }
        }
    }

    theme::hairline (g, area.getX(), area.getBottom() - 1.0f, area.getRight(), area.getBottom() - 1.0f);

    // The top-left module aligns with the ruler.
    g.setColour (theme::line);
    g.drawHorizontalLine (rulerArea.getY(), area.getX(), area.getRight());
}

void ArrangeView::paintPlayhead (juce::Graphics& g)
{
    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());
    const auto bar = 1.0 + engine.positionSeconds() / secondsPerBar;
    const auto x = static_cast<float> (barToX (bar));

    if (x < laneArea.getX() - 1.0f || x > laneArea.getRight() + 1.0f)
        return;

    const auto top = static_cast<float> (loopArea.getY());
    const auto bottom = static_cast<float> (laneArea.getBottom());

    juce::ColourGradient glow (theme::accentHot.withAlpha (0.17f), x, top,
                               theme::accentHot.withAlpha (0.0f), x + 13.0f, top, false);
    g.setGradientFill (glow);
    g.fillRect (juce::Rectangle<float> (x, top, 13.0f, bottom - top));

    juce::ColourGradient glowLeft (theme::accentHot.withAlpha (0.17f), x, top,
                                   theme::accentHot.withAlpha (0.0f), x - 13.0f, top, false);
    g.setGradientFill (glowLeft);
    g.fillRect (juce::Rectangle<float> (x - 13.0f, top, 13.0f, bottom - top));

    g.setColour (theme::accentHot);
    g.fillRect (juce::Rectangle<float> (x - 0.7f, top, 1.4f, bottom - top));

    g.setColour (theme::paper.withAlpha (0.8f));
    g.fillRect (juce::Rectangle<float> (x - 1.0f, top, 2.0f, 7.0f));

    juce::Path head;
    head.addTriangle (x - 5.0f, top, x + 5.0f, top, x, top + 9.0f);
    g.setColour (theme::accentHot);
    g.fillPath (head);
}

//==============================================================================
void ArrangeView::seekToX (int x)
{
    const auto bar = juce::jmax (1.0, xToBar (static_cast<double> (x)));
    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());
    engine.setPositionSeconds ((bar - 1.0) * secondsPerBar);
    lastPlayheadX = -1.0f;
    repaint();
}

void ArrangeView::toggleArm (int track)
{
    const auto index = juce::jlimit (0, shownTracks() - 1, track);
    engine.setRecordArmed (index, ! engine.isRecordArmed (index));
    repaint (laneBounds (index));
}

bool ArrangeView::webEditClip (int clipIndex, const juce::var& patch)
{
    const auto index = clipIndex;
    auto* obj = patch.getDynamicObject();

    if (index < 0 || index >= static_cast<int> (clips.size()) || obj == nullptr)
        return false;

    pushUndo();
    auto& clip = clips[static_cast<size_t> (index)];
    const auto oldStart = clip.startBar;
    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());

    if (obj->hasProperty ("startBar"))
        clip.startBar = juce::jmax (1, (int) std::round ((double) obj->getProperty ("startBar")));

    if (obj->hasProperty ("lengthBars"))
        clip.lengthBars = juce::jmax (1, (int) std::round ((double) obj->getProperty ("lengthBars")));

    if (obj->hasProperty ("gainDb"))
        clip.gainDb = juce::jlimit (-24.0f, 12.0f, (float) (double) obj->getProperty ("gainDb"));

    const auto length = static_cast<float> (clip.lengthBars);
    if (obj->hasProperty ("fadeInBars"))
        clip.fadeInBars = juce::jlimit (0.0f, length, (float) (double) obj->getProperty ("fadeInBars"));
    if (obj->hasProperty ("fadeOutBars"))
        clip.fadeOutBars = juce::jlimit (0.0f, length, (float) (double) obj->getProperty ("fadeOutBars"));

    if (obj->hasProperty ("trimmed") && (bool) obj->getProperty ("trimmed"))
        clip.sourceOffsetSeconds = juce::jmax (0.0, clip.sourceOffsetSeconds
                                                       + (clip.startBar - oldStart) * secondsPerBar);

    publishTimeline();
    refresh();
    return true;
}

bool ArrangeView::webSplitClip (int clipIndex, double bar)
{
    const auto index = clipIndex;

    if (index < 0 || index >= static_cast<int> (clips.size()))
        return false;

    splitClip (index, juce::jmax (1, (int) std::round (bar)));
    publishTimeline();
    refresh();
    return true;
}

bool ArrangeView::webDuplicateClip (int clipIndex)
{
    const auto index = clipIndex;

    if (index < 0 || index >= static_cast<int> (clips.size()))
        return false;

    selectedClip = index;
    return duplicateSelected();
}

bool ArrangeView::webDeleteClip (int clipIndex)
{
    const auto index = clipIndex;

    if (index < 0 || index >= static_cast<int> (clips.size()))
        return false;

    selectedClip = index;
    return deleteSelected();
}

bool ArrangeView::webClipAt (int index, WebClipView& out) const
{
    if (index < 0 || index >= static_cast<int> (clips.size()))
        return false;

    const auto& clip = clips[static_cast<size_t> (index)];
    out.track = clip.track;
    out.startBar = clip.startBar;
    out.lengthBars = clip.lengthBars;
    out.name = clip.name;
    out.kind = clip.kind;
    out.gainDb = clip.gainDb;
    out.fadeInBars = clip.fadeInBars;
    out.fadeOutBars = clip.fadeOutBars;
    out.crossfadeBars = clip.crossfadeBars;
    out.compTake = clip.compTake;
    out.assetId = clip.assetId;
    out.sourceOffsetSeconds = clip.sourceOffsetSeconds;
    out.notes = clip.kind == 0 ? &clip.notes : nullptr;
    return true;
}

bool ArrangeView::webSetNotes (int clipIndex, const std::vector<nc::engine::MidiNote>& notes)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int> (clips.size()))
        return false;

    if (clips[static_cast<size_t> (clipIndex)].kind != 0)
        return false;

    applyMidiNotes (clipIndex, notes);
    return true;
}

void ArrangeView::webSetLoopRange (double startBar, double endBar)
{
    loopStartBar = juce::jlimit (1, nc::limits::maxBars, (int) std::round (startBar));
    loopEndBar = juce::jlimit (loopStartBar + 1, nc::limits::maxBars + 1, (int) std::round (endBar));
    engine.setLoopBars (loopStartBar, loopEndBar);
    repaint (rulerArea);
    repaint (laneArea);
}

void ArrangeView::splitClip (int clipIndex, int bar)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int> (clips.size()))
        return;

    auto original = clips[static_cast<size_t> (clipIndex)];
    const auto endBar = original.startBar + original.lengthBars;

    if (bar <= original.startBar || bar >= endBar)
        return;

    pushUndo();

    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());
    auto right = original;
    right.startBar = bar;
    right.lengthBars = endBar - bar;
    right.seed += 0x9e37u;
    right.sourceOffsetSeconds = original.sourceOffsetSeconds
                              + static_cast<double> (bar - original.startBar) * secondsPerBar;
    right.fadeInBars = 0.0f;
    right.crossfadeBars = 0.0f;
    right.fadeOutBars = juce::jmin (original.fadeOutBars, static_cast<float> (right.lengthBars) * 0.45f);

    original.lengthBars = bar - original.startBar;
    original.fadeOutBars = 0.0f;
    original.crossfadeBars = 0.0f;
    original.fadeInBars = juce::jmin (original.fadeInBars, static_cast<float> (original.lengthBars) * 0.45f);

    // MIDI split: notes at/after the split move right and rebase to 0;
    // notes straddling the split are cut at the boundary.
    if (original.kind == 0)
    {
        const auto splitBeat = static_cast<double> (bar - clips[static_cast<size_t> (clipIndex)].startBar) * 4.0;
        std::vector<nc::engine::MidiNote> leftNotes, rightNotes;

        for (const auto& note : clips[static_cast<size_t> (clipIndex)].notes)
        {
            if (note.startBeat + note.lengthBeats <= splitBeat)
            {
                leftNotes.push_back (note);
                continue;
            }

            if (note.startBeat >= splitBeat)
            {
                auto shifted = note;
                shifted.startBeat -= splitBeat;
                rightNotes.push_back (shifted);
                continue;
            }

            auto left = note;
            left.lengthBeats = splitBeat - note.startBeat;
            leftNotes.push_back (left);

            auto rightNote = note;
            rightNote.startBeat = 0.0;
            rightNote.lengthBeats = note.startBeat + note.lengthBeats - splitBeat;
            rightNotes.push_back (rightNote);
        }

        original.notes = std::move (leftNotes);
        right.notes = std::move (rightNotes);
        nc::engine::sanitizeNotes (original.notes, nc::engine::clipLengthBeats (static_cast<double> (original.lengthBars)));
        nc::engine::sanitizeNotes (right.notes, nc::engine::clipLengthBeats (static_cast<double> (right.lengthBars)));
    }

    clips[static_cast<size_t> (clipIndex)] = original;
    clips.insert (clips.begin() + clipIndex + 1, right);
    selectedClip = clipIndex + 1;
    publishTimeline();
    repaint();
}

void ArrangeView::addClipAt (int trackIndex, int bar)
{
    const auto index = juce::jlimit (0, shownTracks() - 1, trackIndex);
    pushUndo();

    Clip clip;
    clip.track = index;
    clip.startBar = juce::jmax (1, bar);
    clip.lengthBars = 4;
    clip.kind = sessionTracks()[static_cast<size_t> (index)].kind == "AUDIO" ? 1 : 0;
    clip.name = clip.kind == 0 ? "NEW PATTERN" : "NEW AUDIO";
    clip.seed = static_cast<juce::uint32> (juce::Time::getMillisecondCounter()) * 2654435761u;
    clip.fadeInBars = 0.25f;
    clip.fadeOutBars = 0.25f;
    clip.assetId = engine.demoAssetId (index);
    clip.sourceOffsetSeconds = 0.0;

    clips.push_back (clip);
    selectedClip = static_cast<int> (clips.size()) - 1;
    selectedTrack = index;
    inspector.setTrack (index);

    if (onTrackSelected)
        onTrackSelected (index);

    publishTimeline();
    repaint();
}

bool ArrangeView::splitSelectedAtPlayhead()
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()))
    {
        if (onStatus)
            onStatus ("Select a clip first / then split it at the playhead");

        return false;
    }

    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());
    const auto bar = 1 + juce::roundToInt (engine.positionSeconds() / secondsPerBar);
    const auto& clip = clips[static_cast<size_t> (selectedClip)];
    const auto name = clip.name;
    const auto endBar = clip.startBar + clip.lengthBars;

    if (bar <= clip.startBar || bar >= endBar)
    {
        if (onStatus)
            onStatus ("Playhead is outside " + name + " / move it inside the clip, then split");

        return false;
    }

    splitClip (selectedClip, bar);

    if (onStatus)
        onStatus ("Split " + name + " at bar " + juce::String (bar));

    return true;
}

bool ArrangeView::duplicateSelected()
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()))
    {
        if (onStatus)
            onStatus ("Select a clip first / then duplicate it");

        return false;
    }

    pushUndo();

    auto copy = clips[static_cast<size_t> (selectedClip)];
    const auto name = copy.name;
    copy.startBar += copy.lengthBars;
    copy.seed += 0x1b3du;
    copy.fadeInBars = juce::jmin (copy.fadeInBars, static_cast<float> (copy.lengthBars) * 0.45f);
    copy.fadeOutBars = juce::jmin (copy.fadeOutBars, static_cast<float> (copy.lengthBars) * 0.45f);

    clips.insert (clips.begin() + selectedClip + 1, copy);
    selectedClip += 1;

    if (onStatus)
        onStatus ("Duplicated " + name + " / bar " + juce::String (copy.startBar));

    publishTimeline();
    repaint();
    return true;
}

bool ArrangeView::deleteSelected()
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()))
    {
        if (onStatus)
            onStatus ("Select a clip first / then delete it");

        return false;
    }

    pushUndo();

    const auto name = clips[static_cast<size_t> (selectedClip)].name;
    clips.erase (clips.begin() + selectedClip);
    selectedClip = -1;
    hoverClip = -1;

    if (onStatus)
        onStatus ("Deleted " + name + " / Ctrl+Z to undo");

    publishTimeline();
    repaint();
    return true;
}

//==============================================================================
void ArrangeView::publishTimeline()
{
    std::vector<AudioEngine::TimelineClip> schedule;
    schedule.reserve (clips.size());
    std::vector<AudioEngine::MidiTimelineClip> midiSchedule;
    midiSchedule.reserve (clips.size());

    for (const auto& clip : clips)
    {
        if (clip.kind == 0)
        {
            AudioEngine::MidiTimelineClip next;
            next.track = clip.track;
            next.startBar = static_cast<double> (clip.startBar);
            next.lengthBars = static_cast<double> (clip.lengthBars);
            next.name = clip.name;
            next.notes = clip.notes;
            next.ccs = clip.ccs;
            next.bends = clip.bends;
            midiSchedule.push_back (std::move (next));
            continue;
        }

        if (clip.assetId < 0)
            continue;

        AudioEngine::TimelineClip next;
        next.assetId = clip.assetId;
        next.track = clip.track;
        next.startBar = static_cast<double> (clip.startBar);
        next.lengthBars = static_cast<double> (clip.lengthBars);
        next.sourceOffsetSeconds = clip.sourceOffsetSeconds;
        next.gainDb = clip.gainDb;
        next.fadeInBars = clip.fadeInBars;
        next.fadeOutBars = clip.fadeOutBars;
        next.crossfadeBars = clip.crossfadeBars;
        schedule.push_back (next);
    }

    const auto dropped = engine.setTimelineClips (std::move (schedule));
    engine.setMidiClips (std::move (midiSchedule));

    if (dropped > 0 && onStatus)
        onStatus (juce::String (dropped) + " CLIP" + juce::String (dropped == 1 ? "" : "S")
                      + " BEYOND THE 1024-CLIP TIMELINE " + juce::String (dropped == 1 ? "WAS" : "WERE")
                      + " DROPPED / SPLIT THE PROJECT TO KEEP EVERYTHING");
}

bool ArrangeView::placeRecordedTake (const juce::File& file, int track, double startBar)
{
    const auto assetId = engine.loadAudioFile (file);

    if (assetId < 0)
        return false;

    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());
    const auto duration = engine.assetDurationSeconds (assetId);
    const auto lengthBars = juce::jlimit (1, 1024,
                                          static_cast<int> (std::ceil (duration / secondsPerBar)));

    pushUndo();

    Clip clip;
    clip.track = juce::jlimit (0, juce::jmax (0, engine.getActiveTrackCount() - 1), track);
    clip.startBar = juce::jlimit (1, 1024, static_cast<int> (startBar));
    clip.lengthBars = lengthBars;
    clip.name = file.getFileNameWithoutExtension().toUpperCase();
    clip.kind = 1;
    clip.assetId = assetId;
    clip.sourceOffsetSeconds = 0.0;
    clip.fadeInBars = 0.0f;
    clip.fadeOutBars = 0.0f;
    clip.seed = static_cast<juce::uint32> (juce::Time::getMillisecondCounter()) * 2654435761u;

    clips.push_back (clip);
    publishTimeline();
    refresh();
    return true;
}

bool ArrangeView::importAudioFile (const juce::File& file)
{
    const auto assetId = engine.loadAudioFile (file);

    if (assetId < 0)
    {
        if (onStatus)
            onStatus ("Import failed / unsupported or empty audio file: " + file.getFileName());

        return false;
    }

    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());
    const auto duration = engine.assetDurationSeconds (assetId);
    const auto lengthBars = juce::jlimit (1, 128, static_cast<int> (std::ceil (duration / secondsPerBar)));
    const auto startBar = juce::jlimit (1, 128, 1 + juce::roundToInt (engine.positionSeconds() / secondsPerBar));
    const auto track = selectedTrack;

    pushUndo();

    Clip clip;
    clip.track = track;
    clip.startBar = startBar;
    clip.lengthBars = lengthBars;
    clip.name = file.getFileNameWithoutExtension().toUpperCase();
    clip.kind = 1;
    clip.assetId = assetId;
    clip.sourceOffsetSeconds = 0.0;
    clip.fadeInBars = 0.0f;
    clip.fadeOutBars = 0.0f;
    clip.seed = static_cast<juce::uint32> (juce::Time::getMillisecondCounter()) * 2654435761u;

    clips.push_back (clip);
    selectedClip = static_cast<int> (clips.size()) - 1;
    selectedTrack = track;
    inspector.setTrack (selectedTrack);

    if (onTrackSelected)
        onTrackSelected (selectedTrack);

    publishTimeline();
    repaint();

    if (onStatus)
        onStatus ("Imported " + file.getFileName() + " / " + juce::String (lengthBars) + " bars / "
                       + juce::String (duration, 1) + " s / " + sessionTracks()[static_cast<size_t> (track)].name);

    return true;
}

bool ArrangeView::applyAudioSuite (const nc::engine::audiosuite::Process& process)
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()))
    {
        if (onStatus)
            onStatus ("AUDIOSUITE / SELECT A CLIP FIRST");

        return false;
    }

    auto& clip = clips[static_cast<size_t> (selectedClip)];

    if (clip.assetId < 0)
    {
        if (onStatus)
            onStatus ("AUDIOSUITE / THAT CLIP HAS NO AUDIO");

        return false;
    }

    pushUndo();

    const auto rendered = engine.processAsset (clip.assetId,
                                               nc::engine::audiosuite::kindName (process.kind),
                                               process);

    if (rendered < 0)
    {
        if (onStatus)
            onStatus ("AUDIOSUITE / RENDER FAILED, CLIP UNCHANGED");

        return false;
    }

    clip.assetId = rendered;
    publishTimeline();
    repaint();

    if (onStatus)
        onStatus (juce::String (nc::engine::audiosuite::kindName (process.kind)) + " APPLIED TO " + clip.name);

    return true;
}

bool ArrangeView::repeatSelected (int times)
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()) || times <= 0)
        return false;

    pushUndo();

    const auto base = clips[static_cast<size_t> (selectedClip)];
    auto nextStart = base.startBar + base.lengthBars;

    for (int i = 0; i < times; ++i)
    {
        auto copy = base;
        copy.startBar = nextStart;
        copy.name = base.name + " " + juce::String (i + 2);
        clips.push_back (copy);
        nextStart += base.lengthBars;
    }

    publishTimeline();
    repaint();

    if (onStatus)
        onStatus ("REPEATED " + base.name + " x" + juce::String (times));

    return true;
}

bool ArrangeView::stripSilenceSelected()
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()))
    {
        if (onStatus)
            onStatus ("STRIP SILENCE / SELECT A CLIP FIRST");

        return false;
    }

    auto& clip = clips[static_cast<size_t> (selectedClip)];
    const auto rate = engine.assetSampleRate (clip.assetId);
    const auto trim = engine.assetTrim (clip.assetId, 0.003f);

    if (rate <= 0.0)
    {
        if (onStatus)
            onStatus ("STRIP SILENCE / NO RESIDENT AUDIO TO ANALYSE");

        return false;
    }

    if (trim.frameCount <= 0)
    {
        if (onStatus)
            onStatus ("STRIP SILENCE / NOTHING ABOVE THE THRESHOLD");

        return false;
    }

    pushUndo();

    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());
    const auto headSeconds = static_cast<double> (trim.firstFrame) / rate;
    const auto contentSeconds = static_cast<double> (trim.frameCount) / rate;

    clip.sourceOffsetSeconds += headSeconds;
    clip.lengthBars = juce::jmax (1, juce::roundToInt (contentSeconds / secondsPerBar));

    publishTimeline();
    repaint();

    if (onStatus)
        onStatus ("STRIP SILENCE / KEPT " + juce::String (trim.frameCount) + " FRAMES");

    return true;
}

int ArrangeView::collectMediaTo (const juce::File& projectFolder, juce::String& report)
{
    report.clear();

    const auto mediaFolder = projectFolder.getChildFile ("Media");

    if (! mediaFolder.isDirectory() && ! mediaFolder.createDirectory().wasOk())
    {
        report = "COULD NOT CREATE " + mediaFolder.getFullPathName();
        return -1;
    }

    std::vector<std::string> sources;
    int generated = 0;

    for (const auto& clip : clips)
    {
        if (clip.assetId < 0)
            continue;

        const auto file = engine.assetFile (clip.assetId);

        if (file == juce::File())
        {
            ++generated;
            continue;
        }

        sources.push_back (file.getFullPathName().toStdString());
    }

    std::vector<std::string> existing;

    for (const auto& entry : juce::RangedDirectoryIterator (mediaFolder, false, "*", juce::File::findFiles))
        existing.push_back (entry.getFile().getFileName().toStdString());

    const auto plan = nc::engine::planCollect (sources, existing,
                                               projectFolder.getFullPathName().toStdString(),
                                               [] (const std::string& path)
                                               {
                                                   return juce::File (juce::String (path)).existsAsFile();
                                               });

    std::map<juce::String, juce::String> destinations;
    int missing = 0;

    for (const auto& item : plan)
    {
        if (item.action == nc::engine::CollectAction::missing)
        {
            ++missing;
            continue;
        }

        if (item.action == nc::engine::CollectAction::alreadyInside)
            continue;

        destinations[juce::String (item.source)] = juce::String (item.destination);
    }

    int copied = 0;
    int failed = 0;
    std::map<juce::String, int> collected;

    for (const auto& entry : destinations)
    {
        const auto destination = juce::File (entry.second);
        const auto source = juce::File (entry.first);

        if (! destination.existsAsFile())
        {
            if (! source.copyFileTo (destination))
            {
                ++failed;
                continue;
            }

            ++copied;
        }

        collected[entry.first] = engine.loadAudioFile (destination);
    }

    if (! collected.empty())
        pushUndo();

    for (auto& clip : clips)
    {
        if (clip.assetId < 0)
            continue;

        const auto found = collected.find (engine.assetFile (clip.assetId).getFullPathName());

        if (found != collected.end() && found->second >= 0)
            clip.assetId = found->second;
    }

    publishTimeline();
    repaint();

    report = "COLLECTED " + juce::String (copied) + " FILES / "
           + juce::String (static_cast<int> (sources.size())) + " REFERENCES";

    if (generated > 0)
        report += " / " + juce::String (generated) + " GENERATED";

    if (missing > 0)
        report += " / " + juce::String (missing) + " MISSING";

    if (onStatus)
        onStatus (report);

    return failed > 0 ? -1 : copied;
}

void ArrangeView::writeClipsToDocument (nc::engine::ProjectDocument& document, const juce::File& projectFile) const
{
    const auto folder = projectFile.getParentDirectory();

    for (const auto& clip : clips)
    {
        if (clip.kind == 0)
        {
            nc::engine::ClipState state;
            state.isMidi = true;
            state.name = clip.name.toStdString();
            state.track = clip.track;
            state.startBar = static_cast<double> (clip.startBar);
            state.lengthBars = static_cast<double> (clip.lengthBars);
            state.gainDb = static_cast<double> (clip.gainDb);
            state.fadeInBars = static_cast<double> (clip.fadeInBars);
            state.fadeOutBars = static_cast<double> (clip.fadeOutBars);
            state.crossfadeBars = static_cast<double> (clip.crossfadeBars);
            state.compTake = clip.compTake;
            state.notes = clip.notes;
            state.ccs = clip.ccs;
            state.bends = clip.bends;
            document.clips.push_back (std::move (state));
            continue;
        }

        const auto file = engine.assetFile (clip.assetId);

        if (file == juce::File())
            continue;

        // Never store an absolute path in the project: a hostile file must
        // not gain access to files outside the project folder on open
        // (CWE-22). External media is collected into Media/ on save by
        // MainComponent::writeProject before this runs; anything still
        // outside is skipped rather than persisted as absolute.
        if (! file.isAChildOf (folder))
            continue;

        nc::engine::ClipState state;
        state.isMidi = false;
        state.assetPath = file.getRelativePathFrom (folder).toStdString();
        state.name = clip.name.toStdString();
        state.track = clip.track;
        state.startBar = static_cast<double> (clip.startBar);
        state.lengthBars = static_cast<double> (clip.lengthBars);
        state.sourceOffsetSeconds = clip.sourceOffsetSeconds;
        state.gainDb = static_cast<double> (clip.gainDb);
        state.fadeInBars = static_cast<double> (clip.fadeInBars);
        state.fadeOutBars = static_cast<double> (clip.fadeOutBars);
        state.crossfadeBars = static_cast<double> (clip.crossfadeBars);
        state.compTake = clip.compTake;
        document.clips.push_back (std::move (state));
    }
}

int ArrangeView::readClipsFromDocument (const nc::engine::ProjectDocument& document, const juce::File& projectFile, int& missing)
{
    const auto folder = projectFile.getParentDirectory();
    clips.clear();
    selectedClip = -1;
    undoStack.clear();
    missing = 0;
    auto restored = 0;

    for (const auto& state : document.clips)
    {
        if (state.isMidi)
        {
            Clip clip;
            clip.track = juce::jlimit (0, juce::jmax (0, engine.getActiveTrackCount() - 1), state.track);
            clip.startBar = juce::jmax (1, static_cast<int> (state.startBar));
            clip.lengthBars = juce::jmax (1, static_cast<int> (state.lengthBars));
            clip.name = juce::String (state.name);
            clip.kind = 0;
            clip.gainDb = static_cast<float> (state.gainDb);
            clip.fadeInBars = static_cast<float> (state.fadeInBars);
            clip.fadeOutBars = static_cast<float> (state.fadeOutBars);
            clip.crossfadeBars = static_cast<float> (state.crossfadeBars);
            clip.compTake = state.compTake;
            clip.notes = state.notes;
            clip.ccs = state.ccs;
            clip.bends = state.bends;
            clip.seed = static_cast<juce::uint32> (std::hash<std::string> {} (state.name));
            clips.push_back (clip);
            ++restored;
            continue;
        }

        const auto path = juce::String (state.assetPath);

        // Defence in depth with sanitize(): even if a caller bypassed it,
        // an absolute path or a parent segment must never resolve outside
        // the project folder (CWE-22). Lexical check first (isAChildOf on
        // an un-normalised "../" path is unreliable), canonical child
        // check second for the symlink case.
        if (path.isEmpty() || juce::File::isAbsolutePath (path))
        {
            ++missing;
            continue;
        }

        bool escapes = false;

        for (const auto& segment : juce::StringArray (path, "/\\"))
            if (segment.trim() == "..")
                escapes = true;

        if (escapes)
        {
            ++missing;
            continue;
        }

        const auto file = folder.getChildFile (path);

        if (! file.isAChildOf (folder))
        {
            ++missing;
            continue;
        }

        const auto assetId = file.existsAsFile() ? engine.loadAudioFile (file) : -1;

        if (assetId < 0)
        {
            ++missing;
            continue;
        }

        Clip clip;
        clip.track = juce::jlimit (0, juce::jmax (0, engine.getActiveTrackCount() - 1), state.track);
        clip.startBar = juce::jmax (1, static_cast<int> (state.startBar));
        clip.lengthBars = juce::jmax (1, static_cast<int> (state.lengthBars));
        clip.name = juce::String (state.name);
        clip.kind = 1;
        clip.gainDb = static_cast<float> (state.gainDb);
        clip.fadeInBars = static_cast<float> (state.fadeInBars);
        clip.fadeOutBars = static_cast<float> (state.fadeOutBars);
        clip.crossfadeBars = static_cast<float> (state.crossfadeBars);
        clip.compTake = state.compTake;
        clip.sourceOffsetSeconds = state.sourceOffsetSeconds;
        clip.assetId = assetId;
        clips.push_back (clip);
        ++restored;
    }

    publishTimeline();
    repaint();
    return restored;
}

void ArrangeView::clearSession()
{
    clips.clear();
    selectedClip = -1;
    undoStack.clear();
    publishTimeline();
    repaint();
}

void ArrangeView::pushUndo()
{
    undoStack.push (Snapshot { clips, selectedClip });
}

bool ArrangeView::undo()
{
    const auto restored = undoStack.undo (Snapshot { clips, selectedClip });

    if (! restored.has_value())
        return false;

    clips = restored->clips;
    selectedClip = restored->selectedClip;
    publishTimeline();
    repaint();
    return true;
}

bool ArrangeView::redo()
{
    const auto restored = undoStack.redo (Snapshot { clips, selectedClip });

    if (! restored.has_value())
        return false;

    clips = restored->clips;
    selectedClip = restored->selectedClip;
    publishTimeline();
    repaint();
    return true;
}

bool ArrangeView::selectNextClip()
{
    if (clips.empty())
        return false;

    selectedClip = selectedClip < 0 ? 0 : (selectedClip + 1) % static_cast<int> (clips.size());
    repaint();
    return true;
}

bool ArrangeView::selectPrevClip()
{
    if (clips.empty())
        return false;

    selectedClip = selectedClip < 0 ? static_cast<int> (clips.size()) - 1
                                    : (selectedClip - 1 + static_cast<int> (clips.size()))
                                          % static_cast<int> (clips.size());
    repaint();
    return true;
}

bool ArrangeView::nudgeSelected (int bars)
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()) || bars == 0)
        return false;

    pushUndo();
    auto& clip = clips[static_cast<size_t> (selectedClip)];
    clip.startBar = juce::jlimit (1, nc::limits::maxBars, clip.startBar + bars);
    publishTimeline();
    repaint();
    return true;
}

bool ArrangeView::trimSelectedLeft (int bars)
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()) || bars == 0)
        return false;

    pushUndo();
    auto& clip = clips[static_cast<size_t> (selectedClip)];
    const auto delta = juce::jlimit (-(clip.startBar - 1), clip.lengthBars - 1, bars);
    clip.startBar += delta;
    clip.lengthBars -= delta;
    clip.crossfadeBars = juce::jmin (clip.crossfadeBars, static_cast<float> (clip.lengthBars) * 0.45f);
    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());
    clip.sourceOffsetSeconds = juce::jmax (0.0, clip.sourceOffsetSeconds + delta * secondsPerBar);
    publishTimeline();
    repaint();
    return true;
}

bool ArrangeView::trimSelectedRight (int bars)
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()) || bars == 0)
        return false;

    pushUndo();
    auto& clip = clips[static_cast<size_t> (selectedClip)];
    clip.lengthBars = juce::jlimit (1, nc::limits::maxBars, clip.lengthBars + bars);
    clip.crossfadeBars = juce::jmin (clip.crossfadeBars, static_cast<float> (clip.lengthBars) * 0.45f);
    publishTimeline();
    repaint();
    return true;
}

bool ArrangeView::moveSelectedTrack (int deltaTracks)
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()) || deltaTracks == 0)
        return false;

    pushUndo();
    auto& clip = clips[static_cast<size_t> (selectedClip)];
    clip.track = juce::jlimit (0, juce::jmax (0, shownTracks() - 1), clip.track + deltaTracks);
    publishTimeline();
    repaint();
    return true;
}

bool ArrangeView::cycleSelectedCompTake (int delta)
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()))
        return false;

    pushUndo();
    auto& clip = clips[static_cast<size_t> (selectedClip)];
    clip.compTake = juce::jmax (0, clip.compTake + delta);
    publishTimeline();
    repaint();
    return true;
}

bool ArrangeView::applyCrossfadeSelected()
{
    if (selectedClip < 0 || selectedClip >= static_cast<int> (clips.size()))
        return false;

    pushUndo();
    auto& clip = clips[static_cast<size_t> (selectedClip)];
    const auto budget = static_cast<float> (clip.lengthBars) * 0.45f;
    clip.crossfadeBars = clip.crossfadeBars > 0.0f ? 0.0f : juce::jmin (0.5f, budget);
    publishTimeline();
    repaint();
    return true;
}

void ArrangeView::applyMidiNotes (int clipIndex, std::vector<nc::engine::MidiNote> notes)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int> (clips.size()))
        return;

    auto& clip = clips[static_cast<size_t> (clipIndex)];

    if (clip.kind != 0)
        return;

    pushUndo();
    clip.notes = std::move (notes);
    nc::engine::sanitizeNotes (clip.notes, nc::engine::clipLengthBeats (static_cast<double> (clip.lengthBars)));
    publishTimeline();
    repaint();
}

bool ArrangeView::openPianoRollForSelected()
{
    return openPianoRollForClip (selectedClip);
}

bool ArrangeView::openPianoRollForClip (int clipIndex)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int> (clips.size()))
    {
        if (onStatus)
            onStatus ("Select a MIDI clip first / then open the piano roll");

        return false;
    }

    const auto& clip = clips[static_cast<size_t> (clipIndex)];

    if (clip.kind != 0)
    {
        if (onStatus)
            onStatus ("Piano roll edits MIDI clips only / " + clip.name + " is audio");

        return false;
    }

    auto editor = std::make_unique<PianoRoll> (clip.name, static_cast<double> (clip.lengthBars), clip.notes,
                                               [this, clipIndex] (const std::vector<nc::engine::MidiNote>& notes)
                                               {
                                                   applyMidiNotes (clipIndex, notes);
                                               });
    editor->setSize (760, 420);
    selectedClip = clipIndex;
    selectedTrack = clip.track;
    inspector.setTrack (selectedTrack);

    auto* window = new juce::DocumentWindow (clip.name + " / Piano Roll", juce::Colours::black, juce::DocumentWindow::closeButton);
    window->setResizable (true, true);
    window->setUsingNativeTitleBar (false);
    window->centreWithSize (760, 420);
    window->setContentComponent (editor.release());
    window->setVisible (true);

    if (onStatus)
        onStatus ("PIANO ROLL / " + clip.name + " / double-click a MIDI clip to edit it");

    return true;
}

//==============================================================================
void ArrangeView::mouseDown (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    if (toolbarArea.contains (position))
    {
        const auto toolIndex = toolAt (position);

        if (toolIndex >= 0)
        {
            setTool (toolIndex);
            return;
        }

        if (const auto functionIndex = functionAt (position); functionIndex >= 0)
        {
            if (functionIndex == 0)
                splitSelectedAtPlayhead();
            else if (functionIndex == 1)
                duplicateSelected();
            else if (functionIndex == 2)
                deleteSelected();

            return;
        }

        if (zoomInArea.contains (position))
        {
            zoomIn();
            return;
        }

        if (zoomOutArea.contains (position))
        {
            zoomOut();
            return;
        }

        if (snapArea.contains (position))
        {
            snap = ! snap;
            repaint (toolbarArea);
            return;
        }

        return;
    }

    if (position.x < laneArea.getX() && position.y >= laneArea.getY())
    {
        const auto trackIndex = trackAt (position);

        if (trackIndex < 0)
            return;

        if (headerControl (trackIndex, 1).contains (position))
        {
            engine.setTrackMuted (trackIndex, ! engine.trackMuted (trackIndex));
            repaint (laneBounds (trackIndex));
            return;
        }

        if (headerControl (trackIndex, 2).contains (position))
        {
            engine.setTrackSolo (trackIndex, ! engine.trackSolo (trackIndex));
            repaint (laneBounds (trackIndex));
            return;
        }

        if (headerControl (trackIndex, 3).contains (position))
        {
            toggleArm (trackIndex);
            return;
        }

        selectedTrack = trackIndex;
        selectedClip = -1;
        inspector.setTrack (selectedTrack);

        if (onTrackSelected)
            onTrackSelected (selectedTrack);

        repaint();
        return;
    }

    if (position.x >= laneArea.getX() && position.y < rulerArea.getBottom())
    {
        if (const auto markerIndex = markerAt (position); markerIndex >= 0)
        {
            markerDragState = engine.markers();
            markerDragIndex = markerIndex;
            markerDragOriginX = position.x;
            markerDragOriginPpq = markerDragState[static_cast<size_t> (markerIndex)].ppq;
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

        if (loopArea.contains (position))
        {
            const auto x = static_cast<double> (position.x);
            const auto startX = barToX (loopStartBar);
            const auto endX = barToX (loopEndBar);

            draggingLoop = true;
            dragAnchorBar = xToBar (x);
            loopOriginBar = loopStartBar;
            loopOriginLength = loopEndBar - loopStartBar;

            if (std::abs (x - startX) <= 7.0)
                loopDragMode = 1;
            else if (std::abs (x - endX) <= 7.0)
                loopDragMode = 2;
            else if (x > startX && x < endX)
                loopDragMode = 3;
            else
            {
                draggingLoop = false;
                seekToX (position.x);
                draggingPlayhead = true;
            }

            return;
        }

        seekToX (position.x);
        draggingPlayhead = true;
        return;
    }

    if (! laneArea.contains (position))
        return;

    const auto trackIndex = trackAt (position);
    const auto bar = juce::jmax (1, juce::roundToInt (xToBar (static_cast<double> (position.x))));
    const auto clipIndex = clipAt (position);

    auto selectTrackHere = [this, trackIndex]
    {
        if (selectedTrack != trackIndex)
        {
            selectedTrack = trackIndex;
            inspector.setTrack (selectedTrack);

            if (onTrackSelected)
                onTrackSelected (selectedTrack);
        }
    };

    if (tool == 5)
    {
        if (clipIndex >= 0)
        {
            selectTrackHere();
            selectedClip = clipIndex;
            deleteSelected();
        }

        return;
    }

    if (tool == 4)
    {
        if (clipIndex >= 0)
        {
            selectTrackHere();
            splitClip (clipIndex, bar);
        }

        return;
    }

    if (clipIndex < 0)
    {
        selectedClip = -1;
        selectTrackHere();

        if (tool == 0)
        {
            seekToX (position.x);
            draggingPlayhead = true;
        }

        repaint();
        return;
    }

    selectTrackHere();
    selectedClip = clipIndex;

    auto& clip = clips[static_cast<size_t> (clipIndex)];
    dragClipIndex = clipIndex;
    dragOriginBar = clip.startBar;
    dragOriginStart = clip.startBar;
    dragOriginLength = clip.lengthBars;
    dragOriginGain = clip.gainDb;
    dragOriginFadeIn = clip.fadeInBars;
    dragOriginFadeOut = clip.fadeOutBars;
    dragOriginSourceOffset = clip.sourceOffsetSeconds;
    dragOriginX = position.x;
    dragAnchorBar = xToBar (static_cast<double> (position.x));
    dragUndoPushed = false;

    if (tool == 3)
    {
        dragMode = DragMode::gain;
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        repaint();
        return;
    }

    if (tool == 2)
    {
        const auto bounds = clipBounds (clip);
        dragMode = position.x < bounds.getCentreX() ? DragMode::fadeIn : DragMode::fadeOut;
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        repaint();
        return;
    }

    const auto bounds = clipBounds (clip);
    const auto handle = fadeHandleAt (clip, position);
    const auto nearLeft = bounds.getX() + 9.0f > static_cast<float> (position.x);
    const auto nearRight = bounds.getRight() - 9.0f < static_cast<float> (position.x);

    if (tool == 1)
        dragMode = position.x < bounds.getCentreX() ? DragMode::trimLeft : DragMode::trimRight;
    else if (handle == 0)
        dragMode = DragMode::fadeIn;
    else if (handle == 1)
        dragMode = DragMode::fadeOut;
    else if (nearLeft)
        dragMode = DragMode::trimLeft;
    else if (nearRight)
        dragMode = DragMode::trimRight;
    else
        dragMode = DragMode::move;

    setMouseCursor (dragMode == DragMode::move ? juce::MouseCursor::DraggingHandCursor
                                                : juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void ArrangeView::mouseDrag (const juce::MouseEvent& event)
{
    if (markerDragIndex >= 0 && markerDragIndex < static_cast<int> (markerDragState.size()))
    {
        const auto deltaBars = xToBar (static_cast<double> (event.getPosition().x))
                             - xToBar (static_cast<double> (markerDragOriginX));
        auto ppq = markerDragOriginPpq + deltaBars * 4.0;

        if (snap)
        {
            const auto bar = std::round (1.0 + ppq / 4.0);
            ppq = (bar - 1.0) * 4.0;
        }

        markerDragState[static_cast<size_t> (markerDragIndex)].ppq = juce::jmax (0.0, ppq);
        engine.setMarkers (markerDragState);
        repaint (rulerArea);
        return;
    }

    if (draggingPlayhead)
    {
        seekToX (event.getPosition().x);
        return;
    }

    if (draggingLoop)
    {
        const auto bar = juce::roundToInt (xToBar (static_cast<double> (event.getPosition().x)));

        if (loopDragMode == 1)
        {
            loopStartBar = juce::jlimit (1, loopEndBar - 1, bar);
        }
        else if (loopDragMode == 2)
        {
            loopEndBar = juce::jmax (loopStartBar + 1, bar);
        }
        else if (loopDragMode == 3)
        {
            const auto delta = juce::roundToInt (xToBar (static_cast<double> (event.getPosition().x)) - dragAnchorBar);
            loopStartBar = juce::jmax (1, loopOriginBar + delta);
            loopEndBar = loopStartBar + loopOriginLength;
        }

        engine.setLoopBars (loopStartBar, loopEndBar);
        repaint();
        return;
    }

    if (dragClipIndex < 0 || dragClipIndex >= static_cast<int> (clips.size())
        || dragMode == DragMode::none)
        return;

    if (! dragUndoPushed)
    {
        pushUndo();
        dragUndoPushed = true;
    }

    auto& clip = clips[static_cast<size_t> (dragClipIndex)];
    const auto delta = juce::roundToInt (xToBar (static_cast<double> (event.getPosition().x)) - dragAnchorBar);

    switch (dragMode)
    {
        case DragMode::move:
            clip.startBar = juce::jmax (1, dragOriginBar + delta);

            if (const auto targetTrack = trackAt (event.getPosition());
                targetTrack >= 0 && targetTrack != clip.track)
            {
                clip.track = targetTrack;
                selectedTrack = targetTrack;
                inspector.setTrack (selectedTrack);

                if (onTrackSelected)
                    onTrackSelected (selectedTrack);
            }
            break;

        case DragMode::trimLeft:
        {
            const auto endBar = dragOriginStart + dragOriginLength;
            const auto newStart = juce::jlimit (1, endBar - 1, dragOriginStart + delta);
            const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());
            clip.startBar = newStart;
            clip.lengthBars = endBar - newStart;
            clip.sourceOffsetSeconds = juce::jmax (0.0, dragOriginSourceOffset
                                                          + static_cast<double> (newStart - dragOriginStart) * secondsPerBar);
            clip.fadeInBars = juce::jmin (clip.fadeInBars, static_cast<float> (clip.lengthBars) * 0.45f);
            break;
        }

        case DragMode::trimRight:
            clip.lengthBars = juce::jmax (1, dragOriginLength + delta);
            clip.fadeInBars = juce::jmin (clip.fadeInBars, static_cast<float> (clip.lengthBars) * 0.45f);
            clip.fadeOutBars = juce::jmin (clip.fadeOutBars, static_cast<float> (clip.lengthBars) * 0.45f);
            break;

        case DragMode::fadeIn:
            clip.fadeInBars = juce::jlimit (0.0f, static_cast<float> (clip.lengthBars) * 0.45f,
                                            static_cast<float> (xToBar (static_cast<double> (event.getPosition().x))
                                                                    - static_cast<double> (clip.startBar)));
            break;

        case DragMode::fadeOut:
            clip.fadeOutBars = juce::jlimit (0.0f, static_cast<float> (clip.lengthBars) * 0.45f,
                                             static_cast<float> (static_cast<double> (clip.startBar + clip.lengthBars)
                                                                     - xToBar (static_cast<double> (event.getPosition().x))));
            break;

        case DragMode::gain:
            clip.gainDb = juce::jlimit (-24.0f, 12.0f,
                                        dragOriginGain + static_cast<float> (-event.getDistanceFromDragStartY()) * 0.25f);
            break;

        case DragMode::none:
        default:
            break;
    }

    repaint();
}

void ArrangeView::mouseUp (const juce::MouseEvent&)
{
    if (markerDragIndex >= 0)
    {
        if (markerDragIndex < static_cast<int> (markerDragState.size()))
        {
            const auto& marker = markerDragState[static_cast<size_t> (markerDragIndex)];
            engine.setMarkers (markerDragState);

            if (onStatus)
                onStatus ("MARKER MOVED / " + juce::String (marker.name) + " / BAR "
                          + juce::String (1.0 + marker.ppq / 4.0, 2));
        }

        markerDragIndex = -1;
        markerDragOriginX = 0;
        markerDragOriginPpq = 0.0;
        markerDragState.clear();
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint (rulerArea);
        return;
    }

    if (dragClipIndex >= 0 && dragClipIndex < static_cast<int> (clips.size())
        && dragMode != DragMode::none)
    {
        const auto& clip = clips[static_cast<size_t> (dragClipIndex)];

        if (dragMode == DragMode::gain)
        {
            if (onStatus)
                onStatus ("Clip gain " + juce::String (clip.gainDb, 1) + " dB / " + clip.name);
        }
        else if (dragMode == DragMode::fadeIn)
        {
            if (onStatus)
                onStatus ("Fade in " + juce::String (clip.fadeInBars, 2) + " bars / " + clip.name);
        }
        else if (dragMode == DragMode::fadeOut)
        {
            if (onStatus)
                onStatus ("Fade out " + juce::String (clip.fadeOutBars, 2) + " bars / " + clip.name);
        }
    }

    if (dragMode != DragMode::none)
        publishTimeline();

    draggingPlayhead = false;
    draggingLoop = false;
    dragUndoPushed = false;
    dragMode = DragMode::none;
    loopDragMode = 0;
    dragClipIndex = -1;
    setMouseCursor (tool == 0 ? juce::MouseCursor::NormalCursor : juce::MouseCursor::CrosshairCursor);
}

void ArrangeView::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    if (const auto markerIndex = markerAt (position); markerIndex >= 0)
    {
        renameMarker (markerIndex);
        return;
    }

    if (loopArea.contains (position))
    {
        loopStartBar = 1;
        loopEndBar = 17;
        engine.setLoopBars (loopStartBar, loopEndBar);
        repaint();
        return;
    }

    if (position.y < rulerArea.getBottom())
        return;

    if (! laneArea.contains (position))
        return;

    const auto hit = clipAt (position);

    if (hit >= 0)
    {
        selectedClip = hit;
        selectedTrack = clips[static_cast<size_t> (hit)].track;
        inspector.setTrack (selectedTrack);

        if (clips[static_cast<size_t> (hit)].kind == 0)
            openPianoRollForClip (hit);
        return;
    }

    addClipAt (trackAt (position), juce::jmax (1, juce::roundToInt (xToBar (static_cast<double> (position.x)))));
}

void ArrangeView::mouseMove (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();
    const auto previousHoverTrack = hoverTrack;
    const auto previousHoverClip = hoverClip;

    if (toolbarArea.contains (position))
    {
        hoverTrack = -1;
        hoverClip = -1;
        const auto overControl = toolAt (position) >= 0 || functionAt (position) >= 0
                              || zoomInArea.contains (position) || zoomOutArea.contains (position)
                              || snapArea.contains (position);
        setMouseCursor (overControl ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

        if (previousHoverTrack != hoverTrack || previousHoverClip != hoverClip)
            repaint();

        return;
    }

    if (position.x < laneArea.getX() && position.y >= laneArea.getY() && position.y < laneArea.getBottom())
    {
        const auto trackIndex = trackAt (position);
        hoverTrack = trackIndex;
        hoverClip = -1;

        const auto overControl = trackIndex >= 0
                              && (headerControl (trackIndex, 1).contains (position)
                               || headerControl (trackIndex, 2).contains (position)
                               || headerControl (trackIndex, 3).contains (position));
        setMouseCursor (overControl ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

        if (previousHoverTrack != hoverTrack || previousHoverClip != hoverClip)
            repaint();

        return;
    }

    if (position.y < rulerArea.getBottom())
    {
        hoverTrack = -1;
        hoverClip = -1;

        if (markerAt (position) >= 0)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

        if (loopArea.contains (position))
        {
            const auto x = static_cast<double> (position.x);
            const auto startX = barToX (loopStartBar);
            const auto endX = barToX (loopEndBar);
            const auto nearHandle = std::abs (x - startX) <= 7.0 || std::abs (x - endX) <= 7.0;

            setMouseCursor (nearHandle ? juce::MouseCursor::LeftRightResizeCursor
                                       : (x > startX && x < endX ? juce::MouseCursor::DraggingHandCursor
                                                                 : juce::MouseCursor::NormalCursor));
        }
        else
        {
            setMouseCursor (juce::MouseCursor::IBeamCursor);
        }

        if (previousHoverTrack != hoverTrack || previousHoverClip != hoverClip)
            repaint();

        return;
    }

    if (laneArea.contains (position))
    {
        hoverTrack = trackAt (position);
        hoverClip = clipAt (position);

        if (tool == 5)
        {
            setMouseCursor (hoverClip >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }
        else if (tool == 4)
        {
            setMouseCursor (hoverClip >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
        }
        else if (tool == 3 && hoverClip >= 0)
        {
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        }
        else if ((tool == 1 || tool == 2) && hoverClip >= 0)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        }
        else if (tool == 0 && hoverClip >= 0)
        {
            const auto& clip = clips[static_cast<size_t> (hoverClip)];
            const auto bounds = clipBounds (clip);
            const auto nearEdge = bounds.getX() + 9.0f > static_cast<float> (position.x)
                               || bounds.getRight() - 9.0f < static_cast<float> (position.x);
            const auto nearFade = fadeHandleAt (clip, position) >= 0;
            setMouseCursor (nearEdge || nearFade ? juce::MouseCursor::LeftRightResizeCursor
                                                 : juce::MouseCursor::DraggingHandCursor);
        }
        else
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
        }
    }
    else
    {
        hoverTrack = -1;
        hoverClip = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    if (previousHoverTrack != hoverTrack || previousHoverClip != hoverClip)
        repaint();
}

void ArrangeView::mouseExit (const juce::MouseEvent&)
{
    if (hoverTrack == -1 && hoverClip == -1)
        return;

    hoverTrack = -1;
    hoverClip = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void ArrangeView::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // Shift (or a horizontal swipe) scrolls the track rows; the plain wheel
    // keeps its existing zoom, so nothing the user learned changes.
    if (event.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const auto rows = visibleTrackRows();
        const auto shown = shownTracks();
        const auto step = juce::jmax (1, rows / 2);
        const auto direction = (wheel.deltaY > 0.0f || wheel.deltaX > 0.0f) ? -1 : 1;

        firstVisibleTrack = juce::jlimit (0, juce::jmax (0, shown - rows),
                                         firstVisibleTrack + direction * step);
        repaint();
        return;
    }

    if (std::abs (wheel.deltaY) < 0.001f)
        return;

    const auto anchorBar = xToBar (static_cast<double> (juce::jlimit (laneArea.getX(), laneArea.getRight(), event.getPosition().x)));
    const auto factor = wheel.deltaY > 0.0f ? 0.82 : 1.22;
    const auto next = juce::jlimit (4, 128, juce::roundToInt (static_cast<float> (visibleBars) * static_cast<float> (factor)));

    if (next == visibleBars)
        return;

    visibleBars = next;

    const auto relative = static_cast<double> (event.getPosition().x - laneArea.getX())
                        / static_cast<double> (juce::jmax (1, laneArea.getWidth()));
    viewStartBar = juce::jmax (1.0, anchorBar - relative * static_cast<double> (visibleBars));
    lastPlayheadX = -1.0f;
    repaint();
}
} // namespace nc
