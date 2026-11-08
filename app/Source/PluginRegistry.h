#pragma once

#include <juce_core/juce_core.h>

#include <nc/engine/PluginPolicy.hpp>

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

    /** Product name the installed VST3 bundle reports. When set, the catalog
        links this registry row to that scanned description, so one plugin shows
        up once and loads by its stable registry id. */
    juce::String vst3BundleName;
    PluginStatus status = PluginStatus::external;

    /** Ships in the box. A stock row is always visible in the browser and is
        never hidden by an empty scan, so a fresh install still has the whole
        core instrument and effect set. */
    bool stock = false;

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

/** Instantiation tier for a registry status, shared with the load path. */
nc::engine::PluginTier pluginTier (PluginStatus status);

} // namespace nc
