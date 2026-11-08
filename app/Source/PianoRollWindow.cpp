#include "PianoRollWindow.h"

namespace nc
{
PianoRollWindow::PianoRollWindow (const juce::String& title,
                                  std::unique_ptr<juce::Component> content)
    : juce::DocumentWindow (title, juce::Colours::black, juce::DocumentWindow::closeButton)
{
    setResizable (true, true);
    setUsingNativeTitleBar (false);
    setContentOwned (content.release(), true);
    centreWithSize (760, 420);
    setVisible (true);
}

void PianoRollWindow::closeButtonPressed()
{
    delete this;
}
} // namespace nc
