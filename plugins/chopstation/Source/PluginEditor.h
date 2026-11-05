#pragma once

#include "PluginProcessor.h"

class ChopStationEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit ChopStationEditor (nc::ChopStationProcessor&);
    ~ChopStationEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshLabels();
    void chooseFile();

    nc::ChopStationProcessor& processor;
    juce::TextButton loadButton { "Load sample" };
    juce::Label titleLabel;
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopStationEditor)
};
