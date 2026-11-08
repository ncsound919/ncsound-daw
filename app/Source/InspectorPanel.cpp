#include "InspectorPanel.h"

namespace nc
{
InspectorPanel::InspectorPanel (AudioEngine& engineToUse)
    : engine (engineToUse)
{
    for (int i = 0; i < 5; ++i)
    {
        auto& slot = insertSlots[static_cast<size_t> (i)];
        addAndMakeVisible (slot);

        if (i == 0)
        {
            slot.setPluginName ("Overlay Channel Strip");
            slot.setNative (true);
            slot.setTooltip ("Baked channel strip and Open Mic / click to edit");
            slot.onClick = [this]
            {
                if (onStripClicked)
                    onStripClicked (trackIndex);
            };
        }
        else
        {
            slot.setEmptyText (i == 1 ? "a  insert" : "insert");
            slot.onClick = [this, i]
            {
                if (onInsertClicked)
                    onInsertClicked (i - 1);
            };
            slot.onBypass = [this, i]
            {
                const auto slotIndex = i - 1;
                const auto bypassed = engine.trackInsertState (trackIndex, slotIndex).bypassed;
                engine.setTrackInsertBypassed (trackIndex, slotIndex, ! bypassed);
            };
        }
    }

    addAndMakeVisible (panKnob);
    addAndMakeVisible (sendAKnob);
    addAndMakeVisible (sendBKnob);
    addAndMakeVisible (widthKnob);
    addAndMakeVisible (gainFader);
    addAndMakeVisible (levelMeter);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);

    panKnob.setBipolar (true);
    panKnob.setDefaultValue (0.5);
    panKnob.onChange = [this] (double value)
    {
        engine.setTrackPan (trackIndex, static_cast<float> (value * 2.0 - 1.0));
        panKnob.setValueText (theme::panString (engine.trackPan (trackIndex)));
    };

    sendAKnob.setShowValue (true);
    sendAKnob.setLabel ("REVERB");
    sendAKnob.onChange = [this] (double value)
    {
        engine.setTrackSendA (trackIndex, static_cast<float> (value));
        sendAKnob.setValueText (juce::String (juce::roundToInt (value * 100.0)) + "%");
    };

    sendBKnob.setShowValue (true);
    sendBKnob.setLabel ("DELAY");
    sendBKnob.onChange = [this] (double value)
    {
        engine.setTrackSendB (trackIndex, static_cast<float> (value));
        sendBKnob.setValueText (juce::String (juce::roundToInt (value * 100.0)) + "%");
    };

    widthKnob.setShowValue (true);
    widthKnob.onChange = [this] (double value)
    {
        engine.setTrackWidth (trackIndex, static_cast<float> (value));
        widthKnob.setValueText (juce::String (juce::roundToInt (value * 100.0)) + "%");
    };

    gainFader.setShowTicks (false);
    gainFader.onChange = [this] (double value)
    {
        engine.setTrackGain (trackIndex, static_cast<float> (value * 1.2));
    };

    levelMeter.setSource ([this]
    {
        return theme::levelFromGain (engine.trackPeak (trackIndex));
    });

    recordButton.setClickingTogglesState (true);
    recordButton.setDanger (true);
    recordButton.setCompact (true);
    recordButton.onClick = [this]
    {
        engine.setRecordArmed (trackIndex, recordButton.getToggleState());
        recordButton.setToggleState (engine.isRecordArmed (trackIndex), juce::dontSendNotification);
    };

    muteButton.setClickingTogglesState (true);
    muteButton.setCompact (true);
    muteButton.onClick = [this]
    {
        engine.setTrackMuted (trackIndex, muteButton.getToggleState());
    };

    soloButton.setClickingTogglesState (true);
    soloButton.setCompact (true);
    soloButton.onClick = [this]
    {
        engine.setTrackSolo (trackIndex, soloButton.getToggleState());
    };

    setTrack (0);
}

auto InspectorPanel::track() const -> const TrackSpec&
{
    return sessionTracks()[static_cast<size_t> (nc::limits::clampTrack (trackIndex, AudioEngine::trackCount))];
}

