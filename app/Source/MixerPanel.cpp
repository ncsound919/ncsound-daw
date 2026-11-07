#include "MixerPanel.h"

namespace nc
{
MasterStrip::MasterStrip (AudioEngine& engineToUse)
    : engine (engineToUse)
{
    addAndMakeVisible (fader);
    addAndMakeVisible (leftMeter);
    addAndMakeVisible (rightMeter);
    addAndMakeVisible (insertOne);
    addAndMakeVisible (insertTwo);
    addAndMakeVisible (dimButton);
    addAndMakeVisible (monoButton);
    addAndMakeVisible (abButton);
    addAndMakeVisible (automixButton);

    insertOne.setPluginName ("Overlay Master");
    insertOne.setCompact (true);
    insertOne.setNative (true);
    insertOne.setTooltip ("Baked master rack / click to edit");
    insertOne.onClick = [this]
    {
        if (onRackClicked)
            onRackClicked();
    };

    insertTwo.setCompact (true);
    insertTwo.setEmptyText ("master vst");
    insertTwo.setTooltip ("Master VST insert: click to scan, load or remove a plugin");
    insertTwo.onClick = [this]
    {
        if (onInsertClicked)
            onInsertClicked();
    };

    dimButton.setClickingTogglesState (true);
    dimButton.setCompact (true);
    dimButton.setTooltip ("Dim monitor output by 20 dB");
    dimButton.onClick = [this]
    {
        engine.setMasterDim (dimButton.getToggleState());
        repaint();
    };

    monoButton.setClickingTogglesState (true);
    monoButton.setCompact (true);
    monoButton.setTooltip ("Fold the monitor output to mono");
    monoButton.onClick = [this]
    {
        engine.setMasterMono (monoButton.getToggleState());
        repaint();
    };

    abButton.setCompact (true);
    abButton.setTooltip ("Master A/B: capture and recall two complete master-rack snapshots");
    abButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("MASTER A/B");
        menu.addItem (1, "Capture A");
        menu.addItem (2, "Capture B");
        menu.addItem (3, "Recall A", engine.masterMix().hasAB (0));
        menu.addItem (4, "Recall B", engine.masterMix().hasAB (1));
        menu.addSeparator();
        menu.addItem (5, "Compare (native chain bypass)", true, engine.masterMix().params.compare.load());

        auto safe = juce::Component::SafePointer<MasterStrip> (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&abButton),
                            [safe] (int result)
                            {
                                if (safe == nullptr || result == 0)
                                    return;

                                if (result == 1) safe->engine.masterMix().captureAB (0);
                                else if (result == 2) safe->engine.masterMix().captureAB (1);
                                else if (result == 3) safe->engine.masterMix().recallAB (0);
                                else if (result == 4) safe->engine.masterMix().recallAB (1);
                                else if (result == 5) safe->engine.masterMix().setParam ("compare",
                                    safe->engine.masterMix().params.compare.load() ? 0.0f : 1.0f);

                                safe->repaint();
                            });
    };

    automixButton.setCompact (true);
    automixButton.setClickingTogglesState (true);
    automixButton.setTooltip ("Automix master loudness target and maximizer");
    automixButton.onClick = [this]
    {
        engine.masterMix().setParam ("automixOn", automixButton.getToggleState() ? 1.0f : 0.0f);
        repaint();
    };

    fader.setValue (juce::jlimit (0.0, 1.0, (juce::Decibels::gainToDecibels (engine.masterGain(), -60.0f) + 60.0) / 66.0));
    fader.onChange = [this] (double value)
    {
        const auto db = -60.0 + value * 66.0;
        engine.setMasterGain (juce::Decibels::decibelsToGain (static_cast<float> (db), -60.0f));
        repaint();
    };

    leftMeter.setSource ([this]
    {
        return theme::levelFromGain (engine.masterPeakLeft());
    });
    rightMeter.setSource ([this]
    {
        return theme::levelFromGain (engine.masterPeakRight());
    });
}

