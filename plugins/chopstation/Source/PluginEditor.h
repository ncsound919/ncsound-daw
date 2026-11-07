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
    juce::ComboBox presetBox;

    // One slider + attachment per APVTS param (10 total).
    struct ParamControl
    {
        juce::Slider slider;
        juce::Label nameLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        ParamControl (const juce::String& name) { nameLabel.setText (name, juce::dontSendNotification); }
    };
    std::array<std::unique_ptr<ParamControl>, 10> controls;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopStationEditor)
};