void InspectorPanel::setTrack (int newTrack)
{
    trackIndex = nc::limits::clampTrack (newTrack, AudioEngine::trackCount);
    const auto& spec = track();

    for (int i = 0; i < 5; ++i)
    {
        auto& slot = insertSlots[static_cast<size_t> (i)];
        slot.setAccent (spec.tint);

        if (i == 0)
        {
            slot.setPluginName ("Overlay Channel Strip");
        }
        else
        {
            const auto slotIndex = i - 1;
            const auto name = engine.trackInsertName (trackIndex, slotIndex);
            slot.setPluginName (name.isEmpty() ? userInsertAt (spec, slotIndex) : name);
            slot.setBypassed (engine.trackInsertState (trackIndex, slotIndex).bypassed);
            slot.setTooltip (engine.trackInsertIsMissing (trackIndex, slotIndex)
                                 ? "MISSING / " + name
                                 : name);
            slot.setEmptyText (name.isEmpty() ? (slotIndex == 0 ? "a  insert" : "insert") : juce::String());
        }

        if (i == 0)
        {
            slot.setNative (true);
            slot.setTooltip ("Baked channel strip and Open Mic / click to edit");
        }
    }

    panKnob.setValue ((engine.trackPan (trackIndex) + 1.0) * 0.5);
    panKnob.setValueText (theme::panString (engine.trackPan (trackIndex)));

    sendAKnob.setValue (engine.trackSendA (trackIndex));
    sendAKnob.setValueText (juce::String (juce::roundToInt (engine.trackSendA (trackIndex) * 100.0f)) + "%");

    sendBKnob.setValue (engine.trackSendB (trackIndex));
    sendBKnob.setValueText (juce::String (juce::roundToInt (engine.trackSendB (trackIndex) * 100.0f)) + "%");

    widthKnob.setValue (engine.trackWidth (trackIndex));
    widthKnob.setValueText (juce::String (juce::roundToInt (engine.trackWidth (trackIndex) * 100.0f)) + "%");

    gainFader.setValue (juce::jlimit (0.0, 1.0, engine.trackGain (trackIndex) / 1.2));
    recordButton.setToggleState (engine.isRecordArmed (trackIndex), juce::dontSendNotification);
    muteButton.setToggleState (engine.trackMuted (trackIndex), juce::dontSendNotification);
    soloButton.setToggleState (engine.trackSolo (trackIndex), juce::dontSendNotification);

    repaint();
}

void InspectorPanel::syncFromEngine()
{
    const auto& spec = track();

    for (int insert = 1; insert < 5; ++insert)
    {
        const auto slotIndex = insert - 1;
        const auto name = engine.trackInsertName (trackIndex, slotIndex);
        auto& slot = insertSlots[static_cast<size_t> (insert)];
        slot.setPluginName (name.isEmpty() ? userInsertAt (spec, slotIndex) : name);
        slot.setBypassed (engine.trackInsertState (trackIndex, slotIndex).bypassed);
        slot.setTooltip (engine.trackInsertIsMissing (trackIndex, slotIndex)
                             ? "MISSING / " + name
                             : name);
    }

    recordButton.setToggleState (engine.isRecordArmed (trackIndex), juce::dontSendNotification);
    muteButton.setToggleState (engine.trackMuted (trackIndex), juce::dontSendNotification);
    soloButton.setToggleState (engine.trackSolo (trackIndex), juce::dontSendNotification);
    sendAKnob.setValue (engine.trackSendA (trackIndex));
    sendAKnob.setValueText (juce::String (juce::roundToInt (engine.trackSendA (trackIndex) * 100.0f)) + "%");
    sendBKnob.setValue (engine.trackSendB (trackIndex));
    sendBKnob.setValueText (juce::String (juce::roundToInt (engine.trackSendB (trackIndex) * 100.0f)) + "%");
    widthKnob.setValue (engine.trackWidth (trackIndex));
    widthKnob.setValueText (juce::String (juce::roundToInt (engine.trackWidth (trackIndex) * 100.0f)) + "%");
}

void InspectorPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    const auto compact = area.getHeight() < 600;

    headerArea = area.removeFromTop (compact ? 50 : 54);

    // R/M/S live in the header so the channel section can give its whole lower
    // half to the gain fader instead of stacking the fader out of existence.
    auto headerActions = headerArea.withTrimmedTop (4).withHeight (24).removeFromRight (78);
    const auto actionWidth = (headerActions.getWidth() - 6) / 3;
    recordButton.setBounds (headerActions.removeFromLeft (actionWidth));
    headerActions.removeFromLeft (3);
    muteButton.setBounds (headerActions.removeFromLeft (actionWidth));
    headerActions.removeFromLeft (3);
    soloButton.setBounds (headerActions);

    area.removeFromTop (3);
    instrumentArea = area.removeFromTop (compact ? 38 : 56);
    area.removeFromTop (3);
    insertsArea = area.removeFromTop (compact ? 116 : 136);
    area.removeFromTop (3);
    sendsArea = area.removeFromTop (compact ? 80 : 94);
    area.removeFromTop (3);

    // On a 1366 x 768 panel the inspector has to give up the redundant output
    // strip before it can keep a usable fader.
    outputArea = compact ? juce::Rectangle<int>() : area.removeFromBottom (26);
    channelArea = area;

    panKnob.setDiameter (compact ? 42 : 48);
    widthKnob.setDiameter (compact ? 40 : 44);
    sendAKnob.setDiameter (compact ? 32 : 46);
    sendBKnob.setDiameter (compact ? 32 : 46);

    auto insertInner = insertsArea.withTrimmedTop (18);

    for (auto& slot : insertSlots)
    {
        slot.setBounds (insertInner.removeFromTop (compact ? 18 : 22));
        insertInner.removeFromTop (2);
    }

    auto sendsInner = sendsArea.withTrimmedTop (18);
    sendAKnob.setBounds (sendsInner.removeFromLeft (sendsInner.getWidth() / 2)
                             .withSizeKeepingCentre (compact ? 60 : 70, compact ? 60 : 76));
    sendBKnob.setBounds (sendsInner.withSizeKeepingCentre (compact ? 60 : 70, compact ? 60 : 76));

    // PAN and WIDTH centre on the whole channel panel, not on the slice left
    // over by the fader. The fader gets its own centred row underneath; the
    // R/M/S buttons moved to the header to make room for it.
    auto channelInner = channelArea.withTrimmedTop (18);
    channelInner.removeFromBottom (2);

    auto knobRow = channelInner.removeFromTop (compact ? 70 : 78);
    knobRow.reduce (juce::jmin (8, knobRow.getWidth() / 12), 0);
    panKnob.setBounds (knobRow.removeFromLeft (knobRow.getWidth() / 2)
                           .withSizeKeepingCentre (compact ? 66 : 72, compact ? 72 : 78));
    widthKnob.setBounds (knobRow.withSizeKeepingCentre (compact ? 66 : 72, compact ? 72 : 78));

    auto faderRow = channelInner;
    auto faderGroup = faderRow.withSizeKeepingCentre (compact ? 116 : 142, faderRow.getHeight());
    gainLabelArea = faderGroup.removeFromLeft (compact ? 46 : 54);
    faderGroup.removeFromLeft (compact ? 6 : 8);
    const auto faderWidth = compact ? 42 : 48;
    gainFader.setBounds (faderGroup.removeFromLeft (faderWidth).withTrimmedTop (2).withTrimmedBottom (2));
    faderGroup.removeFromLeft (compact ? 8 : 12);
    levelMeter.setBounds (faderGroup.removeFromLeft (compact ? 14 : 16));
}

