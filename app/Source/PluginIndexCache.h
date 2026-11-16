#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>
#include <vector>

namespace nc::pluginindex
{
/** One cached bundle. The size and mtime are the fingerprint: a bundle that was
    upgraded in place usually keeps its path, and only the fingerprint says the
    cached description may now be wrong.

    For a .vst3 *directory* bundle the fingerprint is the directory itself, so
    replacing the folder (the normal install path) is detected while editing a
    file deep inside it may not be. That is deliberate: a cheap check that
    catches reinstalls beats an expensive hash of every bundle on every start. */
struct Entry
{
    juce::String path;
    juce::int64 size = 0;
    juce::int64 modifiedMs = 0;
    juce::String stateXml;   // PluginDescription::createXml()->toString()
};

/** Parses the index and keeps only entries whose bundle still exists at the
    recorded size and mtime. A missing file, a parse error or an index written
    by another schema version returns {} - the whole cache is ignored, never
    half-read. */
std::vector<Entry> loadValid (const juce::File& indexFile);

/** A valid entry's description, or nothing when its XML is not a description. */
std::optional<juce::PluginDescription> descriptionFor (const Entry& entry);

/** Valid descriptions in the index, ready to merge into the catalog. */
std::vector<juce::PluginDescription> loadDescriptions (const juce::File& indexFile);

/** Writes one entry per description, keyed by the bundle's size and mtime.
    Bundles that no longer exist are skipped; a write failure returns false and
    leaves any previous index untouched. */
bool save (const juce::File& indexFile, const std::vector<juce::PluginDescription>& descriptions);

/** The index the app uses: <userApplicationData>/NCSound Music Hub/plugin-index.json */
juce::File defaultIndexFile();
} // namespace nc::pluginindex
