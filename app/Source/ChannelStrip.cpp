#include "ChannelStrip.h"

namespace nc
{
ChannelStrip::ChannelStrip (AudioEngine& engineToUse, int trackIndex)
    : engine (engineToUse), index (trackIndex)
{
    const auto& trackSpec = spec();

    for (int i = 0; i < 5; ++i)
    {
        auto& slot = insertSlots[static_cast<size_t> (i)];
        addAndMakeVisible (slot);
        slot.setAccent (trackSpec.tint);
        slot.setCompact (true);

        if (i == 0)
        {
            // The baked strip is the first insert on every track. It is not a
            // hosted plugin, so it opens the native rack instead of the browser.
            slot.setPluginName ("Overlay Channel Strip");
            slot.setNative (true);
            slot.setEmptyText ({});
            slot.setTooltip ("Baked channel strip and Open Mic / click to edit");
            slot.onClick = [this]
            {
                if (onStripClicked)
                    onStripClicked (index);
            };
        }
        else
        {
            slot.setEmptyText (i == 1 ? "a  insert" : "insert");
            slot.setPluginName (userInsertAt (trackSpec, i - 1));
            slot.onClick = [this, i]
            {
                if (onInsertClicked)
                    onInsertClicked (index, i - 1);
            };
            slot.onBypass = [this, i]
            {
                const auto slotIndex = i - 1;
                const auto bypassed = engine.trackInsertState (index, slotIndex).bypassed;
                engine.setTrackInsertBypassed (index, slotIndex, ! bypassed);
            };
        }
    }

    addAndMakeVisible (panKnob);
    addAndMakeVisible (sendAKnob);
    addAndMakeVisible (sendBKnob);
    addAndMakeVisible (fader);
    addAndMakeVisible (meter);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);

    panKnob.setBipolar (true);
    panKnob.setDefaultValue (0.5);
    panKnob.setValue ((engine.trackPan (index) + 1.0) * 0.5);
    panKnob.setValueText (theme::panString (engine.trackPan (index)));
    panKnob.onChange = [this] (double value)
    {
        engine.setTrackPan (index, static_cast<float> (value * 2.0 - 1.0));
        panKnob.setValueText (theme::panString (engine.trackPan (index)));
    };

    sendA = engine.trackSendA (index);
    sendB = engine.trackSendB (index);
    sendAKnob.setValue (sendA);
    sendBKnob.setValue (sendB);
    sendAKnob.setShowValue (true);
    sendBKnob.setShowValue (true);
    sendAKnob.setValueText (juce::String (juce::roundToInt (sendA * 100.0f)) + "%");
    sendBKnob.setValueText (juce::String (juce::roundToInt (sendB * 100.0f)) + "%");

    sendAKnob.onChange = [this] (double value)
    {
        engine.setTrackSendA (index, static_cast<float> (value));
        sendA = static_cast<float> (value);
        sendAKnob.setValueText (juce::String (juce::roundToInt (value * 100.0)) + "%");
    };

    sendBKnob.onChange = [this] (double value)
    {
        engine.setTrackSendB (index, static_cast<float> (value));
        sendB = static_cast<float> (value);
        sendBKnob.setValueText (juce::String (juce::roundToInt (value * 100.0)) + "%");
    };

    fader.setValue (faderValueForGain (engine.trackGain (index)));
    fader.onChange = [this] (double value)
    {
        const auto db = -60.0 + value * 66.0;
        engine.setTrackGain (index, juce::Decibels::decibelsToGain (static_cast<float> (db), -60.0f));
        repaint();
    };

    meter.setSource ([this]
    {
        return theme::levelFromGain (engine.trackPeak (index));
    });

    recordButton.setCompact (true);
    recordButton.setDanger (true);
    recordButton.setClickingTogglesState (true);
    recordButton.onClick = [this]
    {
        armed = recordButton.getToggleState();
    };