void MasterStrip::syncFromEngine()
{
    // The fader is the source of truth while it is being moved; nothing to pull.
    automixButton.setToggleState (engine.masterMix().params.automixOn.load(),
                                  juce::dontSendNotification);
    dimButton.setToggleState (engine.isMasterDimOn(), juce::dontSendNotification);
    monoButton.setToggleState (engine.isMasterMonoOn(), juce::dontSendNotification);
    abButton.setTooltip (engine.masterMix().hasAB (0) && engine.masterMix().hasAB (1)
                             ? juce::String ("Master A/B active: ") + (engine.masterMix().activeAB() == 0 ? "A" : "B")
                             : "Master A/B: capture and recall two complete master-rack snapshots");
    insertTwo.setPluginName (engine.hasMasterPlugin() ? engine.masterPluginName() : juce::String());
    insertTwo.setTooltip (engine.hasMasterPlugin()
                              ? juce::String ("Master VST: ") + engine.masterPluginName() + " / click to manage"
                              : "No master VST / click to scan or load");
}

void MasterStrip::resized()
{
    auto area = getLocalBounds().reduced (10);
    headerArea = area.removeFromTop (64);
    area.removeFromTop (4);
    insertArea = area.removeFromTop (80);

    auto slotRow = insertArea.withTrimmedTop (22);
    insertOne.setBounds (slotRow.removeFromTop (26));
    slotRow.removeFromTop (4);
    insertTwo.setBounds (slotRow.removeFromTop (26));

    buttonArea = area.removeFromBottom (30);
    area.removeFromBottom (30); // readout strip
    area.removeFromBottom (4);

    auto faderBlock = area;
    meterArea = faderBlock.removeFromRight (34);
    faderBlock.removeFromRight (28); // scale labels painted directly
    faderArea = faderBlock;

    fader.setBounds (faderArea.withTrimmedTop (4).withTrimmedBottom (4));

    auto meters = meterArea.withTrimmedTop (8).withTrimmedBottom (8);
    leftMeter.setBounds (meters.removeFromLeft (14));
    meters.removeFromLeft (6);
    rightMeter.setBounds (meters.removeFromLeft (14));

    const auto buttonWidth = (buttonArea.getWidth() - 18) / 4;
    dimButton.setBounds (buttonArea.removeFromLeft (buttonWidth));
    buttonArea.removeFromLeft (6);
    monoButton.setBounds (buttonArea.removeFromLeft (buttonWidth));
    buttonArea.removeFromLeft (6);
    abButton.setBounds (buttonArea.removeFromLeft (buttonWidth));
    buttonArea.removeFromLeft (6);
    automixButton.setBounds (buttonArea);
}

