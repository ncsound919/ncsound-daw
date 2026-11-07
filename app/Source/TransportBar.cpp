#include "TransportBar.h"

namespace nc
{
namespace
{
void addSeparator (juce::Graphics& g, int x, int top, int bottom)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawVerticalLine (x, static_cast<float> (top), static_cast<float> (bottom));
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.drawVerticalLine (x + 1, static_cast<float> (top), static_cast<float> (bottom));
}
} // namespace

TransportBar::TransportBar (AudioEngine& engineToUse)
    : engine (engineToUse)
{
    auto add = [this] (juce::Component& component)
    {
        addAndMakeVisible (component);
    };

    add (rewindButton);
    add (stopButton);
    add (playButton);
    add (recordButton);
    add (loopButton);
    add (metronomeButton);
    add (browserButton);
    add (viewSwitch);
    add (positionLcd);
    add (tempoLcd);
    add (signatureLcd);
    add (masterLeftMeter);
    add (masterRightMeter);

    for (auto* lcd : { &positionLcd, &tempoLcd, &signatureLcd })
        lcd->setInterceptsMouseClicks (false, false);

    masterLeftMeter.setInterceptsMouseClicks (false, false);
    masterRightMeter.setInterceptsMouseClicks (false, false);

    recordButton.setClickingTogglesState (true);
    recordButton.setDanger (true);

    rewindButton.setTooltip ("Return to bar 1 (Home)");
    stopButton.setTooltip ("Stop and return to bar 1");
    playButton.setTooltip ("Play or stop (Space)");
    recordButton.setTooltip ("Record (not wired in this build)");
    loopButton.setTooltip ("Loop region on the timeline");
    metronomeButton.setTooltip ("Metronome click");
    browserButton.setTooltip ("Plug-in browser (B)");
    loopButton.setClickingTogglesState (true);
    loopButton.setToggleState (true, juce::dontSendNotification);
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setToggleState (engine.isMetronomeOn(), juce::dontSendNotification);
    browserButton.setClickingTogglesState (true);

    playButton.onClick = [this]
    {
        engine.setPlaying (! engine.isPlaying());
        refreshTransportState();
    };

    stopButton.onClick = [this]
    {
        engine.setPlaying (false);
        engine.resetPosition();
        recording = false;
        recordButton.setToggleState (false, juce::dontSendNotification);
        refreshTransportState();
    };

    rewindButton.onClick = [this]
    {
        engine.resetPosition();
        refreshTransportState();
    };

    recordButton.onClick = [this]
    {
        // Nothing captures audio yet, so the button must not claim it does.
        recordButton.setToggleState (false, juce::dontSendNotification);
        recording = false;
        refreshTransportState();

        if (onStatus)
            onStatus ("Recording is not wired in this build / no input is being captured");
    };

    metronomeButton.onClick = [this]
    {
        engine.setMetronome (metronomeButton.getToggleState());
    };

    loopButton.onClick = [this]
    {
        engine.setLoopEnabled (loopButton.getToggleState());
        loopButton.repaint();

        if (onStatus)
            onStatus (loopButton.getToggleState()
                          ? "Loop on / playback repeats the arrange loop region"
                          : "Loop off / playback continues past the loop region");
    };

    browserButton.onClick = [this]
    {
        browserOpen = browserButton.getToggleState();

        if (onBrowserToggled)
            onBrowserToggled();
    };

    viewSwitch.setItems ({ "ARRANGE", "MIX" });
    viewSwitch.setSelected (0);
    viewSwitch.onChange = [this] (int index)
    {
        mixView = index == 1;

        if (onViewChanged)
            onViewChanged (mixView);
    };

    masterLeftMeter.setSource ([this]
    {
        return theme::levelFromGain (engine.masterPeakLeft());
    });
    masterRightMeter.setSource ([this]
    {
        return theme::levelFromGain (engine.masterPeakRight());
    });

    positionLcd.setFontHeight (28.0f);
    tempoLcd.setFontHeight (28.0f);
    signatureLcd.setFontHeight (28.0f);

    refreshTransportState();
    startTimerHz (30);
}

void TransportBar::setView (bool mix)
{
    mixView = mix;
    viewSwitch.setSelected (mix ? 1 : 0, false);
}

void TransportBar::setBrowserOpen (bool shouldBeOpen)
{
    browserOpen = shouldBeOpen;
    browserButton.setToggleState (browserOpen, juce::dontSendNotification);
    browserButton.repaint();
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (16, 12);

    auto left = area.removeFromLeft (292);
    const auto centreY = left.getCentreY();

    auto place = [&left, centreY] (juce::Component& component, int width, int height)
    {
        component.setBounds (juce::Rectangle<int> (left.removeFromLeft (width).getX(), centreY - height / 2, width, height));
        left.removeFromLeft (8);
    };

    place (rewindButton, 38, 38);
    place (stopButton, 38, 38);
    place (playButton, 52, 52);
    place (recordButton, 44, 44);
    left.removeFromLeft (4);
    place (loopButton, 38, 38);
    place (metronomeButton, 38, 38);

    auto right = area.removeFromRight (330);
    const auto rightCentreY = right.getCentreY();
    auto placeRight = [&right, rightCentreY] (juce::Component& component, int width, int height)
    {
        component.setBounds (right.removeFromRight (width).withY (rightCentreY - height / 2).withHeight (height));
        right.removeFromRight (10);
    };

    placeRight (viewSwitch, 184, 38);
    placeRight (browserButton, 110, 38);

    area.removeFromLeft (16);

    auto positionArea = area.removeFromLeft (196);
    positionLcd.setBounds (positionArea.withSizeKeepingCentre (196, 68));
    area.removeFromLeft (14);

    auto tempo = area.removeFromLeft (120);
    tempoLcd.setBounds (tempo.withSizeKeepingCentre (120, 68));
    tempoArea = tempoLcd.getBounds();
    area.removeFromLeft (14);

    auto signature = area.removeFromLeft (80);
    signatureLcd.setBounds (signature.withSizeKeepingCentre (80, 68));
    area.removeFromLeft (16);

    masterArea = area.removeFromLeft (juce::jmin (300, area.getWidth()));

    auto meterArea = masterArea.reduced (10, 8);
    meterArea.removeFromTop (16);
    auto leftMeter = meterArea.removeFromTop (9);
    meterArea.removeFromTop (6);
    auto rightMeter = meterArea.removeFromTop (9);

    masterLeftMeter.setBounds (leftMeter.withTrimmedRight (50));
    masterRightMeter.setBounds (rightMeter.withTrimmedRight (50));
}

