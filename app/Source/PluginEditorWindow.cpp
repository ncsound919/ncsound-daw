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
        setContentOwned (editor_.release(), true);
        setSize (juce::jmax (320, getContentComponent()->getWidth()),
                 juce::jmax (240, getContentComponent()->getHeight()));
    }

    setVisible (true);
}

PluginEditorWindow::~PluginEditorWindow()
{
    setContentComponent (nullptr);
}

void PluginEditorWindow::closeButtonPressed()
{
    delete this;
}
} // namespace nc
