#include "MainComponent.h"
#include "BinaryData.h"

#include <cmath>

namespace nc
{
namespace
{
juce::Font labelFont (float height, bool bold = false)
{
    static const auto display = juce::Typeface::createSystemTypefaceFor (BinaryData::BremenBlack_ttf, BinaryData::BremenBlack_ttfSize);
    static const auto small = juce::Typeface::createSystemTypefaceFor (BinaryData::BankGothic_ttf, BinaryData::BankGothic_ttfSize);
    static const auto ui = juce::Typeface::createSystemTypefaceFor (BinaryData::Inter_ttf, BinaryData::Inter_ttfSize);
    const auto face = bold ? (height <= 12.0f ? small : display) : ui;
    return juce::Font (juce::FontOptions (face).withHeight (height).withStyle (bold ? "Bold" : "Regular"));
}

juce::Colour colourForStatus (PluginStatus status)
{
    switch (status)
    {
        case PluginStatus::native:   return brand::accent;
        case PluginStatus::external: return juce::Colour (0xff6f8f6f);
        case PluginStatus::port:     return juce::Colour (0xffc9a227);
        case PluginStatus::archived: return juce::Colour (0xff5a5a5a);
    }

    return brand::paper;
}

juce::StringArray trackNames()
{
    return { "DRUMS", "BASS", "KEYS", "VOX", "GTR", "PERC", "AUX 1", "AUX 2" };
}
} // namespace

MainComponent::MainComponent (juce::File registryFile)
    : registry (loadRegistry (registryFile))
{
    browser.setRowHeight (32);
    browser.setOutlineThickness (0);
    browser.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    browser.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (browser);

    auto styleButton = [this] (juce::TextButton& button, juce::Colour background)
    {
        button.setColour (juce::TextButton::buttonColourId, background);
        button.setColour (juce::TextButton::buttonOnColourId, brand::accent);
        button.setColour (juce::TextButton::textColourOffId, brand::paper);
        button.setColour (juce::TextButton::textColourOnId, brand::paper);
        addAndMakeVisible (button);
    };

    styleButton (arrangeButton, brand::surface);
    styleButton (mixerButton, brand::surface);
    styleButton (playButton, brand::accent);
    styleButton (stopButton, brand::surface);
    styleButton (metronomeButton, brand::surface);

    arrangeButton.setToggleState (true, juce::dontSendNotification);
    arrangeButton.onClick = [this] { setView (View::arrange); };
    mixerButton.onClick = [this] { setView (View::mixer); };

    playButton.onClick = [this]
    {
        engine.setPlaying (! engine.isPlaying());
        playButton.setButtonText (engine.isPlaying() ? "PAUSE" : "PLAY");
    };

    stopButton.onClick = [this]
    {
        engine.setPlaying (false);
        engine.resetPosition();
        playButton.setButtonText ("PLAY");
        repaint();
    };

    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setToggleState (engine.isMetronomeOn(), juce::dontSendNotification);
    metronomeButton.onClick = [this] { engine.setMetronome (metronomeButton.getToggleState()); };

    bpm.setSliderStyle (juce::Slider::LinearBar);
    bpm.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    bpm.setRange (40.0, 240.0, 1.0);
    bpm.setValue (engine.getBpm(), juce::dontSendNotification);
    bpm.setColour (juce::Slider::trackColourId, brand::accent);
    bpm.setColour (juce::Slider::backgroundColourId, brand::surface);
    bpm.onValueChange = [this] { engine.setBpm (bpm.getValue()); };
    addAndMakeVisible (bpm);

    const auto names = trackNames();

    for (int index = 0; index < AudioEngine::trackCount; ++index)
    {
        strips.push_back (std::make_unique<ChannelStrip> (engine, index, names[index]));
        addChildComponent (*strips.back());
    }

    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

void MainComponent::setView (View newView)
{
    view = newView;

    arrangeButton.setToggleState (view == View::arrange, juce::dontSendNotification);
    mixerButton.setToggleState (view == View::mixer, juce::dontSendNotification);

    for (auto& strip : strips)
        strip->setVisible (view == View::mixer);

    resized();
    repaint();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        playButton.onClick();
        return true;
    }

    return false;
}

int MainComponent::getNumRows()
{
    return static_cast<int> (registry.plugins.size());
}

void MainComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (row < 0 || row >= getNumRows())
        return;

