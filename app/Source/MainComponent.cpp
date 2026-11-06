#include "MainComponent.h"

#include <cmath>

namespace nc
{
namespace
{
const juce::StringArray kTrackNames { "DRUMS", "BASS", "KEYS", "VOX", "GTR", "PERC", "AUX 1", "AUX 2" };
const char* const kTools[] { "ZOOM", "TRIM", "SELECT", "GRAB", "PENCIL", "ERASE" };
const char* const kModes[] { "SHUFFLE", "SLIP", "SPOT", "GRID" };

juce::Colour laneColour (int index)
{
    return index % 2 == 0 ? juce::Colour (0xff0a0d14) : juce::Colour (0xff0c1018);
}
} // namespace

MainComponent::MainComponent (juce::File registryFile)
    : registry (loadRegistry (registryFile))
{
    auto styleButton = [this] (juce::TextButton& button, juce::Colour on = theme::accent)
    {
        button.setColour (juce::TextButton::buttonColourId, theme::panel);
        button.setColour (juce::TextButton::buttonOnColourId, on);
        button.setColour (juce::TextButton::textColourOffId, theme::textDim);
        button.setColour (juce::TextButton::textColourOnId, theme::bg);
        addAndMakeVisible (button);
    };

    styleButton (rtz);
    styleButton (rew);
    styleButton (stopButton);
    styleButton (playButton);
    styleButton (recordButton, theme::red);
    styleButton (metronomeButton, theme::sky);
    styleButton (pluginsButton);
    styleButton (editButton);
    styleButton (mixButton);

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

    rtz.onClick = [this] { engine.resetPosition(); repaint(); };
    rew.onClick = [this] { engine.resetPosition(); repaint(); };

    recordButton.setClickingTogglesState (true);
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setToggleState (engine.isMetronomeOn(), juce::dontSendNotification);
    metronomeButton.onClick = [this] { engine.setMetronome (metronomeButton.getToggleState()); };

    pluginsButton.setClickingTogglesState (true);
    pluginsButton.onClick = [this] { setPanelOpen (pluginsButton.getToggleState()); };

    editButton.setToggleState (true, juce::dontSendNotification);
    editButton.onClick = [this] { setView (View::edit); };
    mixButton.onClick = [this] { setView (View::mix); };

    bpm.setSliderStyle (juce::Slider::LinearBar);
    bpm.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    bpm.setRange (40.0, 240.0, 1.0);
    bpm.setValue (engine.getBpm(), juce::dontSendNotification);
    bpm.setColour (juce::Slider::trackColourId, theme::accent);
    bpm.setColour (juce::Slider::backgroundColourId, theme::bg);
    bpm.onValueChange = [this] { engine.setBpm (bpm.getValue()); };
    addAndMakeVisible (bpm);

    masterFader.setSliderStyle (juce::Slider::LinearVertical);
    masterFader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterFader.setRange (-60.0, 6.0, 0.1);
    masterFader.setValue (-6.0, juce::dontSendNotification);
    masterFader.setColour (juce::Slider::trackColourId, theme::accent);
    masterFader.setColour (juce::Slider::backgroundColourId, theme::bg);
    masterFader.onValueChange = [this]
    {
        engine.setMasterGain (static_cast<float> (juce::Decibels::decibelsToGain (masterFader.getValue())));
    };
    addChildComponent (masterFader);

    browser.setRowHeight (26);
    browser.setOutlineThickness (0);
    browser.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    browser.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addChildComponent (browser);

    for (int index = 0; index < AudioEngine::trackCount; ++index)
    {
        strips.push_back (std::make_unique<ChannelStrip> (engine, index, kTrackNames[index]));
        addChildComponent (*strips.back());
    }

    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

void MainComponent::setView (View newView)
{
    view = newView;
    editButton.setToggleState (view == View::edit, juce::dontSendNotification);
    mixButton.setToggleState (view == View::mix, juce::dontSendNotification);

    for (auto& strip : strips)
        strip->setVisible (view == View::mix);

    masterFader.setVisible (view == View::mix);
    browser.setVisible (view == View::edit && panelOpen);

    resized();
    repaint();
}

void MainComponent::setPanelOpen (bool shouldOpen)
{
    panelOpen = shouldOpen;
    pluginsButton.setToggleState (panelOpen, juce::dontSendNotification);
    browser.setVisible (view == View::edit && panelOpen);
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

    const auto character = key.getTextCharacter();

    if (character == 'r' || character == 'R')
    {
        armed[selectedTrack] = ! armed[selectedTrack];
        repaint();
        return true;
    }

    if (character == 'm' || character == 'M')
    {
        engine.setTrackMuted (selectedTrack, ! engine.trackMuted (selectedTrack));
        repaint();
        return true;
    }

    if (character == 's' || character == 'S')
    {
        engine.setTrackSolo (selectedTrack, ! engine.trackSolo (selectedTrack));
        repaint();
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
    auto area = juce::Rectangle<int> (0, 0, width, height);

    if (rowIsSelected)
    {
        g.setColour (theme::accent.withAlpha (0.16f));
        g.fillRect (area);
    }

    g.setColour (rowIsSelected ? theme::accentHi : theme::textMuted);
    g.setFont (theme::ui (9.0f));
    g.drawText (plugin.isInstrument() ? "INS" : "FX", area.removeFromLeft (28), juce::Justification::centred, false);

    g.setColour (rowIsSelected ? theme::text : theme::textDim);
    g.setFont (theme::ui (10.5f));
    g.drawText (plugin.name, area.withTrimmedRight (54), juce::Justification::centredLeft, true);

    g.setColour (theme::textMuted);
    g.setFont (theme::label (7.5f));
    g.drawText (statusName (plugin.status).toUpperCase(), area.removeFromRight (52), juce::Justification::centredRight, false);
}

void MainComponent::selectedRowsChanged (int lastRowSelected)
{
    selectedRow = lastRowSelected;
    repaint (panelArea);
}

void MainComponent::timerCallback()
{
    for (auto& strip : strips)
        strip->refresh();

    if (engine.isPlaying())
    {
        repaint (transportArea);
        repaint (laneArea);
    }
}

juce::String MainComponent::positionText() const
{
    const auto seconds = engine.positionSeconds();
    const auto tempo = juce::jmax (1.0, engine.getBpm());
    const auto beats = seconds * tempo / 60.0;
    const auto bar = static_cast<int> (beats / 4.0) + 1;
    const auto beat = static_cast<int> (std::fmod (beats, 4.0)) + 1;
    const auto ticks = static_cast<int> ((beats - std::floor (beats)) * 960.0);

    return juce::String (bar) + "|" + juce::String (beat) + "|" + juce::String (ticks).paddedLeft ('0', 3);
}

juce::String MainComponent::clockText() const
{
    const auto seconds = engine.positionSeconds();
    const auto whole = static_cast<int> (seconds);
    const auto millis = static_cast<int> ((seconds - whole) * 1000.0);

    return juce::String (whole / 60).paddedLeft ('0', 2) + ":"
         + juce::String (whole % 60).paddedLeft ('0', 2) + "."
         + juce::String (millis).paddedLeft ('0', 3);
}

int MainComponent::trackAt (juce::Point<int> position) const
{
    if (laneArea.isEmpty() || position.y < laneArea.getY() + 22)
        return -1;

    const auto rowHeight = static_cast<float> (laneArea.getHeight() - 22) / static_cast<float> (AudioEngine::trackCount);
    const auto row = static_cast<int> (static_cast<float> (position.y - laneArea.getY() - 22) / rowHeight);

    return juce::isPositiveAndBelow (row, AudioEngine::trackCount) ? row : -1;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    paintMenu (g);
    paintTransport (g);
    paintTools (g);

    if (view == View::edit)
    {
        paintEdit (g);

        if (panelOpen)
            paintPanel (g);
    }
    else
    {
        paintMix (g);
    }

    paintStatus (g);
}

void MainComponent::paintMenu (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (menuArea);
    theme::hairline (g, 0.0f, static_cast<float> (menuArea.getBottom()), static_cast<float> (getWidth()), static_cast<float> (menuArea.getBottom()));

    g.setColour (theme::text);
    g.setFont (theme::display (11.0f));
    g.drawText ("NCSOUND", menuArea.reduced (10, 0), juce::Justification::centredLeft, false);

    g.setColour (theme::textMuted);
    g.setFont (theme::label (8.0f));
    g.drawText ("FILE     EDIT     VIEW     TRACK     CLIP     OPTIONS     SETUP     WINDOW",
                menuArea.withTrimmedLeft (120), juce::Justification::centredLeft, false);

    g.setColour (theme::textMuted);
    g.setFont (theme::label (7.5f));
    g.drawText ("OVERLAY365  ·  MUSIC HUB", menuArea.reduced (10, 0), juce::Justification::centredRight, false);
}

void MainComponent::paintTransport (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (transportArea);
    theme::hairline (g, 0.0f, static_cast<float> (transportArea.getBottom()), static_cast<float> (getWidth()), static_cast<float> (transportArea.getBottom()));

    const auto x = static_cast<float> (transportArea.getX() + 232);
    g.setColour (theme::textMuted);
    g.setFont (theme::label (7.0f));
    g.drawText ("BARS | BEATS", juce::Rectangle<int> (static_cast<int> (x), transportArea.getY() + 3, 170, 9), juce::Justification::centredLeft, false);

    g.setColour (theme::text);
    g.setFont (theme::clock (17.0f));
    g.drawText (positionText(), juce::Rectangle<int> (static_cast<int> (x), transportArea.getY() + 11, 170, 21), juce::Justification::centredLeft, false);

    g.setColour (theme::sky);
    g.setFont (theme::clock (9.0f));
    g.drawText (clockText(), juce::Rectangle<int> (static_cast<int> (x), transportArea.getY() + 34, 170, 12), juce::Justification::centredLeft, false);

    g.setColour (theme::textMuted);
    g.setFont (theme::label (7.0f));
    g.drawText ("MIN : SEC", juce::Rectangle<int> (static_cast<int> (x) + 90, transportArea.getY() + 34, 80, 12), juce::Justification::centredLeft, false);

    auto meter = juce::Rectangle<int> (transportArea.getX() + 410, transportArea.getY() + 20, 86, 16);
    g.setColour (theme::textMuted);
    g.setFont (theme::label (7.0f));
    g.drawText ("MASTER", meter.withY (transportArea.getY() + 8).withHeight (10), juce::Justification::centredLeft, false);
    g.setColour (theme::bg);
    g.fillRect (meter);

    const auto level = juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (engine.masterPeak(), -60.0f) / 60.0f + 1.0f);
    g.setColour (level > 0.95f ? theme::red : theme::accent);
    g.fillRect (meter.removeFromLeft (static_cast<int> (level * 86.0f)));
}

void MainComponent::paintTools (juce::Graphics& g)
{
    g.setColour (theme::bg);
    g.fillRect (toolArea);
    theme::hairline (g, 0.0f, static_cast<float> (toolArea.getBottom()), static_cast<float> (getWidth()), static_cast<float> (toolArea.getBottom()));

    for (int index = 0; index < 6; ++index)
    {
        auto cell = juce::Rectangle<int> (toolArea.getX() + 10 + index * 66, toolArea.getY(), 62, toolArea.getHeight());
        const auto active = static_cast<int> (tool) == index;

        g.setColour (active ? theme::accentHi : theme::textMuted);
        g.setFont (theme::label (8.0f));
        g.drawText (kTools[index], cell, juce::Justification::centred, false);

        if (active)
        {
            g.setColour (theme::accent);
            g.fillRect (cell.removeFromBottom (2));
        }
    }

    for (int index = 0; index < 4; ++index)
    {
        auto cell = juce::Rectangle<int> (modeArea.getX() + index * 56, modeArea.getY(), 54, modeArea.getHeight());
        const auto active = static_cast<int> (mode) == index;

        g.setColour (active ? theme::accentHi : theme::textMuted);
        g.setFont (theme::label (7.5f));
        g.drawText (kModes[index], cell, juce::Justification::centred, false);

        if (active)
        {
            g.setColour (theme::sky);
            g.fillRect (cell.removeFromBottom (2));
        }
    }
}

void MainComponent::paintEdit (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (headerArea);
    g.setColour (theme::border);
    g.drawVerticalLine (headerArea.getRight() - 1, static_cast<float> (headerArea.getY()), static_cast<float> (headerArea.getBottom()));

    auto ruler = juce::Rectangle<int> (laneArea.getX(), laneArea.getY(), laneArea.getWidth(), 22);
    g.setColour (theme::bg);
    g.fillRect (laneArea);
    g.setColour (theme::panel);
    g.fillRect (ruler);

    const auto bars = 16;
    const auto barWidth = static_cast<float> (laneArea.getWidth()) / static_cast<float> (bars);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto x = static_cast<float> (laneArea.getX()) + static_cast<float> (bar) * barWidth;

        g.setColour (theme::textMuted);
        g.setFont (theme::label (7.0f));
        g.drawText (juce::String (bar + 1), juce::Rectangle<int> (static_cast<int> (x) + 4, ruler.getY() + 4, 24, 14),
                    juce::Justification::centredLeft, false);

        g.setColour (bar % 4 == 0 ? theme::border : theme::border.withAlpha (0.5f));
        g.drawVerticalLine (static_cast<int> (x), static_cast<float> (ruler.getY()), static_cast<float> (laneArea.getBottom()));
    }

    const auto rowHeight = static_cast<float> (laneArea.getHeight() - 22) / static_cast<float> (AudioEngine::trackCount);

    for (int track = 0; track < AudioEngine::trackCount; ++track)
    {
        auto lane = juce::Rectangle<int> (laneArea.getX(), laneArea.getY() + 22 + static_cast<int> (track * rowHeight),
                                         laneArea.getWidth(), static_cast<int> (rowHeight));
        auto header = juce::Rectangle<int> (headerArea.getX(), lane.getY(), headerArea.getWidth() - 1, lane.getHeight());

        g.setColour (track == selectedTrack ? theme::raised : laneColour (track));
        g.fillRect (lane);
        g.setColour (track == selectedTrack ? juce::Colour (0x14ffffff) : theme::panel);
        g.fillRect (header);
        theme::hairline (g, 0.0f, static_cast<float> (lane.getBottom()), static_cast<float> (getWidth()), static_cast<float> (lane.getBottom()));

        g.setColour (theme::textMuted);
        g.setFont (theme::label (7.0f));
        g.drawText (juce::String (track + 1).paddedLeft ('0', 2), header.reduced (8, 0).removeFromTop (16),
                    juce::Justification::centredRight, false);

        g.setColour (theme::text);
        g.setFont (theme::ui (10.5f));
        g.drawText (kTrackNames[track], header.reduced (8, 0).withTrimmedTop (4).withTrimmedRight (26).removeFromTop (16),
                    juce::Justification::centredLeft, true);

        const auto buttonY = header.getY() + 22;

        g.setColour (armed[track] ? theme::red : theme::textMuted);
        if (armed[track])
            g.fillEllipse (static_cast<float> (header.getX() + 9), static_cast<float> (buttonY + 1), 8.0f, 8.0f);
        else
            g.drawEllipse (static_cast<float> (header.getX() + 9), static_cast<float> (buttonY + 1), 8.0f, 8.0f, 1.0f);

        auto drawBox = [&] (int offset, const char* text, bool on, juce::Colour colour)
        {
            auto box = juce::Rectangle<int> (header.getX() + offset, buttonY, 18, 12);
            g.setColour (on ? colour : theme::bg);
            g.fillRect (box);
            g.setColour (theme::border);
            g.drawRect (box, 1);
            g.setColour (on ? theme::bg : theme::textMuted);
            g.setFont (theme::label (6.5f));
            g.drawText (text, box, juce::Justification::centred, false);
        };

        drawBox (24, "S", engine.trackSolo (track), theme::sky);
        drawBox (46, "M", engine.trackMuted (track), theme::gold);

        const auto gain = juce::jlimit (0.0f, 1.0f, engine.trackGain (track) / 1.2f);
        auto faderLine = juce::Rectangle<int> (header.getX() + 8, header.getBottom() - 12, header.getWidth() - 18, 4);
        g.setColour (theme::border);
        g.drawHorizontalLine (faderLine.getCentreY(), static_cast<float> (faderLine.getX()), static_cast<float> (faderLine.getRight()));
        g.setColour (theme::accent);
        g.fillRect (juce::Rectangle<int> (faderLine.getX() + static_cast<int> (gain * static_cast<float> (faderLine.getWidth())) - 1,
                                         faderLine.getY(), 3, faderLine.getHeight()));
    }

    const auto loopSeconds = 64.0 * 60.0 / juce::jmax (1.0, engine.getBpm());
    const auto fraction = std::fmod (engine.positionSeconds(), loopSeconds) / loopSeconds;
    const auto playX = static_cast<float> (laneArea.getX()) + static_cast<float> (fraction) * static_cast<float> (laneArea.getWidth());

    g.setColour (theme::sky);
    g.drawVerticalLine (static_cast<int> (playX), static_cast<float> (laneArea.getY()), static_cast<float> (laneArea.getBottom()));
    juce::Path marker;
    marker.addTriangle (playX - 4.0f, static_cast<float> (laneArea.getY()), playX + 4.0f, static_cast<float> (laneArea.getY()),
                        playX, static_cast<float> (laneArea.getY() + 7));
    g.fillPath (marker);
}

void MainComponent::paintMix (juce::Graphics& g)
{
    g.setColour (theme::bg);
    g.fillRect (mixArea);

    g.setColour (theme::panel);
    g.fillRect (masterArea);
    g.setColour (theme::border);
    g.drawRect (masterArea, 1);

    auto inner = masterArea.reduced (6);
    g.setColour (theme::textMuted);
    g.setFont (theme::label (8.0f));
    g.drawText ("MASTER", inner.removeFromTop (18), juce::Justification::centred, false);

    auto meterArea = inner.removeFromRight (8).withTrimmedTop (30).withTrimmedBottom (28);
    g.setColour (theme::bg);
    g.fillRect (meterArea);
    const auto level = juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (engine.masterPeak(), -60.0f) / 60.0f + 1.0f);
    auto lit = meterArea.withTrimmedTop (static_cast<int> ((1.0f - level) * static_cast<float> (meterArea.getHeight())));
    g.setColour (level > 0.95f ? theme::red : theme::accent);
    g.fillRect (lit);
}

