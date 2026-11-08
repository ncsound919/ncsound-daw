#include "SettingsWindow.h"

#include "Theme.h"

namespace nc
{
SettingsWindow::SettingsWindow (AudioEngine& engineToUse)
    : DocumentWindow ("Audio Settings", theme::panel, DocumentWindow::closeButton, true)
    , engine (engineToUse)
    , selector (engine.audioDeviceManager(),
                0, 32,    // input channels: none required, up to 32
                0, 32,    // output channels
                true,     // show MIDI input selector
                false,    // no MIDI output selector
                true,     // stereo pairs
                false)    // show advanced options inline
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setContentNonOwned (&body, false);

    auxTitle.setText ("AUX RETURNS", juce::dontSendNotification);
    auxTitle.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    auxTitle.setColour (juce::Label::textColourId, theme::accent);
    body.addAndMakeVisible (auxTitle);

    reverbLabel.setText ("REVERB ENGINE", juce::dontSendNotification);
    reverbLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
    reverbLabel.setColour (juce::Label::textColourId, theme::accent.withAlpha (0.65f));
    body.addAndMakeVisible (reverbLabel);

    delayLabel.setText ("DELAY ENGINE", juce::dontSendNotification);
    delayLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
    delayLabel.setColour (juce::Label::textColourId, theme::accent.withAlpha (0.65f));
    body.addAndMakeVisible (delayLabel);

    // Item ids are the AuxBus::Engine values, so the selection maps straight
    // through with no lookup table to drift.
    reverbBox.addItem (AuxBus::engineName (AuxBus::Engine::overlayReverb),
                       static_cast<int> (AuxBus::Engine::overlayReverb) + 1);
    reverbBox.addItem (AuxBus::engineName (AuxBus::Engine::nexusReverb),
                       static_cast<int> (AuxBus::Engine::nexusReverb) + 1);
    reverbBox.onChange = [this]
    {
        engine.setAuxEngine (0, static_cast<AuxBus::Engine> (reverbBox.getSelectedId() - 1));
    };
    body.addAndMakeVisible (reverbBox);

    delayBox.addItem (AuxBus::engineName (AuxBus::Engine::overlayDelay),
                      static_cast<int> (AuxBus::Engine::overlayDelay) + 1);
    delayBox.addItem (AuxBus::engineName (AuxBus::Engine::temporalDelay),
                      static_cast<int> (AuxBus::Engine::temporalDelay) + 1);
    delayBox.onChange = [this]
    {
        engine.setAuxEngine (1, static_cast<AuxBus::Engine> (delayBox.getSelectedId() - 1));
    };
    body.addAndMakeVisible (delayBox);

    body.addAndMakeVisible (selector);

    refreshAuxBoxes();

    setSize (620, 600);
    setVisible (true);
}

void SettingsWindow::refreshAuxBoxes()
{
    reverbBox.setSelectedId (static_cast<int> (engine.auxEngine (0)) + 1, juce::dontSendNotification);
    delayBox.setSelectedId (static_cast<int> (engine.auxEngine (1)) + 1, juce::dontSendNotification);
}

void SettingsWindow::resized()
{
    body.setBounds (getLocalBounds());

    auto area = body.getLocalBounds().reduced (14);
    auto header = area.removeFromTop (74);

    auxTitle.setBounds (header.removeFromTop (16));

    auto row = header.removeFromTop (26);
    reverbLabel.setBounds (row.removeFromLeft (110));
    reverbBox.setBounds (row.removeFromLeft (170));
    row.removeFromLeft (16);
    delayLabel.setBounds (row.removeFromLeft (90));
    delayBox.setBounds (row.removeFromLeft (170));

    area.removeFromTop (6);
    selector.setBounds (area);
}

void SettingsWindow::closeButtonPressed()
{
    delete this;
}
} // namespace nc