    const auto& plugin = registry.plugins[static_cast<size_t> (row)];
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (4, 2);

    if (rowIsSelected)
    {
        g.setColour (brand::surface);
        g.fillRoundedRectangle (area.toFloat(), 3.0f);
    }

    g.setColour (colourForStatus (plugin.status));
    g.fillEllipse (static_cast<float> (area.getX() + 8), static_cast<float> (area.getCentreY() - 3), 6.0f, 6.0f);

    g.setColour (rowIsSelected ? brand::paper : brand::paper.withAlpha (0.82f));
    g.setFont (labelFont (13.0f, rowIsSelected));
    g.drawText (plugin.name, area.withTrimmedLeft (24).withTrimmedRight (78), juce::Justification::centredLeft, true);

    g.setColour (brand::paper.withAlpha (0.38f));
    g.setFont (labelFont (11.0f));
    g.drawText (statusName (plugin.status), area.removeFromRight (74), juce::Justification::centredRight, false);
}

void MainComponent::selectedRowsChanged (int lastRowSelected)
{
    selectedRow = lastRowSelected;
    repaint (inspectorArea);
}

void MainComponent::timerCallback()
{
    if (engine.isPlaying())
        repaint (transportArea);

    for (auto& strip : strips)
        strip->refresh();

    if (view == View::arrange && engine.isPlaying())
        repaint (mainArea);
}

juce::String MainComponent::positionText() const
{
    const auto seconds = engine.positionSeconds();
    const auto tempo = juce::jmax (1.0, engine.getBpm());
    const auto totalBeats = seconds * tempo / 60.0;
    const auto bar = static_cast<int> (totalBeats / 4.0) + 1;
    const auto beat = static_cast<int> (std::fmod (totalBeats, 4.0)) + 1;
    const auto ticks = static_cast<int> ((totalBeats - std::floor (totalBeats)) * 960.0);
    const auto whole = static_cast<int> (seconds);
    const auto millis = static_cast<int> ((seconds - whole) * 1000.0);

    return juce::String (bar) + ":" + juce::String (beat) + ":" + juce::String (ticks).paddedLeft ('0', 3)
         + "    " + juce::String (whole / 60).paddedLeft ('0', 2) + ":" + juce::String (whole % 60).paddedLeft ('0', 2)
         + "." + juce::String (millis).paddedLeft ('0', 3);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (brand::well);
    paintHeader (g);
    paintBrowser (g);

    if (view == View::arrange)
    {
        paintArrangement (g);
        paintInspector (g);
    }
    else
    {
        g.setColour (brand::well);
        g.fillRect (mainArea);

        g.setColour (brand::paper.withAlpha (0.45f));
        g.setFont (labelFont (10.0f, true));
        g.drawText ("MIXER", mainArea.reduced (16, 12).removeFromTop (18), juce::Justification::centredLeft, false);
    }

    paintTransport (g);
}

