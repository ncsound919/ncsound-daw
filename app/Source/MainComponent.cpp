#include "MainComponent.h"

#include "MixRackEditor.h"
#include "MixologyBridge.h"
#include "MixPlan.h"

#include <thread>
#include "ProjectFile.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace nc
{
namespace
{
const juce::StringArray menuNames { "FILE", "EDIT", "VIEW", "TRACK", "HELP" };

juce::String clockNow()
{
    const auto now = juce::Time::getCurrentTime();
    return juce::String::formatted ("%02d:%02d", now.getHours(), now.getMinutes());
}

void drawLed (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, bool on)
{
    theme::led (g, centre, radius, colour, on);
}
} // namespace

MainComponent::MainComponent (juce::File registryFile)
    : registry (loadRegistry (registryFile)),
      engine(),
      transport (engine),
      arrange (engine),
      mixer (engine),
      browser (registry)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);

    // The engine catalog shares the browser's registry rows, so each suite
    // plugin is one row that loads its installed VST3 under a stable id.
    engine.pluginCatalog().setRegistryEntries (registry.plugins);

    addAndMakeVisible (transport);
    addAndMakeVisible (arrange);
    addAndMakeVisible (mixer);
    addChildComponent (browser);
    addAndMakeVisible (newButton);
    addAndMakeVisible (settingsButton);

    arrange.onTrackSelected = [this] (int track)
    {
        setSelectedTrack (track);
    };

    arrange.onInsertClicked = [this] (int track, int slot)
    {
        setBrowserOpen (true, track, slot);
    };

    arrange.onStripClicked = [this] (int track)
    {
        openMixRackEditor (track);
    };

    arrange.onStatus = [this] (const juce::String& message)
    {
        setStatus (message);
    };

    mixer.onTrackSelected = [this] (int track)
    {
        setSelectedTrack (track);
    };

    mixer.onInsertClicked = [this] (int track, int slot)
    {
        setBrowserOpen (true, track, slot);
    };

    mixer.onStripClicked = [this] (int track)
    {
        openMixRackEditor (track);
    };

    mixer.onMasterRackClicked = [this]
    {
        openMixRackEditor (-1);
    };

    mixer.onMasterInsertClicked = [this]
    {
        openMasterInsertMenu();
    };

    transport.onViewChanged = [this] (bool mix)
    {
        setView (mix ? View::mix : View::arrange);
    };

    transport.onBrowserToggled = [this]
    {
        setBrowserOpen (! browserOpen);
    };

    transport.onStatus = [this] (const juce::String& message)
    {
        setStatus (message, false, 5.0);
    };

    transport.onRecordRequest = [this] (bool shouldRecord)
    {
        if (shouldRecord)
            startRecording();
        else
            finishRecording();
    };

    browser.onClose = [this]
    {
        setBrowserOpen (false);
    };

    browser.onStatus = [this] (const juce::String& message)
    {
        setStatus (message);
    };

    browser.onScan = [this]
    {
        browser.setScanState (true, 0.0f);
        setStatus ("SCAN / VST3 folders...", false, 2.0);

        engine.pluginCatalog().scanAsync (
            [safe = juce::Component::SafePointer<MainComponent> (this)] (bool success, juce::String message)
            {
                if (safe == nullptr)
                    return;

                safe->browser.setExternalPlugins (safe->engine.pluginCatalog().asPluginInfo());
                safe->browser.setScanState (false);
                safe->browser.setStatusLine ("SCAN / " + message);
                safe->setStatus ("SCAN / " + message, ! success);
            });
    };

    browser.onLoad = [this] (const PluginInfo& plugin)
    {
        if (plugin.status != PluginStatus::external)
        {
            setStatus (plugin.name + " CANNOT LOAD / " + statusName (plugin.status), true);
            return;
        }

        juce::String error;

        if (plugin.isInstrument())
        {
            if (! engine.loadTrackInstrumentFromId (selectedTrack, plugin.id, error))
            {
                setStatus (plugin.name + " / " + error, true);
                return;
            }

            setStatus ("LOADING " + plugin.name + " as instrument on " + trackName (selectedTrack), false, 3.0);
            setBrowserOpen (false);
            return;
        }

        if (browserSlot < 0 || browserSlot > 3)
        {
            setStatus ("Choose a user insert slot (1-4), not the native strip", true);
            return;
        }

        if (! engine.loadTrackInsertFromId (selectedTrack, browserSlot, plugin.id, error))
        {
            setStatus (plugin.name + " / " + error, true);
            return;
        }

        setStatus ("LOADING " + plugin.name + " on " + trackName (selectedTrack)
                       + " / INS " + juce::String (browserSlot + 1), false, 3.0);
        setBrowserOpen (false);
    };

    engine.onPluginStatus = [this] (juce::String message)
    {
        setStatus (std::move (message), false, 4.0);
    };

    // Link installed bundles to their registry rows in the background; a clicked
    // row still resolves its own bundle if this has not finished yet.
    engine.pluginCatalog().scanAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                      (bool, juce::String)
    {
        if (safe == nullptr)
            return;

        safe->browser.setExternalPlugins (safe->engine.pluginCatalog().asPluginInfo());
        safe->browser.setScanState (false);
        safe->browser.setStatusLine (safe->engine.pluginCatalog().lastScanStatus());
    });

    newButton.setCompact (true);
    newButton.setTooltip ("New session (not wired in this build)");
    newButton.onClick = [this]
    {
        engine.setPlaying (false);
        engine.resetPosition();
        setStatus ("New session is not wired in this build / transport returned to bar 1", true);
    };

    settingsButton.setCompact (true);
    settingsButton.setTooltip ("Preferences (not wired in this build)");
    settingsButton.onClick = [this]
    {
        setStatus ("Preferences are not wired in this build / audio follows the system device");
    };

    auto& look = juce::LookAndFeel::getDefaultLookAndFeel();
    look.setColour (juce::PopupMenu::backgroundColourId, theme::panel);
    look.setColour (juce::PopupMenu::textColourId, theme::paper);
    look.setColour (juce::PopupMenu::headerTextColourId, theme::muted);
    look.setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.28f));
    look.setColour (juce::PopupMenu::highlightedTextColourId, theme::paper);
    look.setColour (juce::TooltipWindow::backgroundColourId, theme::panel);
    look.setColour (juce::TooltipWindow::textColourId, theme::paper);
    look.setColour (juce::TooltipWindow::outlineColourId, theme::lineStrong);
    look.setColour (juce::ScrollBar::thumbColourId, theme::panelHi);

    statusMessage = defaultStatusText();
    setView (View::arrange);
    setSelectedTrack (0);
    startTimerHz (30);
}

juce::String MainComponent::trackName (int index) const
{
    return sessionTracks()[static_cast<size_t> (nc::limits::clampTrack (index, AudioEngine::trackCount))].name;
}

juce::String MainComponent::defaultStatusText() const
{
    return "READY  /  " + juce::String (sessionClips().size()) + " CLIPS  /  "
         + juce::String (registry.plugins.size()) + " PLUG-INS INDEXED  /  "
         + juce::String (engine.getActiveTrackCount()) + " OF "
         + juce::String (AudioEngine::trackCount) + " CHANNELS";
}

void MainComponent::addTrack()
{
    const auto current = engine.getActiveTrackCount();
    const auto next = nc::limits::nextActiveTracks (current, 1);

    if (next == current)
    {
        setStatus ("TRACK LIMIT / " + juce::String (current) + " OF "
                       + juce::String (AudioEngine::trackCount) + " TRACKS IN THIS SESSION", true);
        return;
    }

    engine.setActiveTrackCount (next);
    mixer.resized();
    arrange.refresh();
    setSelectedTrack (next - 1);
    setStatus ("ADDED " + trackName (next - 1) + " / " + juce::String (next) + " OF "
                   + juce::String (AudioEngine::trackCount) + " TRACKS");
}