    muteButton.setCompact (true);
    muteButton.setClickingTogglesState (true);
    muteButton.onClick = [this]
    {
        engine.setTrackMuted (index, muteButton.getToggleState());
    };

    soloButton.setCompact (true);
    soloButton.setClickingTogglesState (true);
    soloButton.onClick = [this]
    {
        engine.setTrackSolo (index, soloButton.getToggleState());
    };

    muteButton.setToggleState (engine.trackMuted (index), juce::dontSendNotification);
    soloButton.setToggleState (engine.trackSolo (index), juce::dontSendNotification);
}

auto ChannelStrip::spec() const -> const TrackSpec&
{
    return sessionTracks()[static_cast<size_t> (nc::limits::clampTrack (index, AudioEngine::trackCount))];
}

float ChannelStrip::faderValueForGain (float gain) const
{
    const auto db = juce::Decibels::gainToDecibels (gain, -60.0f);
    return static_cast<float> (juce::jlimit (0.0, 1.0, (static_cast<double> (db) + 60.0) / 66.0));
}

void ChannelStrip::setSelected (bool shouldBeSelected)
{
    selected = shouldBeSelected;
    repaint();
}

void ChannelStrip::syncFromEngine()
{
    muteButton.setToggleState (engine.trackMuted (index), juce::dontSendNotification);
    soloButton.setToggleState (engine.trackSolo (index), juce::dontSendNotification);
    sendA = engine.trackSendA (index);
    sendB = engine.trackSendB (index);
    sendAKnob.setValue (sendA);
    sendAKnob.setValueText (juce::String (juce::roundToInt (sendA * 100.0f)) + "%");
    sendBKnob.setValue (sendB);
    sendBKnob.setValueText (juce::String (juce::roundToInt (sendB * 100.0f)) + "%");

    const auto& trackSpec = spec();

    for (int insert = 1; insert < 5; ++insert)
    {
        const auto slotIndex = insert - 1;
        const auto name = engine.trackInsertName (index, slotIndex);
        auto& slot = insertSlots[static_cast<size_t> (insert)];
        slot.setPluginName (name.isEmpty() ? userInsertAt (trackSpec, slotIndex) : name);
        slot.setBypassed (engine.trackInsertState (index, slotIndex).bypassed);
        slot.setTooltip (engine.trackInsertIsMissing (index, slotIndex)
                             ? "MISSING / " + name
                             : name);
        slot.setEmptyText (name.isEmpty() ? (slotIndex == 0 ? "a  insert" : "insert") : juce::String());
    }
}

void ChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (9);
    nameArea = area.removeFromTop (50);
    area.removeFromTop (4);

    insertArea = area.removeFromTop (138);
    auto slotArea = insertArea.withTrimmedTop (18);

    for (auto& slot : insertSlots)
    {
        slot.setBounds (slotArea.removeFromTop (22));
        slotArea.removeFromTop (2);
    }

    area.removeFromTop (6);
    sendArea = area.removeFromTop (92);
    auto sendInner = sendArea.withTrimmedTop (18);
    sendAKnob.setBounds (sendInner.removeFromLeft (sendInner.getWidth() / 2).withSizeKeepingCentre (68, 74));
    sendBKnob.setBounds (sendInner.withSizeKeepingCentre (68, 74));

    area.removeFromTop (4);
    panArea = area.removeFromTop (76);

    // Pan and fader share the row. Stacking them left the fader a negative
    // height once the larger knobs and type took their space.
    auto panColumn = panArea;
    auto faderColumn = panColumn.removeFromRight (50);
    panKnob.setBounds (panColumn.withSizeKeepingCentre (60, 72));

    faderArea = faderColumn.removeFromLeft (40);
    faderColumn.removeFromLeft (2);
    meterArea = faderColumn.removeFromLeft (8);
    fader.setBounds (faderArea.withTrimmedTop (2).withTrimmedBottom (2));
    meter.setBounds (meterArea);

    buttonArea = area.removeFromBottom (30);
    area.removeFromBottom (30); // dB readout strip

    const auto buttonWidth = (buttonArea.getWidth() - 8) / 3;
    recordButton.setBounds (buttonArea.removeFromLeft (buttonWidth));
    buttonArea.removeFromLeft (4);
    muteButton.setBounds (buttonArea.removeFromLeft (buttonWidth));
    buttonArea.removeFromLeft (4);
    soloButton.setBounds (buttonArea);
}