void MasterStrip::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    theme::face (g, area, theme::radius::panel);

    g.setColour (theme::accent.withAlpha (0.55f));
    g.drawRoundedRectangle (area.reduced (0.5f), theme::radius::panel, 1.0f);
    theme::chip (g, area.withHeight (3.0f).reduced (4.0f, 0.0f), theme::accent, 1.0f);

    auto header = headerArea.toFloat();
    g.setColour (theme::muted);
    g.setFont (theme::label (11.0f));
    g.drawText ("MASTER OUTPUT", header.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft, false);

    g.setColour (theme::paper);
    g.setFont (theme::display (18.0f));
    g.drawText ("MASTER", header.removeFromTop (26.0f).toNearestInt(), juce::Justification::centredLeft, false);

    g.setColour (theme::muted);
    g.setFont (theme::label ());    const auto ab = engine.masterMix().hasAB (0) && engine.masterMix().hasAB (1)
                        ? (engine.masterMix().activeAB() == 0 ? "A" : "B")
                        : "A/B";
    auto routing = "OUT 1-2  /  " + juce::String (ab) + "  /  24 BIT";

    if (engine.isMasterMonoOn())
        routing += "  /  MONO";

    if (engine.isMasterDimOn())
        routing += "  /  DIM";

    g.drawText (routing, header.toNearestInt(), juce::Justification::centredLeft, false);

    auto insertTitle = juce::Rectangle<int> (insertArea.getX(), insertArea.getY(), insertArea.getWidth(), 16);
    theme::chip (g, juce::Rectangle<float> (static_cast<float> (insertTitle.getX()), static_cast<float> (insertTitle.getY()) + 5.0f, 6.0f, 6.0f), theme::accent);
    g.setColour (theme::muted);
    g.setFont (theme::label ());
    g.drawText ("MASTER CHAIN", insertTitle.withTrimmedLeft (12), juce::Justification::centredLeft, false);

    // Scale.
    auto scaleTop = faderArea.getY() + 9;
    auto scaleBottom = faderArea.getBottom() - 9;
    const float dbs[] { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };

    for (const auto db : dbs)
    {
        const auto norm = (static_cast<double> (db) + 60.0) / 66.0;
        const auto y = static_cast<float> (scaleBottom) - static_cast<float> (norm) * (static_cast<float> (scaleBottom) - static_cast<float> (scaleTop));
        const auto x = static_cast<float> (faderArea.getRight() + 4);

        g.setColour (theme::line);
        g.drawLine (x, y, x + 5.0f, y, 1.0f);
        g.setColour (db == 0.0f ? theme::paperDim : theme::muted);
        g.setFont (theme::clock (11.0f));
        g.drawText (db > 0.0f ? "+" + juce::String (static_cast<int> (db)) : juce::String (static_cast<int> (db)),
                    juce::Rectangle<int> (static_cast<int> (x + 7.0f), static_cast<int> (y) - 7, 30, 14),
                    juce::Justification::centredLeft, false);
    }

    g.setColour (theme::muted);
    g.setFont (theme::label ());
    g.drawText ("L", juce::Rectangle<int> (meterArea.getX(), meterArea.getY() + 2, 14, 10), juce::Justification::centred, false);
    g.drawText ("R", juce::Rectangle<int> (meterArea.getX() + 20, meterArea.getY() + 2, 14, 10), juce::Justification::centred, false);

    const auto db = theme::dbString (engine.masterPeak());
    auto readout = juce::Rectangle<int> (getLocalBounds().getX() + 10, buttonArea.getY() - 27,
                                         getWidth() - 20, 22);
    g.setColour (theme::wellLo);
    g.fillRoundedRectangle (readout.toFloat(), theme::radius::tight);
    g.setColour (engine.masterPeak() > 0.94f ? theme::danger : theme::accentHot);
    g.setFont (theme::clock (13.0f));
    g.drawText (db + " dBFS", readout, juce::Justification::centred, false);

    g.setColour (theme::muted);
    g.setFont (theme::label ());
    g.drawText ("PEAK", juce::Rectangle<int> (readout.getX(), readout.getY() - 14, readout.getWidth(), 13),
                juce::Justification::centred, false);
}

//==============================================================================
MixerPanel::MixerPanel (AudioEngine& engineToUse)
    : engine (engineToUse), master (engineToUse)
{
    addAndMakeVisible (master);
    master.onRackClicked = [this]
    {
        if (onMasterRackClicked)
            onMasterRackClicked();
    };
    master.onInsertClicked = [this]
    {
        if (onMasterInsertClicked)
            onMasterInsertClicked();
    };

    for (int i = 0; i < AudioEngine::trackCount; ++i)
    {
        auto strip = std::make_unique<ChannelStrip> (engine, i);
        strip->onSelected = [this] (int index)
        {
            setSelectedTrack (index);

            if (onTrackSelected)
                onTrackSelected (index);
        };
        strip->onInsertClicked = [this] (int trackIndex, int slot)
        {
            if (onInsertClicked)
                onInsertClicked (trackIndex, slot);
        };
        strip->onStripClicked = [this] (int trackIndex)
        {
            if (onStripClicked)
                onStripClicked (trackIndex);
        };

        addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }

    setSelectedTrack (0);
}