void MainComponent::removeTrack()
{
    const auto current = engine.getActiveTrackCount();
    const auto next = nc::limits::nextActiveTracks (current, -1);

    if (next == current)
    {
        setStatus ("CANNOT REMOVE THE LAST TRACK", true);
        return;
    }

    engine.setActiveTrackCount (next);
    mixer.resized();
    setSelectedTrack (juce::jmin (selectedTrack, next - 1));
    arrange.refresh();
    setStatus ("REMOVED TRACK / " + juce::String (next) + " TRACKS LEFT");
}

void MainComponent::setView (View newView)
{
    view = newView;
    arrange.setVisible (view == View::arrange);
    mixer.setVisible (view == View::mix);
    transport.setView (view == View::mix);

    // Keep the visible surface on top even if a child was raised earlier.
    if (view == View::mix)
        mixer.toFront (false);
    else
        arrange.toFront (false);

    if (view == View::mix)
        mixer.syncFromEngine();
    else
        arrange.refresh();

    repaint (bodyArea);
    repaint (statusArea);
}

void MainComponent::setSelectedTrack (int track)
{
    selectedTrack = juce::jlimit (0, juce::jmax (0, engine.getActiveTrackCount() - 1), track);
    arrange.setSelectedTrack (selectedTrack);
    mixer.setSelectedTrack (selectedTrack);
}

void MainComponent::setBrowserOpen (bool shouldOpen, int targetTrack, int targetSlot)
{
    if (shouldOpen && targetTrack >= 0)
    {
        browserSlot = juce::jmax (0, targetSlot);
        setSelectedTrack (targetTrack);
    }

    browserOpen = shouldOpen;

    if (browserOpen)
    {
        browser.setTarget (trackName (selectedTrack), browserSlot);
        browser.refresh();
        browser.toFront (false);
        browser.setVisible (true);
        setStatus ("Browser open / target " + trackName (selectedTrack) + " / INS "
                       + juce::String (browserSlot + 1), false, 4.0);
    }
    else
    {
        browser.setVisible (false);
    }

    transport.setBrowserOpen (browserOpen);
    repaint (statusArea);
}

void MainComponent::importAudioFile()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Import audio",
                                                        juce::File(),
                                                        "*.wav;*.aif;*.aiff;*.flac");
    auto safe = juce::Component::SafePointer<MainComponent> (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safe, chooser] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;

                              const auto file = fc.getResult();

                              if (file.existsAsFile())
                                  safe->arrange.importAudioFile (file);
                          });
}

void MainComponent::setStatus (juce::String message, bool alert, double seconds)
{
    statusMessage = message.isNotEmpty() ? std::move (message) : defaultStatusText();
    statusAlert = alert;
    statusUntil = seconds > 0.0 ? juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0 : 0.0;
    repaint (statusArea);
}

//==============================================================================
void MainComponent::resized()
{
    auto area = getLocalBounds();

    headerArea = area.removeFromTop (44);
    transportArea = area.removeFromTop (100);
    statusArea = area.removeFromBottom (30);
    bodyArea = area;

    transport.setBounds (transportArea);
    arrange.setBounds (bodyArea);
    mixer.setBounds (bodyArea);
    browser.setBounds (bodyArea);

    auto header = headerArea.reduced (16, 0);
    logoArea = header.removeFromLeft (216);
    header.removeFromLeft (8);

    for (auto& menu : menuAreas)
        menu = header.removeFromLeft (58);

    auto right = headerArea.reduced (16, 0);
    settingsButton.setBounds (right.removeFromRight (34).withSizeKeepingCentre (34, 30));
    right.removeFromRight (10);
    newButton.setBounds (right.removeFromRight (92).withSizeKeepingCentre (92, 30));
    right.removeFromRight (16);

    cpuArea = right.removeFromRight (140);
    headerRightArea = right;

    statusMessageArea = statusArea.reduced (14, 0).removeFromLeft (juce::jmin (500, statusArea.getWidth() / 2));
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::ink);

    const auto area = getLocalBounds().toFloat();
    juce::ColourGradient background (juce::Colour (0xff171513), area.getX(), area.getY(),
                                     theme::ink, area.getX(), area.getBottom(), false);
    g.setGradientFill (background);
    g.fillRect (area);

    paintHeader (g);
    paintStatus (g);
}

void MainComponent::paintHeader (juce::Graphics& g)
{
    const auto area = headerArea.toFloat();

    juce::ColourGradient background (juce::Colour (0xff2c2926), area.getX(), area.getY(),
                                     juce::Colour (0xff181716), area.getX(), area.getBottom(), false);
    background.addColour (0.55, juce::Colour (0xff22201e));
    g.setGradientFill (background);
    g.fillRect (area);

    g.setTiledImageFill (theme::grainTexture(), 0, 0, 0.5f);
    g.fillRect (area);

    g.setColour (theme::accent.withAlpha (0.62f));
    g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), area.getWidth(), 1.5f));
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.drawHorizontalLine (area.getBottom() - 1.0f, area.getX(), area.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawHorizontalLine (area.getBottom(), area.getX(), area.getRight());

    // Lockup.
    {
        auto logo = logoArea.toFloat().reduced (0.0f, 4.0f);
        auto monogram = logo.removeFromLeft (32.0f).withSizeKeepingCentre (28.0f, 28.0f);
        paintMonogram (g, monogram, theme::accentHot);

        logo.removeFromLeft (10.0f);

        g.setColour (theme::paper);
        g.setFont (theme::display (16.0f));
        g.drawText ("NCSOUND", logo.removeFromTop (19.0f).toNearestInt(), juce::Justification::centredLeft, false);

        g.setColour (theme::muted);
        g.setFont (theme::label (10.5f));
        g.drawText ("MUSIC HUB  /  DAW", logo.toNearestInt(), juce::Justification::centredLeft, false);
    }

    // Menus.
    for (int i = 0; i < static_cast<int> (menuAreas.size()); ++i)
    {
        auto menu = menuAreas[static_cast<size_t> (i)].toFloat().reduced (0.0f, 13.0f);

        if (hoverMenu == i)
        {
            g.setColour (theme::accent.withAlpha (0.15f));
            g.fillRoundedRectangle (menu, theme::radius::tight);
            g.setColour (theme::accent.withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float> (menu.getX() + 8.0f, menu.getBottom() - 1.0f,
                                                menu.getWidth() - 16.0f, 1.0f));
        }

        g.setColour (hoverMenu == i ? theme::paper : theme::paperDim);
        g.setFont (theme::label (12.0f));
        g.drawText (menuNames[i], menuAreas[static_cast<size_t> (i)], juce::Justification::centred, false);
    }

    // CPU meter.
    if (! cpuArea.isEmpty())
    {
        auto cpu = cpuArea.toFloat().withSizeKeepingCentre (static_cast<float> (cpuArea.getWidth()), 24.0f);
        auto caption = cpu.removeFromLeft (34.0f);
        const auto load = juce::jlimit (0.0f, 1.0f, engine.dspLoad());

        g.setColour (theme::muted);
        g.setFont (theme::label (10.5f));
        g.drawText ("DSP", caption.toNearestInt(), juce::Justification::centredLeft, false);

        auto percentage = cpu.removeFromRight (44.0f);
        g.setColour (theme::accentHot);
        g.setFont (theme::clock (12.0f));
        g.drawText (juce::String (juce::roundToInt (load * 100.0f)) + "%", percentage.toNearestInt(),
                    juce::Justification::centredRight, false);

        auto bars = cpu.reduced (1.0f, 6.0f);
        const auto count = 10;
        const auto gap = 1.5f;
        const auto width = juce::jmax (1.8f, (bars.getWidth() - gap * (count - 1)) / static_cast<float> (count));

        for (int i = 0; i < count; ++i)
        {
            const auto on = static_cast<float> (i + 1) / static_cast<float> (count) <= load;
            const auto colour = i >= count - 2 ? theme::meterHigh : i >= count - 4 ? theme::meterMid : theme::meterLow;
            g.setColour (on ? colour.withAlpha (0.92f) : juce::Colour (0xff151414));
            g.fillRect (juce::Rectangle<float> (bars.getX() + i * (width + gap), bars.getY(), width, bars.getHeight()));
        }
    }

    // Audio engine block: every value is read from the device, never printed.
    if (headerRightArea.getWidth() > 190)
    {
        auto block = headerRightArea.toFloat().withSizeKeepingCentre (static_cast<float> (juce::jmin (300, headerRightArea.getWidth())), 40.0f);
        block.setX (static_cast<float> (headerRightArea.getRight()) - block.getWidth());

        auto top = block.removeFromTop (18.0f);
        const auto open = engine.isDeviceOpen();
        drawLed (g, { top.getX() + 4.0f, top.getCentreY() }, 3.0f,
                 open ? theme::accentHot : theme::danger, true);

        g.setColour (open ? theme::paperDim : theme::danger);
        g.setFont (theme::label (10.5f));
        g.drawText (open ? "AUDIO ENGINE  ONLINE" : "NO AUDIO DEVICE",
                    top.withTrimmedLeft (16.0f).toNearestInt(), juce::Justification::centredLeft, false);

        g.setColour (theme::accentHot);
        g.setFont (theme::clock (12.0f));
        g.drawText (juce::String (engine.currentSampleRate() / 1000.0, 1) + " kHz / "
                        + juce::String (engine.currentBufferSize()) + " smp",
                    top.toNearestInt(), juce::Justification::centredRight, false);

        auto device = engine.deviceDescription();
        if (device.isEmpty())
            device = "SYSTEM DEFAULT DEVICE";

        g.setColour (theme::muted);
        g.setFont (theme::label (10.5f));
        g.drawText (device.toUpperCase(), block.toNearestInt(), juce::Justification::centredLeft, true);
    }

    // Right edge hardware screws, a small console tell.
    for (const auto y : { area.getY() + 10.0f, area.getBottom() - 10.0f })
    {
        g.setColour (juce::Colour (0xff0c0b0a));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ area.getRight() - 8.0f, y }));
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine (area.getRight() - 9.0f, y - 1.0f, area.getRight() - 7.0f, y + 1.0f, 0.7f);
    }
}