void TransportBar::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    juce::ColourGradient background (theme::panelHi, area.getX(), area.getY(),
                                     theme::panelLo, area.getX(), area.getBottom(), false);
    background.addColour (0.28, juce::Colour (0xff272727));
    g.setGradientFill (background);
    g.fillRect (area);

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (0, area.getX(), area.getRight());
    g.setTiledImageFill (theme::grainTexture(), 0, 0, 0.45f);
    g.fillRect (area);
    theme::hairline (g, 0.0f, area.getBottom() - 1.0f, area.getRight(), area.getBottom() - 1.0f);

    addSeparator (g, 300, 12, getHeight() - 12);
    addSeparator (g, getWidth() - 348, 12, getHeight() - 12);

    // Master readout module.
    if (! masterArea.isEmpty())
    {
        auto master = masterArea.toFloat().withTrimmedTop (2.0f).withTrimmedBottom (2.0f);
        theme::wellFace (g, master, theme::radius::control);

        auto inner = master.reduced (10.0f, 6.0f);
        auto header = inner.removeFromTop (16.0f);

        g.setColour (theme::muted);
        g.setFont (theme::label (11.0f));
        g.drawText ("MASTER", header.removeFromLeft (76.0f).toNearestInt(), juce::Justification::centredLeft, false);

        g.setColour (theme::paperDim);
        g.setFont (theme::label ());
        g.drawText ("L", header.removeFromLeft (12.0f).toNearestInt(), juce::Justification::centred, false);
        g.drawText ("R", header.removeFromLeft (12.0f).toNearestInt(), juce::Justification::centred, false);

        const auto db = theme::dbString (engine.masterPeak());
        g.setColour (engine.masterPeak() > 0.94f ? theme::danger : theme::accentHot);
        g.setFont (theme::clock (13.0f));
        g.drawText (db, header.toNearestInt(), juce::Justification::centredRight, false);

        g.setColour (theme::muted);
        g.setFont (theme::label ());
        g.drawText ("dBFS", inner.toNearestInt(), juce::Justification::bottomRight, false);
    }
}

void TransportBar::mouseDown (const juce::MouseEvent& event)
{
    if (tempoArea.contains (event.getPosition()))
    {
        draggingTempo = true;
        tempoAtDragStart = engine.getBpm();
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
}

void TransportBar::mouseDrag (const juce::MouseEvent& event)
{
    if (! draggingTempo)
        return;

    const auto scale = event.mods.isShiftDown() ? 0.04 : 0.22;
    const auto next = tempoAtDragStart + static_cast<double> (event.getDistanceFromDragStartY()) * -scale;
    engine.setBpm (next);
    refreshTransportState();
}

void TransportBar::mouseUp (const juce::MouseEvent&)
{
    draggingTempo = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void TransportBar::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (tempoArea.contains (event.getPosition()))
    {
        engine.setBpm (120.0);
        refreshTransportState();
    }
}

void TransportBar::mouseMove (const juce::MouseEvent& event)
{
    setMouseCursor (tempoArea.contains (event.getPosition()) && ! draggingTempo
                        ? juce::MouseCursor::UpDownResizeCursor
                        : juce::MouseCursor::NormalCursor);
}

void TransportBar::timerCallback()
{
    refreshTransportState();
}

void TransportBar::refreshTransportState()
{
    playButton.setToggleState (engine.isPlaying(), juce::dontSendNotification);
    recordButton.setToggleState (recording, juce::dontSendNotification);

    const auto position = barsBeats (engine.positionSeconds(), engine.getBpm());
    const auto seconds = timecode (engine.positionSeconds());

    if (position != lastPositionMain || seconds != lastPositionSmall)
    {
        lastPositionMain = position;
        lastPositionSmall = seconds;
        positionLcd.setText (position, seconds, "BARS / BEATS");
    }

    const auto tempo = juce::String (engine.getBpm(), 1);
    
    if (tempo != lastTempo)
    {
        lastTempo = tempo;
        tempoLcd.setText (tempo, "BPM", "TEMPO");
    }

    signatureLcd.setText ("4/4", "4 BEATS", "SIGNATURE");

    const auto db = theme::dbString (engine.masterPeak());

    if (db != lastMasterDb)
    {
        lastMasterDb = db;
        repaint (masterArea);
    }
}
} // namespace nc