void MixerPanel::setSelectedTrack (int track)
{
    selectedTrack = juce::jlimit (0, juce::jmax (0, engine.getActiveTrackCount() - 1), track);

    for (auto& strip : strips)
        strip->setSelected (strip->getTrackIndex() == selectedTrack);
}

void MixerPanel::syncFromEngine()
{
    for (auto& strip : strips)
        strip->syncFromEngine();

    master.syncFromEngine();
}

void MixerPanel::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    juce::ColourGradient background (juce::Colour (0xff1b1815), area.getX(), area.getY(),
                                     theme::ink, area.getX(), area.getBottom(), false);
    g.setGradientFill (background);
    g.fillRect (area);

    // A soft top light behind the console.
    juce::ColourGradient light (theme::accent.withAlpha (0.055f), area.getCentreX(), area.getY() - 40.0f,
                                theme::accent.withAlpha (0.0f), area.getCentreX(), area.getY() + 220.0f, true);
    g.setGradientFill (light);
    g.fillRect (area);

    auto header = headerArea;
    g.setColour (theme::accent);
    g.setFont (theme::display (18.0f));
    g.drawText ("MIX CONSOLE", header.removeFromLeft (170), juce::Justification::centredLeft, false);

    g.setColour (theme::muted);
    g.setFont (theme::label ());
    g.drawText (juce::String (shownTrackCount) + " CHANNELS  /  1 MASTER",
                header.withTrimmedLeft (12), juce::Justification::centredLeft, false);

    const auto firstShown = shownTrackCount > 0 ? firstVisibleStrip + 1 : 0;
    const auto lastShown = juce::jmin (shownTrackCount, firstVisibleStrip + visibleStripCount);
    g.drawText ("TRACKS " + juce::String (firstShown) + "-" + juce::String (lastShown)
                    + " OF " + juce::String (shownTrackCount),
                header, juce::Justification::centredRight, false);

    if (! scrollBarArea.isEmpty() && shownTrackCount > visibleStripCount)
    {
        const auto track = scrollBarArea.toFloat();
        g.setColour (theme::wellLo);
        g.fillRoundedRectangle (track, 3.0f);
        g.setColour (theme::line);
        g.drawRoundedRectangle (track.reduced (0.5f), 3.0f, 0.8f);
        g.setColour (theme::accent.withAlpha (draggingScrollThumb ? 0.75f : 0.48f));
        g.fillRoundedRectangle (scrollThumbArea, 3.0f);
    }

    g.setColour (theme::line);
    g.drawHorizontalLine (headerArea.getBottom() + 1, static_cast<float> (headerArea.getX()), static_cast<float> (headerArea.getRight()));
}

void MixerPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 6);
    headerArea = area.removeFromTop (34);
    area.removeFromTop (4);

    auto masterArea = area.removeFromRight (juce::jmin (188, area.getWidth() / 6));
    area.removeFromRight (12);
    master.setBounds (masterArea);

    scrollBarArea = area.removeFromBottom (12);
    area.removeFromBottom (4);

    const auto shown = juce::jlimit (1, AudioEngine::trackCount, engine.getActiveTrackCount());
    const auto fits = juce::jmax (1, area.getWidth() / minStripWidth);
    const auto visible = juce::jmin (shown, fits);
    const auto stripWidth = area.getWidth() / juce::jmax (1, visible);

    shownTrackCount = shown;
    visibleStripCount = visible;
    firstVisibleStrip = juce::jlimit (0, juce::jmax (0, shown - visible), firstVisibleStrip);
    updateScrollThumb();

    for (auto& strip : strips)
        strip->setVisible (false);

    auto row = area;

    for (int i = 0; i < visible; ++i)
    {
        const auto index = firstVisibleStrip + i;

        if (index >= static_cast<int> (strips.size()))
            break;

        auto& strip = *strips[static_cast<size_t> (index)];
        strip.setVisible (true);
        strip.setBounds (row.removeFromLeft (stripWidth).reduced (4, 0));
    }
}