void MainComponent::paintPanel (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (panelArea);
    g.setColour (theme::border);
    g.drawVerticalLine (panelArea.getX(), static_cast<float> (panelArea.getY()), static_cast<float> (panelArea.getBottom()));

    auto header = panelArea.reduced (8, 0).removeFromTop (26);
    g.setColour (theme::accentHi);
    g.setFont (theme::label (8.0f));
    g.drawText ("PLUG-INS", header.removeFromLeft (90), juce::Justification::centredLeft, false);

    g.setColour (theme::textMuted);
    g.setFont (theme::label (7.5f));
    g.drawText (juce::String (registry.plugins.size()) + " STOCK", header, juce::Justification::centredRight, false);

    theme::hairline (g, static_cast<float> (panelArea.getX()), static_cast<float> (panelArea.getY() + 26),
                     static_cast<float> (panelArea.getRight()), static_cast<float> (panelArea.getY() + 26));
}

void MainComponent::paintStatus (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (statusArea);
    theme::hairline (g, 0.0f, static_cast<float> (statusArea.getY()), static_cast<float> (getWidth()), static_cast<float> (statusArea.getY()));

    g.setColour (theme::textMuted);
    g.setFont (theme::label (7.5f));
    g.drawText (view == View::edit ? "EDIT WINDOW  ·  16 BARS  ·  4/4" : "MIX WINDOW  ·  8 CHANNELS",
                statusArea.reduced (10, 0), juce::Justification::centredLeft, false);

    g.drawText ("NCSOUND MUSIC HUB  ·  " + engine.deviceDescription(),
                statusArea.reduced (10, 0), juce::Justification::centredRight, false);
}

