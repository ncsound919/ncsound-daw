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

    refreshLabels();
    setSize (460, 170);
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
    loadButton.setBounds (area.removeFromTop (30).removeFromLeft (150));
}

void ChopStationEditor::timerCallback()
{
    refreshLabels();
}

void ChopStationEditor::refreshLabels()
{
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
                                  processor.loadSample (file);
                                  refreshLabels();
                              }
                          });
}
