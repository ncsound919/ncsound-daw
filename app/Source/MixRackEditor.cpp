#include "MixRackEditor.h"

#include "Session.h"

namespace nc
{
namespace
{
juce::String dbText (float value) { return juce::String (value, 1) + " dB"; }

void configureBipolarDd (Knob& knob, float value, std::function<void (float)> onChange)
{
    knob.setBipolar (true);
    knob.setDefaultValue (0.5);
    knob.setShowValue (true);
    knob.setValue (juce::jlimit (0.0f, 1.0f, (value + 12.0f) / 24.0f));
    knob.setValueText (dbText (value));
    knob.onChange = [&knob, onChange = std::move (onChange)] (double normalised)
    {
        const auto db = -12.0f + static_cast<float> (normalised) * 24.0f;
        knob.setValueText (dbText (db));
        onChange (db);
    };
}
} // namespace

MixRackEditor::MixRackEditor (AudioEngine& engineToUse, int trackIndex)
    : engine (engineToUse), track (trackIndex), master (trackIndex < 0)
{
    setSize (560, 340);

    for (int i = 0; i < knobCount; ++i)
    {
        knobs[static_cast<size_t> (i)].setDiameter (52);
        addAndMakeVisible (knobs[static_cast<size_t> (i)]);
    }

    for (int i = 0; i < toggleCount; ++i)
    {
        auto& button = toggles[static_cast<size_t> (i)];
        button.setCompact (true);
        button.setClickingTogglesState (true);
        addAndMakeVisible (button);
    }

    if (master)
        setupMaster();
    else
        setupTrack();

    refresh();
}

void MixRackEditor::setupTrack()
{
    auto& channel = engine.mixChannel (track);

    knobs[0].setLabel ("LOW");
    knobs[1].setLabel ("MID");
    knobs[2].setLabel ("HIGH");
    knobs[3].setLabel ("COMP");
    knobs[4].setLabel ("SAT");
    knobs[5].setLabel ("OUT");

    configureBipolarDd (knobs[0], channel.params.bands[0].gainDb.load(), [this] (float db)
    {
        engine.mixChannel (track).setParam ("lowGain", db);
    });
    configureBipolarDd (knobs[1], channel.params.bands[1].gainDb.load(), [this] (float db)
    {
        engine.mixChannel (track).setParam ("lmfGain", db);
    });
    configureBipolarDd (knobs[2], channel.params.bands[3].gainDb.load(), [this] (float db)
    {
        engine.mixChannel (track).setParam ("highGain", db);
    });

    knobs[3].setBipolar (false);
    knobs[3].setDefaultValue (0.625);
    knobs[3].setValue (juce::jlimit (0.0f, 1.0f, (channel.params.compThreshDb.load() + 48.0f) / 48.0f));
    knobs[3].setValueText (dbText (channel.params.compThreshDb.load()));
    // compThreshDb is still a direct field.
    knobs[3].onChange = [this] (double normalised)
    {
        const auto db = -48.0f + static_cast<float> (normalised) * 48.0f;
        engine.mixChannel (track).setParam ("compThresh", db);
        knobs[3].setValueText (dbText (db));
    };

    knobs[4].setBipolar (false);
    knobs[4].setDefaultValue (0.0);
    knobs[4].setValue (juce::jlimit (0.0f, 1.0f, channel.params.satDriveDb.load() / 18.0f));
    knobs[4].setValueText (juce::String (channel.params.satDriveDb.load(), 1) + " dB");
    knobs[4].onChange = [this] (double normalised)
    {
        const auto drive = static_cast<float> (normalised) * 18.0f;
        engine.mixChannel (track).setParam ("satDrive", drive);
        knobs[4].setValueText (juce::String (drive, 1) + " dB");
    };

    knobs[5].setBipolar (false);
    knobs[5].setDefaultValue (0.6667);
    knobs[5].setValue (juce::jlimit (0.0f, 1.0f, (channel.params.outputDb.load() + 24.0f) / 36.0f));
    knobs[5].setValueText (dbText (channel.params.outputDb.load()));
    knobs[5].onChange = [this] (double normalised)
    {
        const auto db = -24.0f + static_cast<float> (normalised) * 36.0f;
        engine.mixChannel (track).setParam ("outputDb", db);
        knobs[5].setValueText (dbText (db));
    };

    toggles[0].setButtonText ("STRIP");
    toggles[1].setButtonText ("COMP");
    toggles[2].setButtonText ("SAT");
    toggles[3].setButtonText ("OPEN MIC");

    toggles[0].onClick = [this]
    {
        engine.mixChannel (track).setParam ("stripOn", toggles[0].getToggleState() ? 1.0f : 0.0f);
    };
    toggles[1].onClick = [this]
    {
        engine.mixChannel (track).setParam ("compOn", toggles[1].getToggleState() ? 1.0f : 0.0f);
    };
    toggles[2].onClick = [this]
    {
        engine.mixChannel (track).setParam ("satOn", toggles[2].getToggleState() ? 1.0f : 0.0f);
    };
    toggles[3].onClick = [this]
    {
        engine.mixChannel (track).setParam ("openMicOn", toggles[3].getToggleState() ? 1.0f : 0.0f);
    };

    toggles[3].setEnabled (channel.isVocal());
    toggles[3].setTooltip (channel.isVocal() ? "Open Mic stage active on this vocal track"
                                             : "Open Mic is only inserted on vocal tracks");
}

void MixRackEditor::setupMaster()
{
    auto& rack = engine.masterMix();

    knobs[0].setLabel ("LOW");
    knobs[1].setLabel ("MID");
    knobs[2].setLabel ("HIGH");
    knobs[3].setLabel ("DYN");
    knobs[4].setLabel ("WIDTH");
    knobs[5].setLabel ("CEIL");

    configureBipolarDd (knobs[0], rack.params.bands[0].gainDb.load(), [this] (float db)
    {
        engine.masterMix().setParam ("lowGain", db);
    });
    configureBipolarDd (knobs[1], rack.params.bands[1].gainDb.load(), [this] (float db)
    {
        engine.masterMix().setParam ("midGain", db);
    });
    configureBipolarDd (knobs[2], rack.params.bands[2].gainDb.load(), [this] (float db)
    {
        engine.masterMix().setParam ("highGain", db);
    });

    knobs[3].setBipolar (false);
    knobs[3].setDefaultValue (0.708);
    knobs[3].setValue (juce::jlimit (0.0f, 1.0f, (rack.params.dynBands[0].threshDb.load() + 48.0f) / 48.0f));
    knobs[3].setValueText (dbText (rack.params.dynBands[0].threshDb.load()));
    knobs[3].onChange = [this] (double normalised)
    {
        const auto db = -48.0f + static_cast<float> (normalised) * 48.0f;
        engine.masterMix().setParam ("dynThresh", db);
        knobs[3].setValueText (dbText (db));
    };

    knobs[4].setBipolar (false);
    knobs[4].setDefaultValue (0.5);
    knobs[4].setValue (juce::jlimit (0.0f, 1.0f, rack.params.imgWidth[0].load() * 0.5f));
    knobs[4].setValueText (juce::String (juce::roundToInt (rack.params.imgWidth[0].load() * 100.0f)) + "%");
    knobs[4].onChange = [this] (double normalised)
    {
        const auto width = static_cast<float> (normalised) * 2.0f;
        engine.masterMix().setParam ("width", width);
        knobs[4].setValueText (juce::String (juce::roundToInt (width * 100.0f)) + "%");
    };

    knobs[5].setBipolar (false);
    knobs[5].setDefaultValue (0.833);
    knobs[5].setValue (juce::jlimit (0.0f, 1.0f, (rack.params.ceilingDb.load() + 6.0f) / 6.0f));
    knobs[5].setValueText (dbText (rack.params.ceilingDb.load()));
    knobs[5].onChange = [this] (double normalised)
    {
        const auto db = -6.0f + static_cast<float> (normalised) * 6.0f;
        engine.masterMix().setParam ("ceiling", db);
        knobs[5].setValueText (dbText (db));
    };

    toggles[0].setButtonText ("EQ");
    toggles[1].setButtonText ("DYN");
    toggles[2].setButtonText ("MAX");
    toggles[3].setButtonText ("AUTOMIX");

    toggles[0].onClick = [this]
    {
        engine.masterMix().setParam ("eqOn", toggles[0].getToggleState() ? 1.0f : 0.0f);
    };
    toggles[1].onClick = [this]
    {
        engine.masterMix().setParam ("dynOn", toggles[1].getToggleState() ? 1.0f : 0.0f);
    };
    toggles[2].onClick = [this]
    {
        engine.masterMix().setParam ("maxOn", toggles[2].getToggleState() ? 1.0f : 0.0f);
    };
    toggles[3].onClick = [this]
    {
        engine.masterMix().setParam ("automixOn", toggles[3].getToggleState() ? 1.0f : 0.0f);
    };
}

void MixRackEditor::refresh()
{
    if (master)
    {
        auto& rack = engine.masterMix();
        toggles[0].setToggleState (rack.params.eqOn.load(), juce::dontSendNotification);
        toggles[1].setToggleState (rack.params.dynOn.load(), juce::dontSendNotification);
        toggles[2].setToggleState (rack.params.maxOn.load(), juce::dontSendNotification);
        toggles[3].setToggleState (rack.params.automixOn.load(), juce::dontSendNotification);
        return;
    }

    auto& channel = engine.mixChannel (track);
    toggles[0].setToggleState (channel.params.stripOn.load(), juce::dontSendNotification);
    toggles[1].setToggleState (channel.params.compOn.load(), juce::dontSendNotification);
    toggles[2].setToggleState (channel.params.satOn.load(), juce::dontSendNotification);
    toggles[3].setToggleState (channel.params.openMicOn.load(), juce::dontSendNotification);
}

void MixRackEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (54);

