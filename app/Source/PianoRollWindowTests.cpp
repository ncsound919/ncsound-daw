#include "PianoRollWindow.h"
#include "PluginEditorRegistry.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>

namespace
{
/** Creates the message manager JUCE requires before any Component, and shuts it
    down after the case; every case here touches a real window. */
struct GuiThread
{
    juce::ScopedJuceInitialiser_GUI initialiser;
};

/** Stands in for PianoRoll: the window only needs something to own and show.
    Counting live instances is how the cases prove the window deleted what it
    was given, without touching freed memory. */
class StubContent final : public juce::Component
{
public:
    StubContent()
    {
        setSize (760, 420);
        ++liveCount;
    }

    ~StubContent() override { --liveCount; }

    static std::atomic<int> liveCount;
};

std::atomic<int> StubContent::liveCount { 0 };
} // namespace

TEST_CASE ("the close button deletes a piano roll window and its editor", "[window][piano-roll]")
{
    const GuiThread gui;
    REQUIRE (StubContent::liveCount == 0);

    juce::Component::SafePointer<nc::PianoRollWindow> window (
        new nc::PianoRollWindow ("Clip / Piano Roll", std::make_unique<StubContent>()));

    REQUIRE (window.getComponent() != nullptr);
    REQUIRE (StubContent::liveCount == 1);

    auto* content = window->getContentComponent();
    REQUIRE (content != nullptr);
    CHECK (content->getParentComponent() == window.getComponent());
    // centreWithSize() sizes the window; the content is inset by the window
    // border, so it is never exactly 760 wide.
    CHECK (window->getWidth() == 760);
    CHECK (content->getWidth() > 0);
    CHECK (window->isVisible());

    // The close button is the only close path a user has. DocumentWindow's
    // default implementation only asserts, so this is what stops the leak.
    window->closeButtonPressed();

    CHECK (window.getComponent() == nullptr);
    CHECK (StubContent::liveCount == 0);
}

TEST_CASE ("closing one piano roll window leaves another open", "[window][piano-roll]")
{
    const GuiThread gui;
    REQUIRE (StubContent::liveCount == 0);

    juce::Component::SafePointer<nc::PianoRollWindow> first (
        new nc::PianoRollWindow ("First", std::make_unique<StubContent>()));
    juce::Component::SafePointer<nc::PianoRollWindow> second (
        new nc::PianoRollWindow ("Second", std::make_unique<StubContent>()));

    REQUIRE (StubContent::liveCount == 2);

    first->closeButtonPressed();

    CHECK (first.getComponent() == nullptr);
    CHECK (second.getComponent() != nullptr);
    CHECK (StubContent::liveCount == 1);

    second->closeButtonPressed();
    CHECK (StubContent::liveCount == 0);
}

TEST_CASE ("a piano roll window is not part of the plugin editor registry", "[window][piano-roll]")
{
    const GuiThread gui;
    REQUIRE (nc::pluginEditors::watcherCount() == 0);

    juce::Component::SafePointer<nc::PianoRollWindow> window (
        new nc::PianoRollWindow ("Clip / Piano Roll", std::make_unique<StubContent>()));

    // The piano roll edits clip data, not a hosted processor, so it has no
    // AudioProcessor to retire. Registering it would make the rack's
    // closeWindowsFor() delete a window that has nothing to do with the insert.
    CHECK (nc::pluginEditors::watcherCount() == 0);

    window->closeButtonPressed();

    CHECK (nc::pluginEditors::watcherCount() == 0);
    CHECK (StubContent::liveCount == 0);
}
