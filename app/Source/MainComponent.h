#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "ArrangeView.h"
#include "AudioEngine.h"
#include "BrowserOverlay.h"
#include "MixerPanel.h"
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
    void writeProject (const juce::File& file);
    void readProject (const juce::File& file);
    void setStatus (juce::String message, bool alert = false, double seconds = 5.0);
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

    std::array<juce::Rectangle<int>, 5> menuAreas;
    juce::Rectangle<int> headerArea, logoArea, headerRightArea, cpuArea,
                         transportArea, bodyArea, statusArea, statusMessageArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace nc