void ChannelStrip::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto& trackSpec = spec();

    if (selected)
    {
        g.setColour (trackSpec.tint.withAlpha (0.12f));
        g.fillRoundedRectangle (area.expanded (2.0f), theme::radius::panel);
    }

    theme::face (g, area, theme::radius::panel);

    if (selected)
    {
        g.setColour (trackSpec.tint.withAlpha (0.92f));
        g.drawRoundedRectangle (area.reduced (0.5f), theme::radius::panel, 1.5f);
    }
    else if (hovering)
    {
        g.setColour (theme::paper.withAlpha (0.18f));
        g.drawRoundedRectangle (area.reduced (0.5f), theme::radius::panel, 1.0f);
    }

    // Name band.
    auto name = nameArea.toFloat();
    theme::chip (g, name.removeFromTop (2.0f), trackSpec.tint, 1.0f);

    auto number = name.removeFromLeft (34.0f).withTrimmedTop (3.0f);
    g.setColour (selected ? trackSpec.tint.brighter (0.35f) : trackSpec.tint);
    g.setFont (theme::clock (16.0f));
    g.drawText (juce::String (index + 1).paddedLeft ('0', 2), number.toNearestInt(), juce::Justification::centredLeft, false);

    auto label = name.withTrimmedTop (2.0f);
    g.setColour (theme::paper);
    g.setFont (theme::display (16.0f));
    g.drawText (trackSpec.name.toUpperCase(), label.removeFromTop (19.0f).toNearestInt(), juce::Justification::centredLeft, false);

    g.setColour (theme::muted);
    g.setFont (theme::label ());
    g.drawText (trackSpec.kind, label.removeFromTop (13.0f).toNearestInt(), juce::Justification::centredLeft, false);

    g.setColour (theme::line);
    g.drawHorizontalLine (nameArea.getBottom(), static_cast<float> (nameArea.getX()), static_cast<float> (nameArea.getRight()));

    auto caption = [&g] (juce::Rectangle<int> section, const juce::String& title, juce::Colour colour)
    {
        auto row = section.removeFromTop (18);
        theme::chip (g, juce::Rectangle<float> (row.getX(), row.getY() + 6.0f, 7.0f, 7.0f), colour);
        g.setColour (theme::muted);
        g.setFont (theme::label ());
        g.drawText (title, row.withTrimmedLeft (13).toNearestInt(), juce::Justification::centredLeft, false);
    };

    caption (insertArea, "INSERTS", theme::accent);
    caption (sendArea, "SENDS", theme::mod);

    // Pan caption is handled by the knob.

    // dB readout. The strip fader is too short for a scale; the readout carries it.
    const auto db = theme::dbString (engine.trackGain (index));
    auto readout = juce::Rectangle<int> (getLocalBounds().getX() + 9, buttonArea.getY() - 27,
                                         getWidth() - 18, 21);
    g.setColour (theme::wellLo);
    g.fillRoundedRectangle (readout.toFloat(), theme::radius::tight);
    g.setColour (theme::accentHot.withAlpha (0.95f));
    g.setFont (theme::clock (13.0f));
    g.drawText (db + " dB", readout, juce::Justification::centred, false);

}

void ChannelStrip::mouseDown (const juce::MouseEvent&)
{
    if (onSelected)
        onSelected (index);
}

void ChannelStrip::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    repaint();
}

void ChannelStrip::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}
} // namespace nc
