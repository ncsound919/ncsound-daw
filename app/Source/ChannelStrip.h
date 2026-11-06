#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Theme.h"

namespace nc
{
class ChannelStrip final : public juce::Component
{
public:
    ChannelStrip (AudioEngine& engineToUse, int trackIndex, juce::String trackName)
        : engine (engineToUse), index (trackIndex), name (std::move (trackName))
    {
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setValue (juce::Decibels::gainToDecibels (engine.trackGain (index), -60.0f), juce::dontSendNotification);
        fader.setColour (juce::Slider::trackColourId, theme::accent);
        fader.setColour (juce::Slider::backgroundColourId, theme::bg);
        fader.onValueChange = [this]
        {
            engine.setTrackGain (index, static_cast<float> (juce::Decibels::decibelsToGain (fader.getValue())));
        };
        addAndMakeVisible (fader);

        pan.setSliderStyle (juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setValue (engine.trackPan (index), juce::dontSendNotification);
        pan.setColour (juce::Slider::trackColourId, theme::textDim);
        pan.setColour (juce::Slider::backgroundColourId, theme::bg);
        pan.onValueChange = [this] { engine.setTrackPan (index, static_cast<float> (pan.getValue())); };
        addAndMakeVisible (pan);

        styleToggle (mute, theme::gold);
        mute.onClick = [this] { engine.setTrackMuted (index, mute.getToggleState()); };
        styleToggle (solo, theme::sky);
        solo.onClick = [this] { engine.setTrackSolo (index, solo.getToggleState()); };
        styleToggle (record, theme::red);
    }

    void refresh()
    {
        if (const auto value = engine.trackPeak (index); std::abs (value - peak) > 0.001f)
        {
            peak = value;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        g.setColour (theme::panel);
        g.fillRect (area);
        g.setColour (theme::border);
        g.drawRect (area, 1);

        auto inner = area.reduced (4);
        auto top = inner.removeFromTop (18);
        g.setColour (theme::textMuted);
        g.setFont (theme::label (8.0f));
        g.drawText ("INSERTS", top, juce::Justification::centredLeft, false);

        for (int slot = 0; slot < 3; ++slot)
        {
            auto r = inner.removeFromTop (13).reduced (0, 1);
            g.setColour (theme::bg);
            g.fillRect (r);
            g.setColour (theme::border);
            g.drawRect (r, 1);
            g.setColour (theme::textMuted);
            g.setFont (theme::ui (8.0f));
            g.drawText ("a  insert", r.reduced (4, 0), juce::Justification::centredLeft, false);
        }

        auto sends = inner.removeFromTop (34).reduced (0, 4);
        g.setColour (theme::bg);
        g.fillRect (sends);
        g.setColour (theme::border);
        g.drawRect (sends, 1);
        g.setColour (theme::textMuted);
        g.setFont (theme::label (7.5f));
        g.drawText ("SENDS A-E", sends.reduced (4, 2), juce::Justification::topLeft, false);

        auto io = inner.removeFromTop (14);
        g.setColour (theme::textMuted);
        g.setFont (theme::ui (8.0f));
        g.drawText ("I/O  A 1-2", io, juce::Justification::centredLeft, false);

        auto meterArea = inner.removeFromRight (7).withTrimmedTop (4).withTrimmedBottom (4);
        auto nameBand = inner.removeFromBottom (18);
        auto buttons = inner.removeFromBottom (18);

        g.setColour (theme::bg);
        g.fillRect (meterArea);
        const auto level = juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (peak, -60.0f) / 60.0f + 1.0f);
        auto lit = meterArea.withTrimmedTop (static_cast<int> ((1.0f - level) * static_cast<float> (meterArea.getHeight())));
        g.setColour (level > 0.92f ? theme::red : theme::accent);
        g.fillRect (lit);

        g.setColour (theme::bg);
        g.fillRect (nameBand);
        g.setColour (theme::accent);
        g.fillRect (nameBand.removeFromTop (2));
        g.setColour (theme::text);
        g.setFont (theme::label (8.5f));
        g.drawText (name, nameBand, juce::Justification::centred, false);

        juce::ignoreUnused (buttons);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4);
        area.removeFromTop (18 + 3 * 13);
        area.removeFromTop (34);
        area.removeFromTop (14);

        auto buttons = area.removeFromBottom (18);
        auto nameBand = area.removeFromBottom (18);
        juce::ignoreUnused (nameBand);

        const auto widths = buttons.getWidth() / 3;
        record.setBounds (buttons.removeFromLeft (widths).reduced (1));
        mute.setBounds (buttons.removeFromLeft (widths).reduced (1));
        solo.setBounds (buttons.reduced (1));

        area.removeFromTop (4);
        auto meter = area.removeFromRight (9);
        juce::ignoreUnused (meter);
        fader.setBounds (area);
        pan.setBounds (area.removeFromTop (0));
    }

private:
    static void styleToggle (juce::TextButton& button, juce::Colour on)
    {
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonColourId, theme::bg);
        button.setColour (juce::TextButton::buttonOnColourId, on);
        button.setColour (juce::TextButton::textColourOffId, theme::textMuted);
        button.setColour (juce::TextButton::textColourOnId, theme::bg);
    }

    AudioEngine& engine;
    int index;
    juce::String name;

    juce::Slider fader;
    juce::Slider pan;
    juce::TextButton record { "R" };
    juce::TextButton mute { "M" };
    juce::TextButton solo { "S" };

    float peak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};
} // namespace nc
