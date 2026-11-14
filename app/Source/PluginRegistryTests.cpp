#include <catch2/catch_test_macros.hpp>

#include "PluginRegistry.h"

#include <juce_core/juce_core.h>

namespace
{
/** A throwaway registry file, so parsing is covered without touching the
    shipped plugins/registry.json. */
juce::File writeRegistry (const juce::String& jsonText)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("nc-registry-"
                                   + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())
                                   + ".json");

    REQUIRE (file.replaceWithText (jsonText));
    return file;
}
} // namespace

TEST_CASE ("a registry row keeps every field it declares", "[registry]")
{
    const auto file = writeRegistry (R"({
        "plugins": [
            { "id": "chopstation", "name": "Chop Station", "kind": "instrument",
              "source": "built", "note": "ships in the box", "vst3": "Chop Station",
              "status": "native", "stock": "1" }
        ]
    })");

    const auto registry = nc::loadRegistry (file);
    file.deleteFile();

    REQUIRE (registry.plugins.size() == 1);

    const auto& plugin = registry.plugins.front();
    REQUIRE (plugin.id == "chopstation");
    REQUIRE (plugin.name == "Chop Station");
    REQUIRE (plugin.kind == "instrument");
    REQUIRE (plugin.source == "built");
    REQUIRE (plugin.note == "ships in the box");
    REQUIRE (plugin.vst3BundleName == "Chop Station");
    REQUIRE (plugin.status == nc::PluginStatus::native);
    REQUIRE (plugin.stock);
    REQUIRE (plugin.isInstrument());
}

TEST_CASE ("rows without an id or a name are dropped", "[registry]")
{
    const auto file = writeRegistry (R"({
        "plugins": [
            { "id": "keep", "name": "Keep Me" },
            { "id": "", "name": "No Id" },
            { "id": "no-name", "name": "" },
            { "id": "also-keep", "name": "Also Keep" }
        ]
    })");

    const auto registry = nc::loadRegistry (file);
    file.deleteFile();

    REQUIRE (registry.plugins.size() == 2);
    REQUIRE (registry.plugins[0].id == "keep");
    REQUIRE (registry.plugins[1].id == "also-keep");
}

TEST_CASE ("an unknown status falls back to external and stock needs the literal 1", "[registry]")
{
    const auto file = writeRegistry (R"({
        "plugins": [
            { "id": "known", "name": "Known", "status": "port", "stock": "1" },
            { "id": "unknown", "name": "Unknown", "status": "nonsense", "stock": "0" },
            { "id": "absent", "name": "Absent" }
        ]
    })");

    const auto registry = nc::loadRegistry (file);
    file.deleteFile();

    REQUIRE (registry.plugins.size() == 3);
    REQUIRE (registry.plugins[0].status == nc::PluginStatus::port);
    REQUIRE (registry.plugins[0].stock);
    REQUIRE (registry.plugins[1].status == nc::PluginStatus::external);
    REQUIRE_FALSE (registry.plugins[1].stock);
    REQUIRE (registry.plugins[2].status == nc::PluginStatus::external);
    REQUIRE_FALSE (registry.plugins[2].stock);
}

TEST_CASE ("a missing registry file is an empty registry, not a failure", "[registry]")
{
    const auto missing = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("nc-registry-never-written.json");
    missing.deleteFile();

    const auto registry = nc::loadRegistry (missing);

    REQUIRE (registry.plugins.empty());
    REQUIRE (registry.countInstruments() == 0);
    REQUIRE (registry.countEffects() == 0);
}

TEST_CASE ("malformed or wrongly shaped json is an empty registry", "[registry]")
{
    for (const auto* text : { "{ this is not json",
                              "[]",
                              R"({ "plugins": "not an array" })",
                              R"({ "plugins": [ 1, 2, 3 ] })" })
    {
        INFO ("json: " << text);

        const auto file = writeRegistry (text);
        const auto registry = nc::loadRegistry (file);
        file.deleteFile();

        REQUIRE (registry.plugins.empty());
    }
}

TEST_CASE ("counts split instruments from effects by kind", "[registry]")
{
    const auto file = writeRegistry (R"({
        "plugins": [
            { "id": "a", "name": "A", "kind": "instrument" },
            { "id": "b", "name": "B", "kind": "effect" },
            { "id": "c", "name": "C", "kind": "instrument", "status": "native" },
            { "id": "d", "name": "D", "kind": "effect", "status": "native" }
        ]
    })");

    const auto registry = nc::loadRegistry (file);
    file.deleteFile();

    REQUIRE (registry.plugins.size() == 4);
    REQUIRE (registry.countInstruments() == 2);
    REQUIRE (registry.countEffects() == 2);
    REQUIRE (registry.countWithStatus (nc::PluginStatus::native) == 2);
    REQUIRE (registry.countWithStatus (nc::PluginStatus::external) == 2);
}

TEST_CASE ("status names and tiers match what the load path expects", "[registry]")
{
    REQUIRE (nc::statusName (nc::PluginStatus::native) == "native");
    REQUIRE (nc::statusName (nc::PluginStatus::external) == "VST3");
    REQUIRE (nc::statusName (nc::PluginStatus::port) == "to port");
    REQUIRE (nc::statusName (nc::PluginStatus::archived) == "archived");

    using nc::engine::PluginTier;
    REQUIRE (nc::pluginTier (nc::PluginStatus::native) == PluginTier::native);
    REQUIRE (nc::pluginTier (nc::PluginStatus::port) == PluginTier::port);
    REQUIRE (nc::pluginTier (nc::PluginStatus::archived) == PluginTier::archived);
    REQUIRE (nc::pluginTier (nc::PluginStatus::external) == PluginTier::external);
}
