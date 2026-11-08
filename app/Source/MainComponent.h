#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <nc/engine/Autosave.hpp>

#include <array>
#include <optional>
#include <memory>

#include "ArrangeView.h"
#include "AudioEngine.h"
#include "AutomationLaneView.h"
#include "BrowserOverlay.h"
#include "MixerPanel.h"
#include "PluginEditorWindow.h"
#include "PluginRegistry.h"
#include "Theme.h"
#include "TransportBar.h"

namespace nc
{
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    explicit MainComponent (juce::File registryFile);
    ~MainComponent() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Dirty-close guard for the main window; public for Main.cpp. */
    bool confirmClose();

private:
    enum class View { arrange, mix };

    void timerCallback() override;

    void setView (View newView);
    void setSelectedTrack (int track);
    void addTrack();
    void removeTrack();
    void setBrowserOpen (bool shouldOpen, int targetTrack = -1, int targetSlot = 0);
    void openMixRackEditor (int track);
    void openMasterInsertMenu();
    void openMenu (int menuIndex);
    void handleMenuResult (int result);
    void importAudioFile();
    void startRecording();
    void finishRecording();
    double recordStartBar = 1.0;
    void writeProject (const juce::File& file);
    void readProject (const juce::File& file);
    void requestNewSession();
    void discardToEmptySession();
    bool hasUnsavedSessionWork() const;

    juce::File cleanExitMarker();
    void writeCleanExitMarker();
    void clearCleanExitMarker();
    void checkCrashRecovery();
    void openSettings();
    void markDirty();
    void maybeAutosave();
    bool saveAutosaveSibling();
    void setStatus (juce::String message, bool alert = false, double seconds = 5.0);
    void setPitchViewOpen (bool shouldOpen);
    juce::WebBrowserComponent::Options buildWebOptions();
    std::optional<juce::WebBrowserComponent::Resource> getWebResource (const juce::String& path);
    juce::var buildState() const;
    juce::var buildTransport() const;
    juce::var buildPitch() const;
    void pushState();
    void paintHeader (juce::Graphics&);
    void paintStatus (juce::Graphics&);
    int menuAt (juce::Point<int>) const;
    juce::String trackName (int index) const;
    juce::String defaultStatusText() const;

    Registry registry;
    AudioEngine engine;
    TransportBar transport;
    ArrangeView arrange;
    MixerPanel mixer;
    BrowserOverlay browser;
    std::unique_ptr<juce::WebBrowserComponent> webView;
    bool pitchOpen = false;

    HardwareButton newButton { HardwareButton::Icon::plus, "NEW" };
    HardwareButton settingsButton { HardwareButton::Icon::settings };

    View view = View::arrange;
    bool browserOpen = false;
    int browserSlot = 0;
    int selectedTrack = 0;
    int hoverMenu = -1;
    int tick = 0;
    bool deviceBannerShown = false;

    juce::TooltipWindow tooltipWindow { this, 700 };

    juce::File currentProjectFile;
    juce::String statusMessage;
    bool statusAlert = false;
    double statusUntil = 0.0;

    // Autosave journal. `lastEditMillis` is wall-clock now when the undo
    // depth advances (dirty proxy); the `.autosave` sibling is written by
    // maybeAutosave() on the 30 Hz timer after the debounce + interval.
    int64_t lastSaveMillis = 0;
    int64_t lastEditMillis = 0;
    int64_t lastAutosaveMillis = 0;
    std::size_t lastSeenUndoDepth = 0;

    // Slot written by the last manual-save rotation; -1 means none yet.
    int lastBackupIndex = -1;

    std::array<juce::Rectangle<int>, 5> menuAreas;
    juce::Rectangle<int> headerArea, logoArea, headerRightArea, cpuArea,
                         transportArea, bodyArea, statusArea, statusMessageArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace nc
