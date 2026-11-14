#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
const auto ink = juce::Colour (0xff181818);
const auto surface = juce::Colour (0xff222222);
const auto paper = juce::Colour (0xfff8e8c8);
const auto accent = juce::Colour (0xffc25a2a);
} // namespace

ChopStationEditor::ChopStationEditor (nc::ChopStationProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    if (auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::BankGothic_ttf, BinaryData::BankGothic_ttfSize))
        getLookAndFeel().setDefaultSansSerifTypeface (face);

    titleLabel.setText ("CHOP STATION", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
    titleLabel.setColour (juce::Label::textColourId, paper);
    addAndMakeVisible (titleLabel);

    statusLabel.setColour (juce::Label::textColourId, paper.withAlpha (0.7f));
    statusLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (statusLabel);

    loadButton.setColour (juce::TextButton::buttonColourId, accent);
    loadButton.setColour (juce::TextButton::textColourOffId, paper);
    loadButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible (loadButton);

    presetBox.setColour (juce::ComboBox::backgroundColourId, surface.brighter (0.08f));
    presetBox.setColour (juce::ComboBox::textColourId, paper);

    for (int i = 0; i < processor.numPresets(); ++i)
        presetBox.addItem (processor.presetName (i), i + 1);

    presetBox.setSelectedId (1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        processor.applyPreset (presetBox.getSelectedId() - 1);
    };
    addAndMakeVisible (presetBox);

    const char* paramIds[10] = { "chops", "gain", "drive", "bits", "cutoff",
                                 "reso", "delayMix", "delayTime", "reverb", "pan" };
    const char* paramNames[10] = { "Chops", "Gain", "Drive", "Bits", "Cutoff",
                                   "Reso", "Delay Mix", "Delay Time", "Reverb", "Pan" };

    for (int i = 0; i < 10; ++i)
    {
        auto control = std::make_unique<ParamControl> (paramNames[i]);
        control->slider.setSliderStyle (juce::Slider::RotaryHorizontalDrag);
        control->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
        control->nameLabel.setColour (juce::Label::textColourId, paper.withAlpha (0.85f));
        control->nameLabel.setFont (juce::Font (12.0f));
        control->nameLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (control->slider);
        addAndMakeVisible (control->nameLabel);
        control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
            (processor.apvts, paramIds[i], control->slider);
        controls[static_cast<size_t> (i)] = std::move (control);
    }

    refreshLabels();
    setSize (460, 380);
    startTimerHz (4);
}

void ChopStationEditor::paint (juce::Graphics& g)
{
    g.fillAll (ink);
    g.setColour (surface);
    g.fillRect (getLocalBounds().reduced (12));
    g.setColour (accent);
    g.fillRect (12, 12, getWidth() - 24, 3);
}

void ChopStationEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (8);

    titleLabel.setBounds (area.removeFromTop (26));
    area.removeFromTop (8);
    statusLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (12);

    auto row = area.removeFromTop (30);
    loadButton.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (12);
    presetBox.setBounds (row.removeFromLeft (160));

    area.removeFromTop (14);

    // 5 columns x 2 rows of rotary knobs.
    constexpr int columns = 5;
    constexpr int rows = 2;
    const auto cellW = area.getWidth() / columns;
    const auto cellH = area.getHeight() / rows;

    for (int i = 0; i < 10; ++i)
    {
        const auto col = i % columns;
        const auto r = i / columns;
        auto cell = juce::Rectangle<int> (area.getX() + col * cellW, area.getY()
                                          + r * cellH, cellW, cellH);
        auto* control = controls[static_cast<size_t> (i)].get();
        control->nameLabel.setBounds (cell.removeFromTop (16));
        control->slider.setBounds (cell.reduced (6, 2));
    }
}

void ChopStationEditor::timerCallback()
{
    refreshLabels();
}

void ChopStationEditor::refreshLabels()
{
    if (processor.isLoadingSample())
    {
        loadButton.setEnabled (false);
        statusLabel.setText ("Analyzing sample...", juce::dontSendNotification);
        return;
    }

    loadButton.setEnabled (true);

    if (processor.didLastLoadFail())
    {
        statusLabel.setText ("Could not analyze that sample", juce::dontSendNotification);
        return;
    }

    const auto name = processor.getSampleName();
    const auto slices = processor.getSliceCount();

    statusLabel.setText (name.isEmpty() ? "No sample loaded - drop in a break"
                                        : name + "  -  " + juce::String (slices) + " chops",
                         juce::dontSendNotification);
}

void ChopStationEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (file.existsAsFile())
                              {
                                  processor.loadSampleAsync (file);
                                  refreshLabels();
                              }
                          });
}
