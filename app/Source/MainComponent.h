#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "ChannelStrip.h"
#include "PluginRegistry.h"

namespace nc
{
namespace brand
{
inline const juce::Colour ink     { 0xff181818 };
inline const juce::Colour well    { 0xff0b0b0b };
inline const juce::Colour surface { 0xff222222 };
inline const juce::Colour paper   { 0xfff8e8c8 };
inline const juce::Colour accent  { 0xffc25a2a };
} // namespace brand

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

private:
    enum class View { arrange, mixer };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void timerCallback() override;

    void setView (View newView);
    juce::String positionText() const;

    void paintHeader (juce::Graphics&);
    void paintBrowser (juce::Graphics&);
    void paintArrangement (juce::Graphics&);
    void paintInspector (juce::Graphics&);
    void paintTransport (juce::Graphics&);

    Registry registry;
    AudioEngine engine;

    juce::ListBox browser { "plugins", this };
    std::vector<std::unique_ptr<ChannelStrip>> strips;

    juce::TextButton arrangeButton { "ARRANGE" };
    juce::TextButton mixerButton { "MIXER" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton metronomeButton { "CLICK" };
    juce::Slider bpm;

    View view = View::arrange;
    int selectedRow = -1;

    juce::Rectangle<int> headerArea, browserArea, mainArea, inspectorArea, transportArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace nc
