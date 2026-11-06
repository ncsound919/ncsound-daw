#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "ChannelStrip.h"
#include "PluginRegistry.h"
#include "Theme.h"

namespace nc
{
class MainComponent final : public juce::Component,
                            private juce::ListBoxModel,
                            private juce::Timer
{
public:
    explicit MainComponent (juce::File registryFile);
    ~MainComponent() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    enum class View { edit, mix };
    enum class Tool { zoom, trim, select, grab, pencil, erase };
    enum class Mode { shuffle, slip, spot, grid };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void timerCallback() override;

    void setView (View newView);
    void setPanelOpen (bool shouldOpen);
    juce::String positionText() const;
    juce::String clockText() const;
    int trackAt (juce::Point<int> position) const;

    void paintMenu (juce::Graphics&);
    void paintTransport (juce::Graphics&);
    void paintTools (juce::Graphics&);
    void paintEdit (juce::Graphics&);
    void paintMix (juce::Graphics&);
    void paintPanel (juce::Graphics&);
    void paintStatus (juce::Graphics&);

    Registry registry;
    AudioEngine engine;

    juce::ListBox browser { "plugins", this };
    std::vector<std::unique_ptr<ChannelStrip>> strips;

    juce::TextButton rtz { "|<" };
    juce::TextButton rew { "<<" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton recordButton { "REC" };
    juce::TextButton metronomeButton { "CLICK" };
    juce::TextButton pluginsButton { "PLUG-INS" };
    juce::TextButton editButton { "EDIT" };
    juce::TextButton mixButton { "MIX" };
    juce::Slider bpm;
    juce::Slider masterFader;

    View view = View::edit;
    Tool tool = Tool::select;
    Mode mode = Mode::grid;
    bool panelOpen = false;
    bool armed[AudioEngine::trackCount] {};
    int selectedTrack = 0;
    int selectedRow = -1;

    juce::Rectangle<int> menuArea, transportArea, toolArea, headerArea, laneArea,
                         panelArea, mixArea, statusArea, modeArea, masterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace nc