void MainComponent::paintHeader (juce::Graphics& g)
{
    g.setColour (brand::ink);
    g.fillRect (headerArea);

    auto area = headerArea.reduced (20, 0);
    auto brandArea = area.removeFromLeft (320);

    g.setColour (brand::paper);
    g.setFont (labelFont (21.0f, true));
    g.drawText ("NCSOUND", brandArea.removeFromTop (34), juce::Justification::bottomLeft, false);

    g.setColour (brand::accent);
    g.setFont (labelFont (10.0f, true));
    g.drawText ("MUSIC HUB  ·  A DIVISION OF OVERLAY365", brandArea, juce::Justification::topLeft, false);

    g.setColour (brand::paper.withAlpha (0.5f));
    g.setFont (labelFont (11.0f));
    g.drawText (juce::String (registry.plugins.size()) + " PLUGINS   ·   "
                    + juce::String (registry.countInstruments()) + " INSTRUMENTS   ·   "
                    + juce::String (registry.countEffects()) + " EFFECTS   ·   "
                    + engine.deviceDescription(),
                area.withTrimmedRight (200), juce::Justification::centredRight, false);
}

void MainComponent::paintBrowser (juce::Graphics& g)
{
    g.setColour (brand::ink);
    g.fillRect (browserArea);

    g.setColour (brand::accent);
    g.setFont (labelFont (10.0f, true));
    g.drawText ("STOCK SUITE", browserArea.reduced (16, 12).removeFromTop (20), juce::Justification::centredLeft, false);
}

