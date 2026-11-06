#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace nc
{
enum class PluginStatus
{
    native,
    external,
    port,
    archived
};

struct PluginInfo
{
    juce::String id;
    juce::String name;
    juce::String kind;
    juce::String source;
    juce::String note;
    PluginStatus status = PluginStatus::external;

    bool isInstrument() const { return kind == "instrument"; }
};

struct Registry
{
    std::vector<PluginInfo> plugins;

    int countWithStatus (PluginStatus status) const;
    int countInstruments() const;
    int countEffects() const;
};

/** Parses plugins/registry.json. Returns an empty registry when the file is missing or malformed. */
Registry loadRegistry (const juce::File& jsonFile);

juce::String statusName (PluginStatus status);

} // namespace nc
