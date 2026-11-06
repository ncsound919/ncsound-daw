#include "MainComponent.h"

namespace nc
{
namespace
{
juce::Font labelFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions().withHeight (height).withStyle (bold ? "Bold" : "Regular"));
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

constexpr int trackCount = 4;
constexpr double loopSeconds = 16.0;

juce::String timecode (double seconds)
{
    const auto whole = static_cast<int> (seconds);
    const auto millis = static_cast<int> ((seconds - whole) * 1000.0);

    return juce::String (whole / 60).paddedLeft ('0', 2) + ":"
         + juce::String (whole % 60).paddedLeft ('0', 2) + "."
         + juce::String (millis).paddedLeft ('0', 3);
}
} // namespace

MainComponent::MainComponent (juce::File registryFile)
{
    registry = loadRegistry (registryFile);

    browser.setRowHeight (34);
    browser.setOutlineThickness (0);
    browser.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    browser.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (browser);

    auto styleButton = [this] (juce::TextButton& button, juce::Colour background)
    {
        button.setColour (juce::TextButton::buttonColourId, background);
        button.setColour (juce::TextButton::textColourOffId, brand::paper);
        button.setColour (juce::TextButton::textColourOnId, brand::paper);
        addAndMakeVisible (button);
    };

    styleButton (playButton, brand::accent);
    styleButton (stopButton, brand::surface);

    playButton.onClick = [this]
    {
        playing = ! playing;
        playButton.setButtonText (playing ? "PAUSE" : "PLAY");
    };

    stopButton.onClick = [this]
    {
        playing = false;
        playheadSeconds = 0.0;
        playButton.setButtonText ("PLAY");
        repaint();
    };

    bpm.setSliderStyle (juce::Slider::LinearBar);
    bpm.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    bpm.setRange (40.0, 240.0, 1.0);
    bpm.setValue (120.0, juce::dontSendNotification);
    bpm.setColour (juce::Slider::trackColourId, brand::accent);
    bpm.setColour (juce::Slider::backgroundColourId, brand::surface);
    addAndMakeVisible (bpm);

    startTimerHz (30);
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
    if (! playing)
        return;

    playheadSeconds += 1.0 / 30.0;

    if (playheadSeconds >= loopSeconds)
        playheadSeconds = 0.0;

    repaint (arrangementArea);
    repaint (transportArea);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (brand::well);
    paintHeader (g);
    paintBrowser (g);
    paintArrangement (g);
    paintInspector (g);
    paintTransport (g);
}

void MainComponent::paintHeader (juce::Graphics& g)
{
    g.setColour (brand::ink);
    g.fillRect (headerArea);

    auto area = headerArea.reduced (20, 0);
    auto brandArea = area.removeFromLeft (360);

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
                    + juce::String (registry.countWithStatus (PluginStatus::native)) + " NATIVE",
                area, juce::Justification::centredRight, false);
}

void MainComponent::paintBrowser (juce::Graphics& g)
{
    g.setColour (brand::ink);
    g.fillRect (browserArea);

    auto area = browserArea.reduced (16, 12);
    g.setColour (brand::accent);
    g.setFont (labelFont (10.0f, true));
    g.drawText ("STOCK SUITE", area.removeFromTop (20), juce::Justification::centredLeft, false);
}

void MainComponent::paintArrangement (juce::Graphics& g)
{
    g.setColour (brand::well);
    g.fillRect (arrangementArea);

    auto area = arrangementArea.reduced (16, 12);
    auto ruler = area.removeFromTop (24);

    g.setColour (brand::paper.withAlpha (0.45f));
    g.setFont (labelFont (10.0f, true));
    g.drawText ("ARRANGEMENT", ruler.removeFromLeft (160), juce::Justification::centredLeft, false);

    const auto laneWidth = ruler.getWidth();
    const auto pixelsPerBar = static_cast<float> (laneWidth) / 8.0f;

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

    const auto laneHeight = area.getHeight() / trackCount;

    for (int track = 0; track < trackCount; ++track)
    {
        auto lane = area.withY (area.getY() + track * laneHeight).withHeight (laneHeight).reduced (0, 2);

        g.setColour (track % 2 == 0 ? brand::ink : brand::surface.withAlpha (0.55f));
        g.fillRoundedRectangle (lane.toFloat(), 3.0f);

        g.setColour (brand::paper.withAlpha (0.35f));
        g.setFont (labelFont (10.0f, true));
        g.drawText ("TRACK " + juce::String (track + 1), lane.withTrimmedLeft (10), juce::Justification::centredLeft, false);

        g.setColour (brand::paper.withAlpha (0.18f));
        g.setFont (labelFont (11.0f));
        g.drawText ("drop a plugin here", lane.withTrimmedRight (12), juce::Justification::centredRight, false);
    }

    if (playing)
    {
        const auto x = static_cast<float> (area.getX())
                     + static_cast<float> (playheadSeconds / loopSeconds) * static_cast<float> (area.getWidth());

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

    area.removeFromTop (10);

    g.setColour (brand::paper.withAlpha (0.35f));
    g.setFont (labelFont (10.0f, true));
    g.drawText ("SOURCE", area.removeFromTop (16), juce::Justification::centredLeft, false);

    g.setColour (brand::paper.withAlpha (0.75f));
    g.setFont (labelFont (11.0f));
    g.drawFittedText (plugin.source, area.removeFromTop (54), juce::Justification::topLeft, 4);

    if (plugin.note.isNotEmpty())
    {
        g.setColour (brand::paper.withAlpha (0.35f));
        g.setFont (labelFont (10.0f, true));
        g.drawText ("NOTE", area.removeFromTop (16), juce::Justification::centredLeft, false);

        g.setColour (brand::accent.withAlpha (0.85f));
        g.setFont (labelFont (11.0f));
        g.drawFittedText (plugin.note, area.removeFromTop (54), juce::Justification::topLeft, 4);
    }
}

void MainComponent::paintTransport (juce::Graphics& g)
{
    g.setColour (brand::ink);
    g.fillRect (transportArea);

    auto area = transportArea.reduced (16, 0);
    area.removeFromLeft (300 + 260 - 16);

    g.setColour (brand::paper.withAlpha (0.85f));
    g.setFont (labelFont (15.0f, true));
    g.drawText (timecode (playheadSeconds), area.removeFromLeft (120), juce::Justification::centredLeft, false);

    g.setColour (brand::paper.withAlpha (0.4f));
    g.setFont (labelFont (10.0f));
    g.drawText (playing ? "PLAYING" : "STOPPED", area.removeFromLeft (90), juce::Justification::centredLeft, false);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    headerArea = area.removeFromTop (56);
    transportArea = area.removeFromBottom (56);
    browserArea = area.removeFromLeft (300);
    inspectorArea = area.removeFromRight (260);
    arrangementArea = area;

    browser.setBounds (browserArea.reduced (16, 36));

    auto transport = transportArea.reduced (16, 12);
    transport.removeFromLeft (300 + 260 - 16);
    playButton.setBounds (transport.removeFromLeft (76).reduced (0, 2));
    transport.removeFromLeft (6);
    stopButton.setBounds (transport.removeFromLeft (66).reduced (0, 2));
    transport.removeFromLeft (12);
    bpm.setBounds (transport.removeFromRight (140).reduced (0, 6));
}
} // namespace nc