void MainComponent::paintArrangement (juce::Graphics& g)
{
    auto area = mainArea.reduced (16, 12);
    auto ruler = area.removeFromTop (24);

    g.setColour (brand::paper.withAlpha (0.45f));
    g.setFont (labelFont (10.0f, true));
    g.drawText ("ARRANGEMENT  ·  16 BARS", ruler.removeFromLeft (200), juce::Justification::centredLeft, false);

    const auto pixelsPerBar = static_cast<float> (ruler.getWidth()) / 8.0f;

    for (int bar = 1; bar <= 8; ++bar)
    {
        const auto x = static_cast<float> (ruler.getX()) + static_cast<float> (bar - 1) * pixelsPerBar;

        g.setColour (brand::paper.withAlpha (0.28f));
        g.setFont (labelFont (9.0f));
        g.drawText (juce::String (bar), juce::Rectangle<int> (static_cast<int> (x) + 4, ruler.getY(), 24, ruler.getHeight()),
                    juce::Justification::centredLeft, false);

        g.setColour (brand::paper.withAlpha (0.07f));
        g.drawVerticalLine (static_cast<int> (x), static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
    }

    const auto names = trackNames();
    const auto laneHeight = area.getHeight() / AudioEngine::trackCount;
    const auto loopSeconds = 64.0 * 60.0 / juce::jmax (1.0, engine.getBpm());

    for (int track = 0; track < AudioEngine::trackCount; ++track)
    {
        auto lane = area.withY (area.getY() + track * laneHeight).withHeight (laneHeight).reduced (0, 2);

        g.setColour (track % 2 == 0 ? brand::ink : brand::surface.withAlpha (0.55f));
        g.fillRoundedRectangle (lane.toFloat(), 3.0f);

        g.setColour (brand::paper.withAlpha (0.35f));
        g.setFont (labelFont (10.0f, true));
        g.drawText (names[track], lane.withTrimmedLeft (10), juce::Justification::centredLeft, false);

        g.setColour (engine.trackMuted (track) ? brand::accent.withAlpha (0.5f) : brand::paper.withAlpha (0.16f));
        g.setFont (labelFont (11.0f));
        g.drawText (engine.trackMuted (track) ? "MUTED" : "no clip", lane.withTrimmedRight (12), juce::Justification::centredRight, false);
    }

    if (engine.isPlaying())
    {
        const auto fraction = std::fmod (engine.positionSeconds(), loopSeconds) / loopSeconds;
        const auto x = static_cast<float> (area.getX()) + static_cast<float> (fraction) * static_cast<float> (area.getWidth());

        g.setColour (brand::accent);
        g.drawVerticalLine (static_cast<int> (x), static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
    }
}

void MainComponent::paintInspector (juce::Graphics& g)
{
    g.setColour (brand::ink);
    g.fillRect (inspectorArea);

    auto area = inspectorArea.reduced (16, 12);

    g.setColour (brand::accent);
    g.setFont (labelFont (10.0f, true));
    g.drawText ("INSPECTOR", area.removeFromTop (20), juce::Justification::centredLeft, false);

    if (selectedRow < 0 || selectedRow >= getNumRows())
    {
        g.setColour (brand::paper.withAlpha (0.4f));
        g.setFont (labelFont (12.0f));
        g.drawFittedText ("Select a plugin to see where it is built and what it still needs.",
                          area.removeFromTop (80), juce::Justification::topLeft, 4);
        return;
    }

    const auto& plugin = registry.plugins[static_cast<size_t> (selectedRow)];

    g.setColour (brand::paper);
    g.setFont (labelFont (16.0f, true));
    g.drawFittedText (plugin.name, area.removeFromTop (48), juce::Justification::topLeft, 2);

    g.setColour (colourForStatus (plugin.status));
    g.setFont (labelFont (10.0f, true));
    g.drawText (statusName (plugin.status).toUpperCase() + "   ·   " + plugin.kind.toUpperCase(),
                area.removeFromTop (18), juce::Justification::centredLeft, false);
}

void MainComponent::paintTransport (juce::Graphics& g)
{
    g.setColour (brand::ink);
    g.fillRect (transportArea);

    auto area = transportArea.reduced (16, 0);
    area.removeFromLeft (280).removeFromLeft (330);

    g.setColour (brand::paper);
    static const auto digital = juce::Typeface::createSystemTypefaceFor (BinaryData::DigitalReadout_ttf, BinaryData::DigitalReadout_ttfSize);
    g.setFont (juce::Font (juce::FontOptions (digital).withHeight (16.0f)));
    g.drawText (positionText(), area, juce::Justification::centredRight, false);

    auto meter = transportArea.reduced (16, 16).removeFromRight (220).removeFromLeft (10);
    meter.setY (transportArea.getCentreY() - 8);
    meter.setHeight (16);

    g.setColour (brand::surface);
    g.fillRect (meter);

    const auto level = juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (engine.masterPeak(), -60.0f) / 60.0f + 1.0f);
    auto lit = meter.removeFromBottom (static_cast<int> (level * 16.0f));

    g.setColour (level > 0.95f ? brand::accent : juce::Colour (0xff6f8f6f));
    g.fillRect (lit);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    headerArea = area.removeFromTop (56);
    transportArea = area.removeFromBottom (64);
    browserArea = area.removeFromLeft (280);

    if (view == View::arrange)
        inspectorArea = area.removeFromRight (240);
    else
        inspectorArea = {};

    mainArea = area;

    auto header = headerArea.reduced (16, 0);
    auto viewButtons = header.removeFromRight (200);
    mixerButton.setBounds (viewButtons.removeFromRight (86).reduced (0, 12));
    viewButtons.removeFromRight (6);
    arrangeButton.setBounds (viewButtons.removeFromRight (96).reduced (0, 12));

    browser.setBounds (browserArea.reduced (16, 36));

    auto transport = transportArea.reduced (16, 12);
    playButton.setBounds (transport.removeFromLeft (74).reduced (0, 2));
    transport.removeFromLeft (6);
    stopButton.setBounds (transport.removeFromLeft (64).reduced (0, 2));
    transport.removeFromLeft (6);
    metronomeButton.setBounds (transport.removeFromLeft (68).reduced (0, 2));
    transport.removeFromLeft (12);
    bpm.setBounds (transport.removeFromLeft (110).reduced (0, 8));

    if (view == View::mixer)
    {
        auto stripArea = mainArea.reduced (16, 32);
        stripArea.removeFromTop (10);
        const auto width = juce::jmin (120, stripArea.getWidth() / AudioEngine::trackCount);

        for (auto& strip : strips)
            strip->setBounds (stripArea.removeFromLeft (width).reduced (3, 0));
    }
}
} // namespace nc
// end of file
