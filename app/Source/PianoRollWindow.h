#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace nc
{
/** Pop-up host for the piano roll.

    DocumentWindow's default closeButtonPressed() only asserts, so a window
    without this override can never be closed and every open leaks the window
    and its editor. This one deletes itself, which is also why it must be
    created with `new()` and never owned by a smart pointer.

    The content is a plain Component rather than a PianoRoll so that window
    lifetime can be tested without constructing a piano roll; ArrangeView still
    passes it a PianoRoll, and the window takes ownership either way.
*/
class PianoRollWindow final : public juce::DocumentWindow
{
public:
    PianoRollWindow (const juce::String& title, std::unique_ptr<juce::Component> content);

    /** Deletes the window, and with it the content it owns. */
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollWindow)
};
} // namespace nc
