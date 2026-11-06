#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"

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
        fader.onValueChange = [this]
        {
            engine.setTrackGain (index, static_cast<float> (juce::Decibels::decibelsToGain (fader.getValue())));
        };
        addAndMakeVisible (fader);

        pan.setSliderStyle (juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setValue (engine.trackPan (index), juce::dontSendNotification);
        pan.onValueChange = [this] { engine.setTrackPan (index, static_cast<float> (pan.getValue())); };
        addAndMakeVisible (pan);

        mute.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc25a2a));
        mute.onClick = [this] { engine.setTrackMuted (index, mute.getToggleState()); };
        addAndMakeVisible (mute);

        solo.setClickingTogglesState (true);
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc9a227));
        solo.onClick = [this] { engine.setTrackSolo (index, solo.getToggleState()); };
        addAndMakeVisible (solo);
    }

    void refresh()
    {
        const auto newPeak = engine.trackPeak (index);

        if (std::abs (newPeak - peak) > 0.001f)
        {
            peak = newPeak;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xff181818));
        g.fillRoundedRectangle (area, 4.0f);

        g.setColour (juce::Colour (0xfff8e8c8));
        g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f).withStyle ("Bold")));
        g.drawText (name, getLocalBounds().reduced (6, 0).removeFromTop (18), juce::Justification::centred, false);

        auto meter = getLocalBounds().reduced (6, 0).removeFromRight (8).withTrimmedTop (60).withTrimmedBottom (34);
        g.setColour (juce::Colour (0xff222222));
        g.fillRect (meter);

        const auto level = juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (peak, -60.0f) / 60.0f + 1.0f);
        auto lit = meter.withTrimmedTop (static_cast<int> ((1.0f - level) * static_cast<float> (meter.getHeight())));

        g.setColour (level > 0.92f ? juce::Colour (0xffc25a2a) : juce::Colour (0xff6f8f6f));
        g.fillRect (lit);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 0);
        area.removeFromTop (22);

        pan.setBounds (area.removeFromTop (20));
        area.removeFromTop (6);

        auto buttons = area.removeFromBottom (22);
        mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 2));
        buttons.removeFromLeft (4);
        solo.setBounds (buttons);

        area.removeFromBottom (4);
        fader.setBounds (area.withTrimmedRight (12));
    }

private:
    AudioEngine& engine;
    int index;
    juce::String name;

    juce::Slider fader;
    juce::Slider pan;
    juce::TextButton mute { "M" };
    juce::TextButton solo { "S" };

    float peak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};
} // namespace nc