void MainComponent::mouseDown (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    if (toolArea.contains (position))
    {
        const auto index = (position.x - toolArea.getX() - 10) / 66;

        if (juce::isPositiveAndBelow (index, 6))
        {
            tool = static_cast<Tool> (index);
            repaint (toolArea);
        }

        return;
    }

    if (modeArea.contains (position))
    {
        const auto index = (position.x - modeArea.getX()) / 56;

        if (juce::isPositiveAndBelow (index, 4))
        {
            mode = static_cast<Mode> (index);
            repaint (toolArea);
        }

        return;
    }

    if (view == View::edit && headerArea.contains (position))
    {
        const auto track = trackAt (position);

        if (track < 0)
            return;

        selectedTrack = track;
        const auto rowHeight = static_cast<float> (laneArea.getHeight() - 22) / static_cast<float> (AudioEngine::trackCount);
        const auto rowY = laneArea.getY() + 22 + static_cast<int> (track * rowHeight);
        const auto buttonY = rowY + 22;

        if (juce::Rectangle<int> (headerArea.getX() + 8, buttonY, 12, 12).contains (position))
            armed[track] = ! armed[track];
        else if (juce::Rectangle<int> (headerArea.getX() + 24, buttonY, 18, 12).contains (position))
            engine.setTrackSolo (track, ! engine.trackSolo (track));
        else if (juce::Rectangle<int> (headerArea.getX() + 46, buttonY, 18, 12).contains (position))
            engine.setTrackMuted (track, ! engine.trackMuted (track));

        repaint();
        return;
    }

    if (view == View::edit && laneArea.contains (position))
    {
        const auto track = trackAt (position);

        if (track >= 0)
        {
            selectedTrack = track;
            repaint();
        }
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    menuArea = area.removeFromTop (22);
    transportArea = area.removeFromTop (54);
    toolArea = area.removeFromTop (28);
    statusArea = area.removeFromBottom (20);

    modeArea = juce::Rectangle<int> (toolArea.getRight() - 240, toolArea.getY(), 232, toolArea.getHeight());

    if (view == View::edit)
    {
        headerArea = area.removeFromLeft (200);
        panelArea = panelOpen ? area.removeFromRight (260) : juce::Rectangle<int>();
        laneArea = area;
        mixArea = {};
        masterArea = {};
    }
    else
    {
        mixArea = area;
        headerArea = {};
        laneArea = {};
        panelArea = {};
    }

    auto transport = transportArea.reduced (8, 10);
    rtz.setBounds (transport.removeFromLeft (30));
    transport.removeFromLeft (2);
    rew.setBounds (transport.removeFromLeft (30));
    transport.removeFromLeft (2);
    stopButton.setBounds (transport.removeFromLeft (44));
    transport.removeFromLeft (2);
    playButton.setBounds (transport.removeFromLeft (46));
    transport.removeFromLeft (2);
    recordButton.setBounds (transport.removeFromLeft (40));

    auto right = transportArea.reduced (8, 10);
    mixButton.setBounds (right.removeFromRight (42));
    right.removeFromRight (4);
    editButton.setBounds (right.removeFromRight (42));
    right.removeFromRight (10);
    pluginsButton.setBounds (right.removeFromRight (64));
    right.removeFromRight (10);
    metronomeButton.setBounds (right.removeFromRight (52));
    right.removeFromRight (8);
    bpm.setBounds (right.removeFromRight (58));

    browser.setBounds (panelArea.withTrimmedTop (28).reduced (6));

    if (view == View::mix)
    {
        auto stripsArea = mixArea.reduced (8, 10);
        masterArea = stripsArea.removeFromRight (108);
        const auto width = juce::jmin (104, stripsArea.getWidth() / AudioEngine::trackCount);

        for (auto& strip : strips)
            strip->setBounds (stripsArea.removeFromLeft (width).reduced (2, 0));

        masterFader.setBounds (masterArea.reduced (10, 36).withTrimmedRight (22));
    }
}
} // namespace nc
// end of file