void MainComponent::paintStatus (juce::Graphics& g)
{
    const auto area = statusArea.toFloat();

    juce::ColourGradient background (juce::Colour (0xff1c1a19), area.getX(), area.getY(),
                                     juce::Colour (0xff111010), area.getX(), area.getBottom(), false);
    g.setGradientFill (background);
    g.fillRect (area);

    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawHorizontalLine (statusArea.getY(), area.getX(), area.getRight());
    g.setColour (theme::line);
    g.drawHorizontalLine (statusArea.getY() + 1, area.getX(), area.getRight());

    const auto accent = statusAlert ? theme::danger : theme::accentHot;
    drawLed (g, { area.getX() + 18.0f, area.getCentreY() }, 2.2f, accent, true);

    g.setColour (statusAlert ? theme::danger : theme::paperDim);
    g.setFont (theme::label (11.0f));
    g.drawText (statusMessage, statusMessageArea.withTrimmedLeft (16), juce::Justification::centredLeft, true);

    auto hints = statusArea.reduced (14, 0).withTrimmedLeft (juce::jmin (520, statusArea.getWidth() / 2)).withTrimmedRight (330);
    if (hints.getWidth() > 120)
    {
        g.setColour (theme::muted);
        g.setFont (theme::label (10.5f));
        g.drawText ("V SELECT  /  T TRIM  /  F FADE  /  G GAIN  /  X SPLIT  /  E ERASE",
                    hints, juce::Justification::centredLeft, true);
    }

    auto right = statusArea.reduced (16, 0);
    g.setColour (theme::muted);
    g.setFont (theme::label (10.5f));
    g.drawText (juce::String (engine.getActiveTrackCount()) + " OF "
                    + juce::String (AudioEngine::trackCount) + " TRACKS  /  "
                    + juce::String (sessionClips().size()) + " CLIPS  /  "
                    + juce::String (registry.plugins.size()) + " PLUG-INS  /  " + clockNow(),
                right, juce::Justification::centredRight, false);
}

//==============================================================================
void MainComponent::startRecording()
{
    juce::String error;
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile ("Recorded Takes");

    if (! engine.startRecording (folder, error))
    {
        setStatus ("RECORD / " + error, true);
        return;
    }

    // Recording captures from the current position (punch-in); with the
    // loop on, the audio thread flags punch-out at the loop end and the
    // timer finalises the take. Placement uses the take's start frame.
    engine.setPlaying (true);
    setStatus ("RECORDING / ARMED TRACKS CAPTURING / STOP TO PLACE TAKES", false, 0.0);
}

void MainComponent::finishRecording()
{
    if (! engine.isRecording())
        return;

    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile ("Recorded Takes");
    juce::String error;
    const auto takes = engine.stopRecording (folder, 24, error);

    if (takes.empty())
    {
        setStatus ("RECORD / " + (error.isEmpty() ? juce::String ("no audio captured") : error), true);
        return;
    }

    auto placed = 0;

    for (const auto& take : takes)
    {
        const auto startBar = 1.0 + static_cast<double> (take.startFrame)
                              / juce::jmax (1.0, engine.currentSampleRate())
                              / (240.0 / juce::jmax (1.0, engine.getBpm()));
        if (arrange.placeRecordedTake (take.file, take.track, startBar))
            ++placed;
    }

    setStatus ("TAKES PLACED / " + juce::String (placed) + " OF " + juce::String (static_cast<int> (takes.size())));
}

void MainComponent::timerCallback()
{
    ++tick;

    // Read-ahead refills stay on the message thread; the audio callback only
    // reads the window that is already resident.
    engine.serviceStreaming();
    engine.pollRetiredInserts();

    if (engine.consumePunchOut())
    {
        finishRecording();
        engine.setPlaying (false);
        setStatus ("PUNCH OUT / TAKE PLACED AT LOOP START");
    }

    if (engine.isMidiRecording())
        engine.drainMidiRecording();

    if (engine.pluginCatalog().isScanning())
        browser.setScanState (true, engine.pluginCatalog().scanProgress());

    const auto playing = engine.isPlaying();

    // Idle the meters and the header when the transport is parked, instead of
    // repainting the whole surface 30 times a second at rest.
    if (playing || tick % 15 == 0)
    {
        if (view == View::arrange)
            arrange.refresh();
        else
            mixer.syncFromEngine();
    }

    if (tick % (playing ? 4 : 15) == 0)
        repaint (headerArea);

    if (statusUntil > 0.0 && juce::Time::getMillisecondCounterHiRes() > statusUntil)
    {
        statusUntil = 0.0;
        statusAlert = false;
        statusMessage = defaultStatusText();
        repaint (statusArea);
    }

    // A missing output device is a fact worth stating once, not a silent mix.
    if (! deviceBannerShown && tick > 12 && ! engine.isDeviceOpen())
    {
        deviceBannerShown = true;
        setStatus ("NO AUDIO DEVICE / PLAYBACK IS SILENT UNTIL AN OUTPUT IS AVAILABLE", true, 0.0);
    }

    if (tick % 8 == 0)
        repaint (statusArea);

    if (hoverMenu >= 0 && ! headerArea.contains (getMouseXYRelative()))
    {
        hoverMenu = -1;
        repaint (headerArea);
    }
}

