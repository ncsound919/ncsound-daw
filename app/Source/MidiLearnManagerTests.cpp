#include <catch2/catch_test_macros.hpp>

#include "MidiLearnManager.h"

#include <juce_core/juce_core.h>

namespace
{
juce::File tempMappingFile (const juce::String& label)
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
               .getChildFile ("nc-midilearn-" + label + "-"
                              + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())
                              + ".json");
}

nc::MidiLearnManager::Binding makeBinding (int channel, int controller, const char* target,
                                           bool isNote = false)
{
    nc::MidiLearnManager::Binding binding;
    binding.midiChannel = channel;
    binding.controller = controller;
    binding.isNote = isNote;
    binding.targetParamId = target;
    binding.learnArmed = false;
    return binding;
}
} // namespace

TEST_CASE ("learning binds the next controller event to the armed parameter", "[midilearn]")
{
    nc::MidiLearnManager manager;

    REQUIRE_FALSE (manager.isLearnArmed());
    REQUIRE_FALSE (manager.consumeNextCc (0, 74, 64, false));   // nothing armed

    manager.armLearn ("track.0.gain");
    REQUIRE (manager.isLearnArmed());

    REQUIRE (manager.consumeNextCc (3, 74, 64, false));

    // One event completes the learn.
    REQUIRE_FALSE (manager.isLearnArmed());
    REQUIRE_FALSE (manager.consumeNextCc (3, 75, 64, false));

    const auto binding = manager.bindingFor (3, 74, false);
    REQUIRE (binding.has_value());
    REQUIRE (binding->targetParamId == "track.0.gain");
    REQUIRE (binding->midiChannel == 3);
    REQUIRE (binding->controller == 74);
    REQUIRE_FALSE (binding->isNote);
}

TEST_CASE ("cancelling a learn leaves no binding behind", "[midilearn]")
{
    nc::MidiLearnManager manager;

    manager.armLearn ("track.1.gain");
    manager.cancelLearn();

    REQUIRE_FALSE (manager.isLearnArmed());
    REQUIRE_FALSE (manager.consumeNextCc (1, 20, 1, false));
    REQUIRE_FALSE (manager.bindingFor (1, 20, false).has_value());
}

TEST_CASE ("an omni binding answers for any channel, a channel binding wins", "[midilearn]")
{
    nc::MidiLearnManager manager;

    manager.addBinding (makeBinding (0, 71, "track.0.volume"));
    manager.addBinding (makeBinding (4, 71, "track.1.volume"));

    const auto omni = manager.bindingFor (9, 71, false);
    REQUIRE (omni.has_value());
    REQUIRE (omni->targetParamId == "track.0.volume");

    const auto exact = manager.bindingFor (4, 71, false);
    REQUIRE (exact.has_value());
    REQUIRE (exact->targetParamId == "track.1.volume");

    REQUIRE_FALSE (manager.bindingFor (2, 99, false).has_value());
}

TEST_CASE ("notes and controllers with the same number stay separate", "[midilearn]")
{
    nc::MidiLearnManager manager;

    manager.addBinding (makeBinding (0, 60, "track.0.mute", true));

    REQUIRE (manager.bindingFor (0, 60, true).has_value());
    REQUIRE_FALSE (manager.bindingFor (0, 60, false).has_value());
}

TEST_CASE ("bindings can be removed and listed", "[midilearn]")
{
    nc::MidiLearnManager manager;

    manager.addBinding (makeBinding (0, 20, "transport.play"));
    manager.addBinding (makeBinding (0, 21, "transport.stop"));
    REQUIRE (manager.bindings().size() == 2);

    manager.removeBinding (0, 20, false);
    REQUIRE (manager.bindings().size() == 1);
    REQUIRE_FALSE (manager.bindingFor (0, 20, false).has_value());

    manager.removeBinding (0, 21, false);
    REQUIRE (manager.bindings().empty());
}

TEST_CASE ("mappings survive a save and reload", "[midilearn]")
{
    nc::MidiLearnManager source;
    auto binding = makeBinding (2, 74, "track.3.hpfFreq");
    binding.mode = nc::MidiLearnManager::Mode::relative;
    source.addBinding (binding);
    source.addBinding (makeBinding (0, 60, "transport.play", true));

    const auto file = tempMappingFile ("roundtrip");
    source.saveToFile (file);
    REQUIRE (file.existsAsFile());

    nc::MidiLearnManager restored;
    REQUIRE (restored.loadFromFile (file));
    file.deleteFile();

    REQUIRE (restored.bindings().size() == 2);

    const auto cc = restored.bindingFor (2, 74, false);
    REQUIRE (cc.has_value());
    REQUIRE (cc->targetParamId == "track.3.hpfFreq");
    REQUIRE (cc->mode == nc::MidiLearnManager::Mode::relative);

    const auto note = restored.bindingFor (0, 60, true);
    REQUIRE (note.has_value());
    REQUIRE (note->targetParamId == "transport.play");
}

TEST_CASE ("a bad mapping file is refused and leaves existing bindings alone", "[midilearn]")
{
    nc::MidiLearnManager manager;
    manager.addBinding (makeBinding (0, 20, "transport.play"));

    REQUIRE_FALSE (manager.loadFromFile (tempMappingFile ("missing")));

    const auto notMappings = tempMappingFile ("wrong-shape");
    REQUIRE (notMappings.replaceWithText (R"({ "somethingElse": 1 })"));
    REQUIRE_FALSE (manager.loadFromFile (notMappings));
    notMappings.deleteFile();

    REQUIRE (manager.bindings().size() == 1);
}

TEST_CASE ("a built-in template installs its bindings and an unknown name is a no-op", "[midilearn]")
{
    nc::MidiLearnManager manager;

    manager.loadBuiltInTemplate ("Push 3");
    REQUIRE (manager.bindings().size() == 3);
    REQUIRE (manager.bindingFor (0, 71, false).has_value());

    const auto pushCount = manager.bindings().size();
    manager.loadBuiltInTemplate ("No Such Controller");
    REQUIRE (manager.bindings().size() == pushCount);
}
