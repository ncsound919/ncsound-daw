#include "PluginRegistry.h"

namespace nc
{
namespace
{
PluginStatus parseStatus (const juce::String& text)
{
    if (text == "native")   return PluginStatus::native;
    if (text == "port")     return PluginStatus::port;
    if (text == "archived") return PluginStatus::archived;
    return PluginStatus::external;
}

juce::String property (const juce::DynamicObject& object, const char* key)
{
    return object.getProperty (juce::Identifier (key)).toString();
}
} // namespace

int Registry::countWithStatus (PluginStatus status) const
{
    int total = 0;

    for (const auto& plugin : plugins)
        if (plugin.status == status)
            ++total;

    return total;
}

int Registry::countInstruments() const
{
    int total = 0;

    for (const auto& plugin : plugins)
        if (plugin.isInstrument())
            ++total;

    return total;
}

int Registry::countEffects() const
{
    return static_cast<int> (plugins.size()) - countInstruments();
}

Registry loadRegistry (const juce::File& jsonFile)
{
    Registry registry;

    if (! jsonFile.existsAsFile())
        return registry;

    juce::var parsed;
    const auto result = juce::JSON::parse (jsonFile.loadFileAsString(), parsed);

    if (result.failed())
        return registry;

    const auto* root = parsed.getDynamicObject();

    if (root == nullptr)
        return registry;

    const auto* entries = root->getProperty ("plugins").getArray();

    if (entries == nullptr)
        return registry;

    for (const auto& entry : *entries)
    {
        const auto* object = entry.getDynamicObject();

        if (object == nullptr)
            continue;

        PluginInfo plugin;
        plugin.id = property (*object, "id");
        plugin.name = property (*object, "name");
        plugin.kind = property (*object, "kind");
        plugin.source = property (*object, "source");
        plugin.note = property (*object, "note");
        plugin.status = parseStatus (property (*object, "status"));

        if (plugin.id.isNotEmpty() && plugin.name.isNotEmpty())
            registry.plugins.push_back (std::move (plugin));
    }

    return registry;
}

juce::String statusName (PluginStatus status)
{
    switch (status)
    {
        case PluginStatus::native:   return "native";
        case PluginStatus::external: return "VST3";
        case PluginStatus::port:     return "to port";
        case PluginStatus::archived: return "archived";
    }

    return {};
}
} // namespace nc