//==============================================================================
void MainComponent::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    if (headerArea.contains (event.getPosition()))
    {
        const auto menu = menuAt (event.getPosition());

        if (menu >= 0)
            openMenu (menu);
    }
}

void MainComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto menu = headerArea.contains (event.getPosition()) ? menuAt (event.getPosition()) : -1;

    if (menu != hoverMenu)
    {
        hoverMenu = menu;
        repaint (headerArea);
    }

    setMouseCursor (menu >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseExit (const juce::MouseEvent&)
{
    if (hoverMenu >= 0)
    {
        hoverMenu = -1;
        repaint (headerArea);
    }
}

int MainComponent::menuAt (juce::Point<int> position) const
{
    for (int i = 0; i < static_cast<int> (menuAreas.size()); ++i)
        if (menuAreas[static_cast<size_t> (i)].contains (position))
            return i;

    return -1;
}

void MainComponent::openMenu (int menuIndex)
{
    juce::PopupMenu menu;

    switch (menuIndex)
    {
        case 0:
            menu.addSectionHeader ("SESSION");
            menu.addItem (1001, "New Session");
            menu.addItem (1002, "Open Session...");
            menu.addItem (1003, "Save Session");
            menu.addItem (1007, "Save Session As...");
            menu.addSeparator();
            menu.addItem (1005, "Import Audio...");
            menu.addItem (1006, "Collect Media...");
            menu.addItem (1004, "Export Mixdown...");
            menu.addSeparator();
            menu.addSectionHeader ("AGENT");
            menu.addItem (1008, "Apply Mix Plan...");
            menu.addItem (1009, "Ask Mix Assistant...");
            menu.addSeparator();
            menu.addItem (1099, "Quit NCSound Music Hub");
            break;

        case 1:
        {
            menu.addSectionHeader ("EDIT");
            menu.addItem (2001, "Undo", arrange.canUndo());
            menu.addItem (2002, "Redo", false);
            menu.addSeparator();
            menu.addItem (2003, "Split at Playhead", arrange.hasSelectedClip());
            menu.addItem (2006, "Duplicate Clip", arrange.hasSelectedClip());
            menu.addItem (2007, "Delete Clip", arrange.hasSelectedClip());
            menu.addSeparator();
            menu.addSectionHeader ("MIDI");
            menu.addItem (2050, "Edit MIDI Clip (Piano Roll)", arrange.hasSelectedClip());
            menu.addItem (2051, "Import MIDI File...", true);
            menu.addSeparator();
            menu.addSectionHeader ("AUDIOSUITE");
            menu.addItem (2100, "LoFi", arrange.hasSelectedClip());
            menu.addItem (2101, "Var-Fi", arrange.hasSelectedClip());
            menu.addItem (2102, "Fast Track FX", arrange.hasSelectedClip());
            menu.addItem (2103, "Normalize", arrange.hasSelectedClip());
            menu.addItem (2104, "Reverse", arrange.hasSelectedClip());
            menu.addItem (2105, "Invert Polarity", arrange.hasSelectedClip());
            menu.addItem (2106, "Gain -3 dB", arrange.hasSelectedClip());
            menu.addItem (2107, "Strip Silence", arrange.hasSelectedClip());
            menu.addItem (2108, "Repeat Clip x2", arrange.hasSelectedClip());
            menu.addSeparator();
            menu.addSectionHeader ("MASTER INSERT");
            menu.addItem (2200, "Scan Plugins (VST3)", true);
            menu.addItem (2201, "Load Master Plugin...", engine.scannedPluginCount() > 0);
            menu.addItem (2202, "Remove Master Plugin", engine.hasMasterPlugin());
            menu.addItem (2203, "Plugin Info", engine.hasMasterPlugin());
            menu.addItem (2004, "Select All", false);
            menu.addSeparator();
            menu.addItem (2005, "Preferences...");
            menu.addSeparator();
            menu.addSectionHeader ("MIXOLOGY");

            const auto presets = mixologyPresetNames();

            for (int i = 0; i < presets.size(); ++i)
                menu.addItem (2300 + i, "Apply " + presets[i]);

            menu.addSeparator();
            menu.addSectionHeader ("MASTER RACK");
            menu.addItem (2310, "Automix", true, engine.masterMix().params.automixOn.load());
            menu.addItem (2311, "Automix Target -14 LUFS (Streaming)");
            menu.addItem (2312, "Automix Target -9 LUFS (Club)");
            menu.addItem (2313, "Automix Target -12 LUFS (Vocal)");
            menu.addItem (2314, "Automix Target -10 LUFS (Reference)");
            menu.addSeparator();
            menu.addItem (2320, "Capture A/B Slot A");
            menu.addItem (2321, "Capture A/B Slot B");
            menu.addItem (2322, "Recall A/B Slot A", engine.masterMix().hasAB (0));
            menu.addItem (2323, "Recall A/B Slot B", engine.masterMix().hasAB (1));
            menu.addItem (2324, "Compare (Native Chain Bypass)", true, engine.masterMix().params.compare.load());
            menu.addItem (2325, "Open Master Rack...");
            break;
        }

        case 2:
            menu.addSectionHeader ("VIEW");
            menu.addItem (3001, "Arrange", true, view == View::arrange);
            menu.addItem (3002, "Mix", true, view == View::mix);
            menu.addSeparator();
            menu.addItem (3003, "Plug-in Browser", true, browserOpen);
            menu.addSeparator();
            menu.addItem (3004, "Zoom In");
            menu.addItem (3005, "Zoom Out");
            break;

        case 3:
            menu.addSectionHeader ("TRACK");
            menu.addItem (4001, "Add Instrument Track", true);
            menu.addItem (4002, "Add Audio Track", true);
            menu.addItem (4003, "Add Return Track", true);
            menu.addItem (4010, "Remove Last Track", engine.getActiveTrackCount() > 1);
            menu.addSeparator();
            menu.addSectionHeader ("INSTRUMENT");
            menu.addItem (4011, "Built-in PolySynth (Sine)", true);
            menu.addItem (4012, "Built-in PolySynth (Triangle)", true);
            menu.addItem (4013, "Built-in PolySynth (Saw)", true);
            menu.addItem (4014, "Load Track Instrument...", engine.scannedInstrumentCount() > 0);
            menu.addItem (4015, "Remove Track Instrument", engine.trackInstrumentName (selectedTrack).isNotEmpty());
            menu.addItem (4016, "Open Instrument Editor", engine.trackInstrumentInstance (selectedTrack) != nullptr);
            menu.addItem (4017, "Record MIDI from Device...", ! engine.isMidiRecording());
            menu.addItem (4018, "Stop MIDI Recording", engine.isMidiRecording());
            menu.addSeparator();
            menu.addItem (4004, "Mute Selected Track", true, engine.trackMuted (selectedTrack));
            menu.addItem (4005, "Solo Selected Track", true, engine.trackSolo (selectedTrack));
            break;

        case 4:
        default:
            menu.addSectionHeader ("HELP");
            menu.addItem (5001, "About NCSound Music Hub");
            menu.addItem (5002, "Keyboard Shortcuts");
            menu.addItem (5003, "Documentation");
            break;
    }

    if (menuIndex < 0 || menuIndex >= static_cast<int> (menuAreas.size()))
        return;

    auto safe = juce::Component::SafePointer<MainComponent> (this);
    auto options = juce::PopupMenu::Options()
                       .withTargetComponent (this)
                       .withTargetScreenArea (localAreaToGlobal (menuAreas[static_cast<size_t> (menuIndex)]));

    menu.showMenuAsync (options, [safe] (int result)
    {
        if (safe != nullptr && result != 0)
            safe->handleMenuResult (result);
    });
}

void MainComponent::handleMenuResult (int result)
{
    switch (result)
    {
        case 1001:
            engine.setPlaying (false);
            engine.resetPosition();
            setStatus ("New session is not wired in this build / transport returned to bar 1", true);
            break;

        case 1002:
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Open a project",
                                                                juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                                "*.ncsproject");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe = juce::Component::SafePointer<MainComponent> (this), chooser] (const juce::FileChooser& chosen)
                                  {
                                      if (safe != nullptr && chosen.getResult() != juce::File())
                                          safe->readProject (chosen.getResult());
                                  });
            break;
        }
        case 1003:
            if (currentProjectFile == juce::File())
            {
                setStatus ("NO PROJECT FILE YET / USE SAVE SESSION AS", true);
                break;
            }

            writeProject (currentProjectFile);
            break;
        case 1007:
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Save project as",
                                                                juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                                                    .getChildFile ("Untitled.ncsproject"),
                                                                "*.ncsproject");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safe = juce::Component::SafePointer<MainComponent> (this), chooser] (const juce::FileChooser& chosen)
                                  {
                                      if (safe == nullptr || chosen.getResult() == juce::File())
                                          return;

                                      auto file = chosen.getResult();

                                      if (file.getFileExtension().isEmpty())
                                          file = file.withFileExtension ("ncsproject");

                                      safe->writeProject (file);
                                  });
            break;
        }
        case 1005: importAudioFile(); break;
        case 1004:
        {
            // The session's own range, plus a bar so a master reverb tail rings out.
            const auto lastBar = juce::jmax (1.0, engine.contentEndBar()) + 1.0;

            auto chooser = std::make_shared<juce::FileChooser> (
                "Export mixdown",
                juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                    .getChildFile ("mixdown.wav"),
                "*.wav");

            chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safe = juce::Component::SafePointer<MainComponent> (this), chooser, lastBar] (const juce::FileChooser& fc)
                                  {
                                      if (safe == nullptr)
                                          return;

                                      auto file = fc.getResult();

                                      if (file == juce::File())
                                          return;

                                      if (! file.hasFileExtension ("wav"))
                                          file = file.withFileExtension ("wav");

                                      juce::String error;
                                      const auto rendered = safe->engine.renderToWav (file, 1.0, lastBar, 24, error);

                                      safe->setStatus (rendered
                                                           ? "Mixdown written: " + file.getFileName()
                                                           : "Export failed: " + error,
                                                       ! rendered,
                                                       4.0);
                                  });
            break;
        }
        case 1006:
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Choose the project folder",
                                                                juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                                "*");

            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectDirectories,
                                  [safe = juce::Component::SafePointer<MainComponent> (this), chooser] (const juce::FileChooser& fc)
                                  {
                                      if (safe == nullptr)
                                          return;

                                      const auto folder = fc.getResult();

                                      if (folder == juce::File())
                                          return;

                                      juce::String report;
                                      const auto copied = safe->arrange.collectMediaTo (folder, report);
                                      safe->setStatus (copied < 0 ? "COLLECT FAILED / " + report : report, copied < 0);
                                  });
            break;
        }
        case 1008:
        {
            // The agent writes a plan file; the session validates it before a
            // single fader moves. Nothing here trusts the document.
            auto chooser = std::make_shared<juce::FileChooser> ("Apply a mix plan",
                                                                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                                "*.json;*.txt");

            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe = juce::Component::SafePointer<MainComponent> (this), chooser] (const juce::FileChooser& fc)
                                  {
                                      if (safe == nullptr || fc.getResult() == juce::File())
                                          return;

                                      const auto text = fc.getResult().loadFileAsString();
                                      const auto validation = parseMixPlanJson (text, safe->engine.getActiveTrackCount());

                                      if (! validation.usable())
                                      {
                                          safe->setStatus ("MIX PLAN REJECTED / " + validation.summary(), true, 8.0);
                                          return;
                                      }

                                      const auto report = executeMixPlan (safe->engine, validation.plan);

                                      juce::String status = "MIX PLAN / " + validation.summary()
                                                            + " / " + juce::String (report.applied) + " applied";

                                      if (report.cleared > 0)
                                          status << " / " << report.cleared << " cleared";

                                      if (! validation.rejected.empty())
                                          status << " / " << validation.rejected.front().where << ": "
                                                 << validation.rejected.front().reason;

                                      safe->setStatus (status, ! report.anyChange(), 8.0);
                                  });
            break;
        }
        case 1009:
        {
            auto dialog = std::make_shared<juce::AlertWindow> ("Ask Mix Assistant",
                                                              "Describe the mix you want and the local "
                                                              "Qwen server will propose a plan; every "
                                                              "lane is validated before anything moves.",
                                                              juce::AlertWindow::QuestionIcon, this);

            dialog->addTextEditor ("brief", "vocal-forward chorus lift", "Goal", false);
            dialog->addTextEditor ("server", "http://127.0.0.1:8092", "Model server", false);
            dialog->addButton ("Ask", 1, juce::KeyPress (juce::KeyPress::returnKey, 0, 0));
            dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey, 0, 0));

            dialog->enterModalState (true,
                                     juce::ModalCallbackFunction::create (
                                         [dialog, safe = juce::Component::SafePointer<MainComponent> (this)] (int modalResult) mutable
                                         {
                                             if (modalResult != 1 || safe == nullptr)
                                                 return;

                                             const auto brief = dialog->getTextEditorContents ("brief");
                                             const auto server = dialog->getTextEditorContents ("server");

                                             // Snapshot the session on the message thread;
                                             // the worker only speaks HTTP and parses JSON.
                                             const auto activeTracks = safe->engine.getActiveTrackCount();
                                             const auto prompt = buildMixPlanPrompt (brief, activeTracks);

                                             safe->setStatus ("MIX ASSISTANT / contacting model...", false, 300.0);

                                             std::thread ([safe, server, prompt, activeTracks]() mutable
                                             {
                                                 if (safe == nullptr)
                                                     return;

                                                 const auto fetch = requestMixPlanFromQwen (server, prompt, activeTracks);

                                                 juce::MessageManager::callAsync ([safe, fetch] () mutable
                                                 {
                                                     if (safe == nullptr)
                                                         return;

                                                     if (fetch.error.isNotEmpty())
                                                     {
                                                         safe->setStatus ("MIX ASSISTANT ERROR / " + fetch.error, true, 10.0);
                                                         return;
                                                     }

                                                     if (! fetch.validation.usable())
                                                     {
                                                         safe->setStatus ("MIX PLAN REJECTED / " + fetch.validation.summary(), true, 10.0);
                                                         return;
                                                     }

                                                     const auto report = executeMixPlan (safe->engine, fetch.validation.plan);

                                                     juce::String status = "MIX ASSISTANT / " + fetch.validation.summary()
                                                                          + " / " + juce::String (report.applied) + " applied";

                                                     if (report.cleared > 0)
                                                         status << " / " << report.cleared << " cleared";

                                                     if (! fetch.validation.rejected.empty())
                                                         status << " / " << fetch.validation.rejected.front().where << ": "
                                                                << fetch.validation.rejected.front().reason;

                                                     safe->setStatus (status, ! report.anyChange(), 8.0);
                                                 });
                                             }).detach();
                                         }));
            break;
        }
        case 1099:
            if (auto* app = juce::JUCEApplication::getInstance())
                app->systemRequestedQuit();
            break;

        case 2200:
        {
            const auto found = engine.scanPlugins();
            setStatus ("SCAN / " + juce::String (found) + " PLUGINS FOUND", found == 0);
            break;
        }

        case 2201:
        {
            if (engine.scannedPluginCount() == 0)
            {
                setStatus ("SCAN PLUGINS FIRST", true);
                break;
            }

            juce::PopupMenu pluginMenu;
            pluginMenu.addSectionHeader ("MASTER INSERT");

            for (int i = 0; i < engine.scannedPluginCount(); ++i)
                pluginMenu.addItem (i + 1, engine.scannedPluginName (i));

            pluginMenu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                      [safe = juce::Component::SafePointer<MainComponent> (this)] (int chosen)
                                      {
                                          if (safe == nullptr || chosen <= 0)
                                              return;

                                          if (safe->engine.loadMasterPlugin (chosen - 1))
                                              safe->setStatus ("MASTER INSERT / " + safe->engine.masterPluginName()
                                                               + " / " + juce::String (safe->engine.masterPluginLatency())
                                                               + " SAMPLES LATENCY");
                                          else
                                              safe->setStatus ("MASTER INSERT / PLUGIN REFUSED TO LOAD", true);
                                      });
            break;
        }

        case 2202:
            engine.removeMasterPlugin();
            setStatus ("MASTER INSERT CLEARED");
            break;

        case 2203:
            setStatus (engine.hasMasterPlugin()
                           ? "MASTER INSERT / " + engine.masterPluginName() + " / "
                                 + juce::String (engine.masterPluginLatency()) + " SAMPLES LATENCY"
                           : "MASTER INSERT / EMPTY");
            break;

        case 2204:
            if (auto* instance = engine.masterInsertInstance())
                new PluginEditorWindow (engine.masterPluginName(), instance);
            else
                setStatus ("NO MASTER INSERT LOADED", true);
            break;

        case 2100:
        {
            nc::engine::audiosuite::Process process;
            process.kind = nc::engine::audiosuite::Kind::lofi;
            arrange.applyAudioSuite (process);
            break;
        }

        case 2101:
        {
            nc::engine::audiosuite::Process process;
            process.kind = nc::engine::audiosuite::Kind::varifi;
            arrange.applyAudioSuite (process);
            break;
        }

        case 2102:
        {
            nc::engine::audiosuite::Process process;
            process.kind = nc::engine::audiosuite::Kind::fastTrack;
            arrange.applyAudioSuite (process);
            break;
        }

        case 2103:
        {
            nc::engine::audiosuite::Process process;
            process.kind = nc::engine::audiosuite::Kind::normalize;
            arrange.applyAudioSuite (process);
            break;
        }

        case 2104:
        {
            nc::engine::audiosuite::Process process;
            process.kind = nc::engine::audiosuite::Kind::reverse;
            arrange.applyAudioSuite (process);
            break;
        }

        case 2105:
        {
            nc::engine::audiosuite::Process process;
            process.kind = nc::engine::audiosuite::Kind::invert;
            arrange.applyAudioSuite (process);
            break;
        }

        case 2106:
        {
            nc::engine::audiosuite::Process process;
            process.kind = nc::engine::audiosuite::Kind::gain;
            process.gainDb = -3.0f;
            arrange.applyAudioSuite (process);
            break;
        }

        case 2107:
            arrange.stripSilenceSelected();
            break;

        case 2108:
            arrange.repeatSelected (2);
            break;

        case 2001:
            setStatus (arrange.undo() ? "Undo / last clip edit reverted" : "Nothing to undo", ! arrange.canUndo(), 3.0);
            break;

        case 2002: setStatus ("Redo is not wired in this build / undo is one step", true); break;
        case 2003: arrange.splitSelectedAtPlayhead(); break;
        case 2006: arrange.duplicateSelected(); break;
        case 2007: arrange.deleteSelected(); break;
        case 2050: arrange.openPianoRollForSelected(); break;
        case 2051:
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Import a MIDI file", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.mid;*.midi");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe = juce::Component::SafePointer<MainComponent> (this), chooser] (const juce::FileChooser& chosen)
                                  {
                                      if (safe == nullptr || chosen.getResult() == juce::File())
                                          return;
                                      const auto index = safe->engine.importMidiFile (chosen.getResult(), safe->selectedTrack, 1.0);
                                      safe->arrange.refresh();
                                      safe->setStatus (index >= 0 ? "MIDI IMPORTED " + chosen.getResult().getFileName() : "MIDI IMPORT FAILED", index < 0);
                                  });
            break;
        }
        case 2004: setStatus ("All clips already visible in this preview session"); break;
        case 2005: setStatus ("Preferences are not wired in this build / audio follows the system device"); break;

        case 2300:
        case 2301:
        case 2302:
        case 2303:
        case 2304:
        {
            const auto report = applyMixologyPreset (engine, result - 2300);
            setStatus (report.text, ! report.ok(), 6.0);
            break;
        }

        case 2310:
        {
            const auto on = ! engine.masterMix().params.automixOn.load();
            engine.masterMix().setParam ("automixOn", on ? 1.0f : 0.0f);
            setStatus (on ? "MASTER AUTOMIX ON / target " + juce::String (engine.masterMix().params.targetLufs.load(), 1) + " LUFS"
                          : "MASTER AUTOMIX OFF");
            break;
        }

        case 2311:
        case 2312:
        case 2313:
        case 2314:
            engine.masterMix().setParam ("automixProfile", static_cast<float> (result - 2311));
            engine.masterMix().setParam ("automixOn", 1.0f);
            setStatus ("MASTER AUTOMIX / " + juce::String (engine.masterMix().params.targetLufs.load(), 1) + " LUFS TARGET");
            break;

        case 2320:
            engine.masterMix().captureAB (0);
            setStatus ("MASTER A/B / SLOT A CAPTURED");
            break;

        case 2321:
            engine.masterMix().captureAB (1);
            setStatus ("MASTER A/B / SLOT B CAPTURED");
            break;

        case 2322:
            engine.masterMix().recallAB (0);
            setStatus ("MASTER A/B / RECALLED A");
            break;

        case 2323:
            engine.masterMix().recallAB (1);
            setStatus ("MASTER A/B / RECALLED B");
            break;

        case 2324:
        {
            const auto compare = ! engine.masterMix().params.compare.load();
            engine.masterMix().setParam ("compare", compare ? 1.0f : 0.0f);
            setStatus (compare ? "MASTER COMPARE ON / native chain bypassed" : "MASTER COMPARE OFF");
            break;
        }

        case 2325:
            openMixRackEditor (-1);
            break;

        case 3001: setView (View::arrange); break;
        case 3002: setView (View::mix); break;
        case 3003: setBrowserOpen (! browserOpen); break;
        case 3004: arrange.zoomIn(); break;
        case 3005: arrange.zoomOut(); break;

        case 4001:
        case 4002:
        case 4003: addTrack(); break;

        case 4010: removeTrack(); break;

        case 4004:
            engine.setTrackMuted (selectedTrack, ! engine.trackMuted (selectedTrack));
            setStatus ("Mute " + juce::String (engine.trackMuted (selectedTrack) ? "on" : "off")
                           + " / " + trackName (selectedTrack));
            break;

        case 4011:
        case 4012:
        case 4013:
        {
            const auto wave = result - 4011;
            juce::String error;
            engine.loadBuiltinInstrumentForTrack (selectedTrack, wave, error);
            arrange.refresh();
            setStatus ("INSTRUMENT / PolySynth on " + trackName (selectedTrack), false, 3.0);
            break;
        }

        case 4014:
        {
            if (engine.scannedInstrumentCount() == 0)
            {
                setStatus ("SCAN PLUGINS FIRST", true);
                break;
            }
            juce::PopupMenu pluginMenu;
            pluginMenu.addSectionHeader ("TRACK INSTRUMENT");
            for (int i = 0; i < engine.scannedInstrumentCount(); ++i)
                pluginMenu.addItem (i + 1, engine.scannedInstrumentName (i));
            pluginMenu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                      [safe = juce::Component::SafePointer<MainComponent> (this)] (int chosen)
                                      {
                                          if (safe == nullptr || chosen <= 0)
                                              return;
                                          juce::String error;
                                          if (! safe->engine.loadTrackInstrumentByIndex (safe->selectedTrack, chosen - 1, error))
                                              safe->setStatus ("INSTRUMENT / " + error, true);
                                      });
            break;
        }

        case 4015:
            engine.unloadTrackInstrument (selectedTrack);
            arrange.refresh();
            setStatus ("INSTRUMENT REMOVED / " + trackName (selectedTrack));
            break;

        case 4016:
        {
            if (auto* instance = engine.trackInstrumentInstance (selectedTrack))
                new PluginEditorWindow (engine.trackInstrumentName (selectedTrack), instance);
            else
                setStatus ("NO HOSTED INSTRUMENT ON " + trackName (selectedTrack), true);
            break;
        }

        case 4017:
        {
            const auto devices = AudioEngine::availableMidiDevices();
            if (devices.isEmpty())
            {
                setStatus ("NO MIDI DEVICES FOUND", true);
                break;
            }
            juce::PopupMenu deviceMenu;
            for (int i = 0; i < devices.size(); ++i)
                deviceMenu.addItem (i + 1, devices[i].name);
            deviceMenu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                      [safe = juce::Component::SafePointer<MainComponent> (this), devices] (int chosen)
                                      {
                                          if (safe == nullptr || chosen <= 0)
                                              return;
                                          juce::String error;
                                          if (! safe->engine.openMidiDevice (devices[chosen - 1].identifier, error))
                                          {
                                              safe->setStatus ("MIDI / " + error, true);
                                              return;
                                          }
                                          if (! safe->engine.startMidiRecording (safe->selectedTrack, error))
                                          {
                                              safe->setStatus ("MIDI RECORD / " + error, true);
                                              return;
                                          }
                                          safe->engine.setPlaying (true);
                                          safe->setStatus ("MIDI RECORDING / PLAY TO CAPTURE / STOP TO PLACE");
                                      });
            break;
        }

        case 4018:
        {
            if (! engine.isMidiRecording())
                break;
            const auto clip = engine.stopMidiRecording();
            auto clips = engine.midiClips();
            AudioEngine::MidiTimelineClip midi;
            midi.track = clip.track;
            midi.startBar = clip.startBar;
            midi.lengthBars = clip.lengthBars;
            midi.name = juce::String (clip.name);
            midi.notes = clip.notes;
            midi.ccs = clip.ccs;
            midi.bends = clip.bends;
            clips.push_back (std::move (midi));
            engine.setMidiClips (std::move (clips));
            arrange.refresh();
            setStatus ("MIDI TAKE PLACED / " + juce::String (static_cast<int> (clip.notes.size())) + " NOTES");
            break;
        }

        case 4005:
            engine.setTrackSolo (selectedTrack, ! engine.trackSolo (selectedTrack));
            setStatus ("Solo " + juce::String (engine.trackSolo (selectedTrack) ? "on" : "off")
                           + " / " + trackName (selectedTrack));
            break;

        case 5001:
            setStatus ("NCSound Music Hub 0.1.0 / a division of Overlay 365 / JUCE 8 native host");
            break;

        case 5002:
            setStatus ("V select / T trim / F fade / G gain / X split / E erase / Ctrl+E split at playhead / Ctrl+D duplicate / Del delete");
            break;

        case 5003:
            setStatus ("Design spec: docs/superpowers/specs/2026-11-05-ncsound-daw-design.md");
            break;

        default:
            break;
    }
}