    const auto knobWidth = area.getWidth() / 3;
    const auto knobHeight = 88;

    for (int row = 0; row < 2; ++row)
    {
        auto rowArea = area.removeFromTop (knobHeight);

        for (int column = 0; column < 3; ++column)
        {
            const auto index = row * 3 + column;
            knobs[static_cast<size_t> (index)].setBounds (rowArea.removeFromLeft (knobWidth).reduced (4, 0));
        }
    }

    area.removeFromTop (8);
    auto toggleArea = area.removeFromTop (32);
    const auto toggleWidth = (toggleArea.getWidth() - 18) / toggleCount;

    for (int i = 0; i < toggleCount; ++i)
    {
        toggles[static_cast<size_t> (i)].setBounds (toggleArea.removeFromLeft (toggleWidth));
        toggleArea.removeFromLeft (6);
    }
}

void MixRackEditor::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.fillAll (theme::ink);
    theme::face (g, area.reduced (1.0f), theme::radius::panel);

    g.setColour (theme::accent);
    g.setFont (theme::display (18.0f));
    g.drawText (master ? "MASTER RACK" : "CHANNEL STRIP",
                getLocalBounds().reduced (18).removeFromTop (26),
                juce::Justification::centredLeft, false);

    g.setColour (theme::muted);
    g.setFont (theme::label (10.5f));

    const auto trackName = master
                               ? juce::String ("MASTER BUS")
                               : sessionTracks()[static_cast<size_t> (juce::jlimit (0, AudioEngine::trackCount - 1, track))].name.toUpperCase();
    const auto subtitle = master
                              ? juce::String ("OVERLAY MASTER  /  AUTOMIX  /  A-B")
                              : trackName + "  /  OVERLAY CHANNEL STRIP  /  OPEN MIC";

    g.drawText (subtitle, getLocalBounds().reduced (18).removeFromTop (44).withTrimmedTop (22),
                juce::Justification::centredLeft, false);

    g.setColour (theme::line);
    g.drawHorizontalLine (getLocalBounds().getY() + 56, 18.0f, static_cast<float> (getWidth() - 18));
}
} // namespace nc
