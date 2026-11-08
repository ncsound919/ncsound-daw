#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace nc
{
class EightOHeightEditor final : public juce::AudioProcessorEditor
{
public:
    explicit EightOHeightEditor (EightOHeightProcessor&);
    ~EightOHeightEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Rotary
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        int column = 0;
    };

    Rotary& addRotary (const char* parameterId, const juce::String& name, int column);

    EightOHeightProcessor& processor;

    std::vector<std::unique_ptr<Rotary>> rotaries;
    juce::ComboBox waveBox, glideModeBox, circuitBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveAttachment,
        glideModeAttachment, circuitAttachment;

    juce::ToggleButton characterToggle { "CHARACTER" }, monoLockToggle { "MONO LOCK" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> characterAttachment,
        monoLockAttachment;

    juce::Label title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EightOHeightEditor)
};
} // namespace nc