void MainComponent::openMasterInsertMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("MASTER INSERT");
    menu.addItem (2200, "Scan Plugins (VST3)");
    menu.addItem (2201, "Load Master Plugin...", engine.scannedPluginCount() > 0);
    menu.addItem (2202, "Remove Master Plugin", engine.hasMasterPlugin());
    menu.addItem (2203, "Plugin Info", engine.hasMasterPlugin());
    menu.addItem (2204, "Open Master Insert Editor", engine.masterInsertInstance() != nullptr);

    auto safe = juce::Component::SafePointer<MainComponent> (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result != 0)
                                safe->handleMenuResult (result);
                        });
}

void MainComponent::openMixRackEditor (int track)
{
    auto editor = std::make_unique<MixRackEditor> (engine, track);
    juce::CallOutBox::launchAsynchronously (std::move (editor),
                                            getLocalBounds().withSizeKeepingCentre (560, 360),
                                            this);
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::spaceKey)
    {
        engine.setPlaying (! engine.isPlaying());
        setStatus (engine.isPlaying() ? "Playback running" : "Playback stopped", false, 2.0);
        return true;
    }

    if (code == juce::KeyPress::escapeKey)
    {
        if (browserOpen)
        {
            setBrowserOpen (false);
            return true;
        }

        return false;
    }

    if (code == juce::KeyPress::homeKey)
    {
        engine.resetPosition();
        setStatus ("Returned to bar 1", false, 2.0);
        return true;
    }

    if (key.getModifiers().isCommandDown())
    {
        if (key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z')
        {
            const auto undone = arrange.undo();
            setStatus (undone ? "Undo / last clip edit reverted" : "Nothing to undo", ! undone, 3.0);
            return true;
        }

        if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')
        {
            setStatus ("Saving is not wired in this build / nothing was written to disk", true);
            return true;
        }

        if (key.getTextCharacter() == 'b' || key.getTextCharacter() == 'B')
        {
            setBrowserOpen (! browserOpen);
            return true;
        }

        const auto commandCharacter = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

        if (commandCharacter == 'e')
        {
            arrange.splitSelectedAtPlayhead();
            return true;
        }

        if (commandCharacter == 'd')
        {
            arrange.duplicateSelected();
            return true;
        }

        return false;
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        if (view == View::arrange)
            arrange.deleteSelected();

        return true;
    }

    const auto character = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    switch (character)
    {
        case '1': setView (View::arrange); return true;
        case '2': setView (View::mix); return true;
        case 'b': setBrowserOpen (! browserOpen); return true;

        case 'v': arrange.setTool (0); setStatus ("Select tool / drag clips, trim edges, pull fades"); return true;
        case 't': arrange.setTool (1); setStatus ("Trim tool / drag either clip edge"); return true;
        case 'f': arrange.setTool (2); setStatus ("Fade tool / drag the top corners of a clip"); return true;
        case 'g': arrange.setTool (3); setStatus ("Clip gain tool / drag up or down"); return true;
        case 'x': arrange.setTool (4); setStatus ("Split tool / click a clip to cut at that bar"); return true;
        case 'e': arrange.setTool (5); setStatus ("Erase tool / click a clip"); return true;

        case '[': arrange.zoomOut(); return true;
        case ']': arrange.zoomIn(); return true;
        case '-': arrange.zoomOut(); return true;
        case '+':
        case '=': arrange.zoomIn(); return true;

        case 'm':
            engine.setTrackMuted (selectedTrack, ! engine.trackMuted (selectedTrack));
            setStatus ("Mute " + juce::String (engine.trackMuted (selectedTrack) ? "on" : "off")
                           + " / " + trackName (selectedTrack), false, 2.0);
            return true;

        case 's':
            engine.setTrackSolo (selectedTrack, ! engine.trackSolo (selectedTrack));
            setStatus ("Solo " + juce::String (engine.trackSolo (selectedTrack) ? "on" : "off")
                           + " / " + trackName (selectedTrack), false, 2.0);
            return true;

        default:
            break;
    }

    return false;
}
} // namespace nc

