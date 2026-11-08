#pragma once

#include "PluginEditorRegistry.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace nc
{
/** A DocumentWindow hosting one hosted plugin's editor (instrument or
    effect). The editor is created on the message thread from the live
    AudioPluginInstance; the window is non-modal and closes when the
    plugin is unloaded/replaced (the owner checks instance identity). */
class PluginEditorWindow final : public juce::DocumentWindow,
                                 private pluginEditors::Watcher
{
public:
    PluginEditorWindow (const juce::String& title, juce::AudioProcessor* processor);
    ~PluginEditorWindow() override;

    juce::AudioProcessor* processor() const noexcept { return processor_; }

private:
    void closeButtonPressed() override;

    juce::AudioProcessor* watchedProcessor() const noexcept override { return processor_; }
    void closeForProcessorGone() override;

    juce::AudioProcessor* processor_ = nullptr;
    std::unique_ptr<juce::AudioProcessorEditor> editor_;
    juce::AudioProcessorEditor* hostedEditor_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditorWindow)
};
} // namespace nc
