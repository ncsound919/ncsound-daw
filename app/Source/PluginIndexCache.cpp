#include "PluginIndexCache.h"

namespace nc::pluginindex
{
namespace
{
// Bump when the stored shape changes. An index from another version is ignored
// wholesale, which is cheaper to reason about than a partial migration.
constexpr int schemaVersion = 1;
constexpr const char* formatTag = "NCMixRackPluginIndex";

juce::int64 modificationMs (const juce::File& file)
{
    return file.getLastModificationTime().toMilliseconds();
}
} // namespace

juce::File defaultIndexFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("NCSound Music Hub")
        .getChildFile ("plugin-index.json");
}

std::vector<Entry> loadValid (const juce::File& indexFile)
{
    std::vector<Entry> valid;

    if (! indexFile.existsAsFile())
        return valid;

    const auto parsed = juce::JSON::parse (indexFile.loadFileAsString());

    if (! parsed.isObject())
        return valid;

    auto* root = parsed.getDynamicObject();

    if (root == nullptr)
        return valid;

    if (root->getProperty ("format").toString() != juce::String (formatTag)
        || static_cast<int> (root->getProperty ("version")) != schemaVersion)
        return valid;

    const auto* entries = root->getProperty ("entries").getArray();

    if (entries == nullptr)
        return valid;

    for (const auto& item : *entries)
    {
        const auto* object = item.getDynamicObject();

        if (object == nullptr)
            continue;

        Entry entry;
        entry.path = object->getProperty ("path").toString();
        entry.size = static_cast<juce::int64> (object->getProperty ("size"));
        entry.modifiedMs = static_cast<juce::int64> (object->getProperty ("mtime"));
        entry.stateXml = object->getProperty ("xml").toString();

        if (entry.path.isEmpty() || entry.stateXml.isEmpty())
            continue;

        const juce::File bundle (entry.path);

        if (! bundle.exists())
            continue;

        // Both parts of the fingerprint must match: a bundle restored from a
        // backup with an older timestamp but a different size is still stale.
        if (static_cast<juce::int64> (bundle.getSize()) != entry.size)
            continue;

        if (modificationMs (bundle) != entry.modifiedMs)
            continue;

        valid.push_back (std::move (entry));
    }

    return valid;
}

std::optional<juce::PluginDescription> descriptionFor (const Entry& entry)
{
    const auto xml = juce::parseXML (entry.stateXml);

    if (xml == nullptr)
        return std::nullopt;

    juce::PluginDescription description;

    if (! description.loadFromXml (*xml))
        return std::nullopt;

    return description;
}

std::vector<juce::PluginDescription> loadDescriptions (const juce::File& indexFile)
{
    std::vector<juce::PluginDescription> descriptions;

    for (const auto& entry : loadValid (indexFile))
        if (auto description = descriptionFor (entry))
            descriptions.push_back (std::move (*description));

    return descriptions;
}

bool save (const juce::File& indexFile, const std::vector<juce::PluginDescription>& descriptions)
{
    juce::Array<juce::var> entries;

    for (const auto& description : descriptions)
    {
        if (description.fileOrIdentifier.isEmpty())
            continue;

        const juce::File bundle (description.fileOrIdentifier);

        // A description for something that is not on disk cannot be validated
        // later, so it is not worth an index slot.
        if (! bundle.exists())
            continue;

        const auto xml = description.createXml();

        if (xml == nullptr)
            continue;

        auto* object = new juce::DynamicObject();
        object->setProperty ("path", description.fileOrIdentifier);
        object->setProperty ("size", static_cast<juce::int64> (bundle.getSize()));
        object->setProperty ("mtime", modificationMs (bundle));
        object->setProperty ("xml", xml->toString (juce::XmlElement::TextFormat {}));
        entries.add (juce::var (object));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", formatTag);
    root->setProperty ("version", schemaVersion);
    root->setProperty ("entries", entries);

    const auto json = juce::JSON::toString (juce::var (root), false);

    // Write through a sibling file and rename, so a crash mid-write cannot
    // leave a truncated index that the next start would have to reject.
    const auto temporary = indexFile.getSiblingFile (indexFile.getFileName() + ".tmp");

    if (indexFile.getParentDirectory().createDirectory().failed())
        return false;

    if (! temporary.replaceWithText (json))
        return false;

    return temporary.moveFileTo (indexFile);
}
} // namespace nc::pluginindex