void MixerPanel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto shown = juce::jlimit (1, AudioEngine::trackCount, engine.getActiveTrackCount());
    const auto width = juce::jmax (1, getWidth() - 32 - juce::jmin (188, getWidth() / 6) - 12);
    const auto fits = juce::jmax (1, width / minStripWidth);
    const auto step = wheel.deltaY > 0.0f ? -1 : 1;

    firstVisibleStrip = juce::jlimit (0, juce::jmax (0, shown - juce::jmin (shown, fits)),
                                     firstVisibleStrip + step);
    resized();
    repaint();
}

void MixerPanel::updateScrollThumb()
{
    scrollThumbArea = {};

    if (scrollBarArea.isEmpty() || shownTrackCount <= visibleStripCount)
        return;

    const auto width = static_cast<float> (scrollBarArea.getWidth());
    const auto thumbWidth = juce::jmax (28.0f, width * static_cast<float> (visibleStripCount)
                                                     / static_cast<float> (shownTrackCount));
    const auto span = juce::jmax (0, shownTrackCount - visibleStripCount);
    const auto travel = juce::jmax (0.0f, width - thumbWidth);
    const auto x = scrollBarArea.getX() + (span > 0
                                                ? travel * static_cast<float> (firstVisibleStrip)
                                                      / static_cast<float> (span)
                                                : 0.0f);

    scrollThumbArea = { x, static_cast<float> (scrollBarArea.getY() + 1),
                        thumbWidth, static_cast<float> (scrollBarArea.getHeight() - 2) };
}

void MixerPanel::scrollToX (int x)
{
    if (shownTrackCount <= visibleStripCount || scrollBarArea.isEmpty())
        return;

    const auto span = juce::jmax (0, shownTrackCount - visibleStripCount);
    const auto travel = juce::jmax (1.0f, static_cast<float> (scrollBarArea.getWidth())
                                               - scrollThumbArea.getWidth());
    const auto start = static_cast<float> (x) - scrollBarArea.getX() - scrollThumbArea.getWidth() * 0.5f;
    firstVisibleStrip = juce::jlimit (0, span, juce::roundToInt (start / travel * static_cast<float> (span)));
    updateScrollThumb();
    resized();
    repaint();
}

void MixerPanel::mouseDown (const juce::MouseEvent& event)
{
    if (! scrollBarArea.contains (event.getPosition()))
        return;

    if (scrollThumbArea.contains (event.getPosition().toFloat()))
    {
        draggingScrollThumb = true;
        scrollDragStartX = event.x;
        scrollDragStartIndex = firstVisibleStrip;
    }
    else
    {
        scrollToX (event.x);
    }
}

void MixerPanel::mouseDrag (const juce::MouseEvent& event)
{
    if (! draggingScrollThumb)
        return;

    const auto span = juce::jmax (0, shownTrackCount - visibleStripCount);
    const auto travel = juce::jmax (1.0f, static_cast<float> (scrollBarArea.getWidth())
                                               - scrollThumbArea.getWidth());
    const auto moved = static_cast<float> (event.x - scrollDragStartX) / travel * static_cast<float> (span);

    firstVisibleStrip = juce::jlimit (0, span, scrollDragStartIndex + juce::roundToInt (moved));
    updateScrollThumb();
    resized();
    repaint();
}

void MixerPanel::mouseUp (const juce::MouseEvent&)
{
    if (! draggingScrollThumb)
        return;

    draggingScrollThumb = false;
    repaint (scrollBarArea);
}
} // namespace nc
