#include "PluginEditorWindow.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
/** Creates the message manager JUCE requires before any Component, and shuts it
    down after the case; every case here touches a real window. */
struct GuiThread
{
    juce::ScopedJuceInitialiser_GUI initialiser;
};

class StubEditor final : public juce::AudioProcessorEditor
{
public:
    explicit StubEditor (juce::AudioProcessor& owner)
        : juce::AudioProcessorEditor (owner)
    {
        setSize (320, 240);
    }
};

/** The smallest processor that reports an editor, so window lifetime can be
    tested without loading a plug-in. */
class StubProcessor final : public juce::AudioProcessor
{
public:
    StubProcessor()
        : juce::AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return "Stub"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new StubEditor (*this); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};
} // namespace

TEST_CASE ("closing editor windows for a retired processor clears its active editor",
           "[window][lifetime]")
{
    const GuiThread gui;
    REQUIRE (nc::pluginEditors::watcherCount() == 0);

    StubProcessor processor;
    new nc::PluginEditorWindow ("Stub", &processor);

    REQUIRE (nc::pluginEditors::watcherCount() == 1);
    REQUIRE (processor.getActiveEditor() != nullptr);

    nc::pluginEditors::closeWindowsFor (&processor);

    CHECK (nc::pluginEditors::watcherCount() == 0);
    CHECK (processor.getActiveEditor() == nullptr);
}

TEST_CASE ("closing one processor's editor leaves another window open",
           "[window][lifetime]")
{
    const GuiThread gui;
    REQUIRE (nc::pluginEditors::watcherCount() == 0);

    StubProcessor first, second;
    new nc::PluginEditorWindow ("First", &first);
    new nc::PluginEditorWindow ("Second", &second);

    REQUIRE (nc::pluginEditors::watcherCount() == 2);

    nc::pluginEditors::closeWindowsFor (&first);

    CHECK (nc::pluginEditors::watcherCount() == 1);
    CHECK (first.getActiveEditor() == nullptr);
    CHECK (second.getActiveEditor() != nullptr);

    nc::pluginEditors::closeWindowsFor (&second);
    CHECK (nc::pluginEditors::watcherCount() == 0);
}

TEST_CASE ("destroying an editor window unregisters it and releases the editor",
           "[window][lifetime]")
{
    const GuiThread gui;
    REQUIRE (nc::pluginEditors::watcherCount() == 0);

    StubProcessor processor;
    auto* window = new nc::PluginEditorWindow ("Direct", &processor);
    REQUIRE (nc::pluginEditors::watcherCount() == 1);

    delete window;

    CHECK (nc::pluginEditors::watcherCount() == 0);
    CHECK (processor.getActiveEditor() == nullptr);
}

TEST_CASE ("a window without a processor closes cleanly", "[window][lifetime]")
{
    const GuiThread gui;
    REQUIRE (nc::pluginEditors::watcherCount() == 0);

    auto* window = new nc::PluginEditorWindow ("Empty", nullptr);
    REQUIRE (nc::pluginEditors::watcherCount() == 1);

    // A null target must not close anything: closeWindowsFor() is called from
    // the rack for every retired insert, including one that never had a host.
    nc::pluginEditors::closeWindowsFor (nullptr);
    CHECK (nc::pluginEditors::watcherCount() == 1);

    delete window;
    CHECK (nc::pluginEditors::watcherCount() == 0);
}