void InspectorPanel::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    juce::ColourGradient background (theme::panel, area.getX(), area.getY(),
                                     juce::Colour (0xff1b1b1b), area.getX(), area.getBottom(), false);
    g.setGradientFill (background);
    g.fillRect (area);
    g.setTiledImageFill (theme::grainTexture(), 0, 0, 0.45f);
    g.fillRect (area);

    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawVerticalLine (0, 0.0f, area.getBottom());
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawVerticalLine (1, 0.0f, area.getBottom());

    const auto& spec = track();

    // The selected-channel frame follows the track identity, so the inspector
    // and the strip beside it read as the same channel.
    g.setColour (spec.tint.withAlpha (0.68f));
    g.drawRoundedRectangle (area.reduced (1.5f), theme::radius::panel, 1.5f);
    g.setColour (spec.tint.withAlpha (0.16f));
    g.fillRoundedRectangle (area.withWidth (3.0f), 1.5f);

    // Header.
    auto header = headerArea.toFloat();
    theme::chip (g, header.removeFromLeft (3.0f).withTrimmedTop (4.0f).withTrimmedBottom (4.0f), spec.tint);

    auto headerText = header.withTrimmedLeft (12.0f);
    auto headerLabel = headerText.removeFromTop (17.0f).withTrimmedRight (86.0f);
    g.setColour (theme::muted);
    g.setFont (theme::label (10.0f));
    g.drawText ("SELECTED CHANNEL", headerLabel.toNearestInt(), juce::Justification::centredLeft, false);

    auto numberArea = headerText.removeFromLeft (52.0f);
    g.setColour (spec.tint);
    g.setFont (theme::clock (28.0f));
    g.drawText (juce::String (trackIndex + 1).paddedLeft ('0', 2), numberArea.toNearestInt(), juce::Justification::centredLeft, false);

    auto nameArea = headerText;
    g.setColour (theme::paper);
    g.setFont (theme::display (22.0f));
    g.drawText (spec.name.toUpperCase(), nameArea.removeFromTop (27.0f).toNearestInt(), juce::Justification::centredLeft, false);

    g.setColour (theme::muted);
    g.setFont (theme::label ());
    g.drawText (spec.kind + "  /  STEREO", nameArea.toNearestInt(), juce::Justification::centredLeft, false);

    g.setColour (theme::line);
    g.drawHorizontalLine (headerArea.getBottom() + 3, 12.0f, area.getRight() - 12.0f);

    auto sectionTitle = [&g] (juce::Rectangle<int> section, const juce::String& title, juce::Colour chipColour)
    {
        auto row = section.removeFromTop (20);
        theme::chip (g, juce::Rectangle<float> (row.getX(), row.getY() + 7.0f, 7.0f, 7.0f), chipColour);
        g.setColour (theme::paperDim);
        g.setFont (theme::label (10.0f));
        g.drawText (title, row.withTrimmedLeft (13).toNearestInt(), juce::Justification::centredLeft, false);
        g.setColour (theme::line);
        g.drawHorizontalLine (row.getBottom(), static_cast<float> (row.getX()), static_cast<float> (row.getRight()));
    };

    sectionTitle (instrumentArea, "INSTRUMENT", theme::mod);
    sectionTitle (insertsArea, "INSERTS", theme::accent);
    sectionTitle (sendsArea, "SENDS", theme::mod);
    sectionTitle (channelArea, "CHANNEL", theme::accent);

    // Instrument face.
    auto instrument = instrumentArea.withTrimmedTop (20).toFloat();
    theme::wellFace (g, instrument, 3.0f);

    auto instrumentInner = instrument.reduced (10.0f, 3.0f);

    if (instrumentInner.getHeight() >= 26.0f)
    {
        g.setColour (theme::paper);
        g.setFont (theme::ui (14.0f, true));
        g.drawText (spec.instrument, instrumentInner.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft, false);

        g.setColour (theme::muted);
        g.setFont (theme::ui (12.0f));
        g.drawText (spec.preset, instrumentInner.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft, false);
    }
    else
    {
        // At the compact height there is only one text row. Keep the preset
        // visible beside the instrument name instead of dropping it entirely.
        auto presetArea = instrumentInner.removeFromRight (juce::jmin (84.0f, instrumentInner.getWidth() * 0.46f));
        g.setColour (theme::paper);
        g.setFont (theme::ui (13.5f, true));
        g.drawText (spec.instrument, instrumentInner.toNearestInt(), juce::Justification::centredLeft, true);
        g.setColour (theme::muted);
        g.setFont (theme::ui (10.5f));
        g.drawText (spec.preset, presetArea.toNearestInt(), juce::Justification::centredRight, true);
    }

    // Channel gain fader readout.
    if (! gainLabelArea.isEmpty())
    {
        auto block = gainLabelArea.withSizeKeepingCentre (gainLabelArea.getWidth(), 32).toFloat();
        g.setColour (theme::muted);
        g.setFont (theme::label (10.0f));
        g.drawText ("GAIN", block.removeFromTop (14.0f).toNearestInt(), juce::Justification::centredLeft, false);
        g.setColour (theme::accentHot);
        g.setFont (theme::clock (11.0f));
        g.drawText (theme::dbString (engine.trackGain (trackIndex)), block.removeFromTop (15.0f).toNearestInt(),
                    juce::Justification::centredLeft, false);
    }

    // Output row: the gain readout is real, the sample rate lives in the header.
    if (outputArea.isEmpty())
        return;

    const auto db = theme::dbString (engine.trackGain (trackIndex));
    g.setColour (theme::muted);
    g.setFont (theme::label ());
    g.drawText ("OUTPUT", juce::Rectangle<int> (outputArea.getX(), outputArea.getY(), 72, 18),
                juce::Justification::centredLeft, false);
    g.setColour (theme::paperDim);
    g.setFont (theme::ui (12.5f, true));
    g.drawText ("MASTER  /  STEREO", juce::Rectangle<int> (outputArea.getX() + 72, outputArea.getY(), 170, 18),
                juce::Justification::centredLeft, false);
    g.setColour (theme::accentHot);
    g.setFont (theme::clock (13.0f));
    g.drawText (db + " dB", outputArea, juce::Justification::centredRight, false);
}

} // namespace nc