namespace nc
{
void MainComponent::writeProject (const juce::File& file)
{
    // Collect external media into the project folder before serialising, so
    // the project never stores absolute paths (CWE-22; see
    // ArrangeView::writeClipsToDocument). If collecting fails, do not write a
    // project that would silently lose those clips.
    {
        juce::String collectReport;
        const auto copied = arrange.collectMediaTo (file.getParentDirectory(), collectReport);

        if (copied < 0)
        {
            setStatus ("SAVE FAILED / " + collectReport, true);
            return;
        }
    }

    nc::engine::ProjectDocument document;
    document.bpm = engine.getBpm();
    document.tempoMap = engine.tempoMap();
    document.markers = engine.markers();
    document.activeTrackCount = engine.getActiveTrackCount();

    for (auto track = 0; track < document.activeTrackCount; ++track)
    {
        nc::engine::TrackState state;
        state.name = trackName (track).toStdString();
        state.gainDb = juce::Decibels::gainToDecibels (static_cast<double> (engine.trackGain (track)), -60.0);
        state.pan = engine.trackPan (track);
        state.muted = engine.trackMuted (track);
        state.soloed = engine.trackSolo (track);
        state.sendA = engine.trackSendA (track);
        state.sendB = engine.trackSendB (track);
        state.width = engine.trackWidth (track);

        for (int slot = 0; slot < 4; ++slot)
            state.inserts[static_cast<size_t> (slot)] = engine.trackInsertState (track, slot);

        state.instrument = engine.trackInstrumentState (track);
        document.tracks.push_back (std::move (state));
    }

    arrange.writeClipsToDocument (document, file);

    for (const auto& clip : engine.midiClips())
    {
        nc::engine::ClipState state;
        state.isMidi = true;
        state.name = clip.name.toStdString();
        state.track = clip.track;
        state.startBar = clip.startBar;
        state.lengthBars = clip.lengthBars;
        state.notes = clip.notes;
        state.ccs = clip.ccs;
        state.bends = clip.bends;
        document.clips.push_back (std::move (state));
    }

    if (engine.hasMasterPlugin() || engine.masterInsertIsMissing())
        document.masterInsert = engine.masterInsertState();

    document.mixRack = engine.mixRackStateJson().toStdString();

    juce::String error;

    if (! saveProjectDocument (file, document, error))
    {
        setStatus ("SAVE FAILED / " + error, true);
        return;
    }

    currentProjectFile = file;
    setStatus ("SAVED " + file.getFileName() + " / "
               + juce::String (static_cast<int> (document.clips.size())) + " CLIPS");
}

void MainComponent::readProject (const juce::File& file)
{
    juce::String error;
    const auto document = loadProjectDocument (file, error);

    if (! document.has_value())
    {
        setStatus ("OPEN FAILED / " + error, true);
        return;
    }

    engine.setTempoMap (document->tempoMap);
    engine.setMarkers (document->markers);
    engine.setActiveTrackCount (document->activeTrackCount);

    {
        std::vector<AudioEngine::MidiTimelineClip> midiClips;
        for (const auto& clip : document->clips)
        {
            if (! clip.isMidi)
                continue;
            AudioEngine::MidiTimelineClip midi;
            midi.track = clip.track;
            midi.startBar = clip.startBar;
            midi.lengthBars = clip.lengthBars;
            midi.name = juce::String (clip.name);
            midi.notes = clip.notes;
            midi.ccs = clip.ccs;
            midi.bends = clip.bends;
            midiClips.push_back (std::move (midi));
        }
        engine.setMidiClips (std::move (midiClips));
    }

    // Restore runs off descriptionXml (saved per slot), so no catalog scan
    // is required on open. The legacy scanSync() blocked the message thread
    // here; scanning is now only triggered from the browser's RESCAN button
    // via scanAsync().

    auto missingInserts = 0;
    juce::String missingInsertNames;

    for (auto track = 0; track < document->activeTrackCount; ++track)
    {
        const nc::engine::TrackState emptyTrack;
        const auto& trackState = static_cast<size_t> (track) < document->tracks.size()
                                     ? document->tracks[static_cast<size_t> (track)] : emptyTrack;

        for (int slot = 0; slot < 4; ++slot)
        {
            const auto& insert = trackState.inserts[static_cast<size_t> (slot)];

            if (insert.isEmpty())
                continue;

            juce::String insertError;

            if (! engine.restoreTrackInsert (track, slot, insert, insertError))
            {
                ++missingInserts;

                if (missingInsertNames.length() < 120)
                    missingInsertNames += (missingInsertNames.isEmpty() ? "" : ", ")
                                          + juce::String (insert.identifier);
            }
        }

        if (! trackState.instrument.isEmpty())
        {
            juce::String instrumentError;
            if (! engine.restoreTrackInstrument (track, trackState.instrument, instrumentError))
            {
                ++missingInserts;
                if (missingInsertNames.length() < 120)
                    missingInsertNames += (missingInsertNames.isEmpty() ? "" : ", ")
                                          + juce::String (trackState.instrument.identifier);
            }
        }
    }

    for (auto track = 0; track < document->activeTrackCount; ++track)
    {
        const nc::engine::TrackState empty;
        const auto& state = static_cast<size_t> (track) < document->tracks.size()
                                ? document->tracks[static_cast<size_t> (track)] : empty;
        engine.setTrackGain (track, static_cast<float> (juce::Decibels::decibelsToGain (state.gainDb, -60.0)));
        engine.setTrackPan (track, static_cast<float> (state.pan));
        engine.setTrackMuted (track, state.muted);
        engine.setTrackSolo (track, state.soloed);
        engine.setTrackSendA (track, static_cast<float> (state.sendA));
        engine.setTrackSendB (track, static_cast<float> (state.sendB));
        engine.setTrackWidth (track, static_cast<float> (state.width));
    }

    auto insertRestored = false;

    if (! document->mixRack.empty())
        engine.restoreMixRackStateJson (juce::String (document->mixRack));

    if (! document->masterInsert.isEmpty())
    {
        juce::String masterError;
        insertRestored = engine.restoreMasterInsert (document->masterInsert, masterError);
    }

    auto missing = 0;
    const auto restored = arrange.readClipsFromDocument (*document, file, missing);
    currentProjectFile = file;
    setView (View::arrange);
    auto note = juce::String();

    if (! document->masterInsert.isEmpty())
        note = insertRestored ? " / INSERT RESTORED" : " / INSERT " + juce::String (document->masterInsert.identifier) + " NOT FOUND";

    if (missingInserts > 0)
        note += " / " + juce::String (missingInserts) + " TRACK INSERTS MISSING"
                + (missingInsertNames.isNotEmpty() ? " [" + missingInsertNames + "]" : "");

    setStatus ("OPENED " + file.getFileName() + " / " + juce::String (restored) + " CLIPS"
                   + (missing > 0 ? " / " + juce::String (missing) + " MEDIA MISSING" : "") + note,
               missing > 0 || missingInserts > 0 || (! document->masterInsert.isEmpty() && ! insertRestored));
}

} // namespace nc
