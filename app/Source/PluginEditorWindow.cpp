#include "PluginEditorWindow.h"

namespace nc
{
PluginEditorWindow::PluginEditorWindow (const juce::String& title, juce::AudioProcessor* processor)
    : DocumentWindow (title, juce::Colours::darkgrey, DocumentWindow::allButtons, true)
    , processor_ (processor)
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);

    if (processor_ != nullptr && processor_->hasEditor())
    {
        editor_.reset (processor_->createEditorAndMakeActive());
        hostedEditor_ = editor_.get();
        setContentOwned (editor_.release(), true);
        setSize (juce::jmax (320, getContentComponent()->getWidth()),
                 juce::jmax (240, getContentComponent()->getHeight()));
    }

    setVisible (true);

    pluginEditors::watch (this);
}

PluginEditorWindow::~PluginEditorWindow()
{
    pluginEditors::unwatch (this);

    // createEditorAndMakeActive() registered this editor with the processor.
    // Deleting the component without saying so leaves
    // AudioProcessor::activeEditor pointing at freed memory; JUCE's own
    // format wrappers make the same call in their editors' destructors.
    if (hostedEditor_ != nullptr && processor_ != nullptr)
        processor_->editorBeingDeleted (hostedEditor_);

    if (getContentComponent() != nullptr)
        setContentComponent (nullptr);

    hostedEditor_ = nullptr;
    processor_ = nullptr;
}

void PluginEditorWindow::closeForProcessorGone()
{
    closeButtonPressed();
}

void PluginEditorWindow::closeButtonPressed()
{
    delete this;
}
} // namespace nc
