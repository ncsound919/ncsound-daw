#include <catch2/catch_test_macros.hpp>

#include "HostedInsert.h"
#include "PluginCatalog.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace
{
/** Returns canned replies without launching a child process, so the catalog's
    reply handling is covered deterministically on machines with no VST3s. */
class FakeScannerBackend final : public nc::ScannerBackend
{
public:
    explicit FakeScannerBackend (nc::ScannerReply cannedReply) : reply (std::move (cannedReply)) {}

    nc::ScannerReply scan (const juce::String&, const juce::String&) override { return reply; }
    void cancel() override {}

private:
    nc::ScannerReply reply;
};

juce::PluginDescription makeDescription (const juce::String& name, const juce::String& file, int uniqueId)
{
    juce::PluginDescription description;
    description.name = name;
    description.descriptiveName = name;
    description.pluginFormatName = "VST3";
    description.category = "Fx";
    description.manufacturerName = "NCSound";
    description.version = "1.0.0";
    description.fileOrIdentifier = file;
    description.uniqueId = uniqueId;
    description.isInstrument = false;
    return description;
}

juce::String descriptionsXml (const std::vector<juce::PluginDescription>& descriptions)
{
    juce::XmlElement root { "SCAN" };

    for (const auto& description : descriptions)
        root.addChildElement (description.createXml().release());

    return root.toString();
}

nc::PluginInfo registryInfo (const juce::String& id,
                             const juce::String& name,
                             const juce::String& kind,
                             nc::PluginStatus status,
                             const juce::String& vst3Name = {})
{
    nc::PluginInfo info;
    info.id = id;
    info.name = name;
    info.kind = kind;
    info.status = status;
    info.vst3BundleName = vst3Name;
    return info;
}
} // namespace

TEST_CASE ("scanner reply descriptions round-trip through the catalog helper", "[catalog]")
{
    const auto first = makeDescription ("Overlay Reverb", "C:/VST3/overlay_reverb.vst3", 101);
    const auto second = makeDescription ("Overlay Delay", "C:/VST3/overlay_delay.vst3", 102);

    nc::ScannerReply reply;
    reply.ok = true;
    reply.descriptionsXml = descriptionsXml ({ first, second });

    std::vector<juce::PluginDescription> descriptions;
    juce::String error;

    REQUIRE (nc::PluginCatalog::appendDescriptionsFromReply (reply, descriptions, error));
    REQUIRE (error.isEmpty());
    REQUIRE (descriptions.size() == 2);
    REQUIRE (descriptions[0].name == "Overlay Reverb");
    REQUIRE (descriptions[0].pluginFormatName == "VST3");
    REQUIRE (descriptions[1].createIdentifierString() == second.createIdentifierString());
}

TEST_CASE ("a timed out scan reply is reported and adds nothing", "[catalog]")
{
    nc::ScannerReply reply;
    reply.ok = false;
    reply.timedOut = true;
    reply.descriptionsXml = descriptionsXml ({ makeDescription ("Ignored", "C:/VST3/x.vst3", 1) });

    std::vector<juce::PluginDescription> descriptions;
    juce::String error;

    REQUIRE_FALSE (nc::PluginCatalog::appendDescriptionsFromReply (reply, descriptions, error));
    REQUIRE (descriptions.empty());
    REQUIRE (error.containsIgnoreCase ("timed out"));
}

TEST_CASE ("a crashed scanner worker is reported and adds nothing", "[catalog]")
{
    nc::ScannerReply reply;
    reply.ok = false;
    reply.connectionLost = true;

    std::vector<juce::PluginDescription> descriptions;
    juce::String error;

    REQUIRE_FALSE (nc::PluginCatalog::appendDescriptionsFromReply (reply, descriptions, error));
    REQUIRE (descriptions.empty());
    REQUIRE (error.containsIgnoreCase ("crash"));
}

TEST_CASE ("an unreadable scanner response is reported", "[catalog]")
{
    nc::ScannerReply reply;
    reply.ok = true;
    reply.descriptionsXml = "this is not xml <<<";

    std::vector<juce::PluginDescription> descriptions;
    juce::String error;

    REQUIRE_FALSE (nc::PluginCatalog::appendDescriptionsFromReply (reply, descriptions, error));
    REQUIRE (descriptions.empty());
    REQUIRE (error.containsIgnoreCase ("unreadable"));
}

TEST_CASE ("catalog merge keeps registry rows, dedupes and sorts scanned rows", "[catalog]")
{
    std::vector<nc::PluginInfo> registry;
    registry.push_back (registryInfo ("overlay-reverb", "Overlay Reverb", "effect", nc::PluginStatus::external));
    registry.push_back (registryInfo ("chopstation", "Chop Station", "instrument", nc::PluginStatus::native));

    nc::PluginCatalog catalog (registry, std::make_unique<FakeScannerBackend> (nc::ScannerReply {}));

    REQUIRE (catalog.snapshot().size() == 2);

    const auto reverb = makeDescription ("Zeta Reverb", "C:/VST3/zeta.vst3", 201);
    const auto delay = makeDescription ("Alpha Delay", "C:/VST3/alpha.vst3", 202);

    catalog.mergeScannedDescriptions ({ reverb, reverb, delay });

    const auto entries = catalog.snapshot();
    REQUIRE (entries.size() == 4);

    std::vector<juce::String> names;
    for (const auto& entry : entries)
        names.push_back (entry.name);

    REQUIRE (names.front() == "Alpha Delay");
    REQUIRE (std::is_sorted (names.begin(), names.end(),
                             [] (const juce::String& a, const juce::String& b)
                             {
                                 return a.compareIgnoreCase (b) < 0;
                             }));

    auto scanned = 0;
    auto native = 0;

    for (const auto& entry : entries)
    {
        if (entry.external) ++scanned;
        if (! entry.external && entry.id == "chopstation") ++native;
    }

    REQUIRE (scanned == 3);   // registry external row plus the two deduped VST3 rows
    REQUIRE (native == 1);
}

TEST_CASE ("findDescription resolves only scanned external ids", "[catalog]")
{
    std::vector<nc::PluginInfo> registry;
    registry.push_back (registryInfo ("overlay-reverb", "Overlay Reverb", "effect", nc::PluginStatus::external));
    registry.push_back (registryInfo ("chopstation", "Chop Station", "instrument", nc::PluginStatus::native));

    nc::PluginCatalog catalog (registry);
    const auto delay = makeDescription ("Alpha Delay", "C:/VST3/alpha.vst3", 202);
    catalog.mergeScannedDescriptions ({ delay });

    REQUIRE_FALSE (catalog.findDescription ("chopstation").has_value());
    REQUIRE_FALSE (catalog.findDescription ("overlay-reverb").has_value());

    const auto found = catalog.findDescription (delay.createIdentifierString());
    REQUIRE (found.has_value());
    REQUIRE (found->name == "Alpha Delay");

    const auto info = catalog.asPluginInfo();

    for (const auto& row : info)
        REQUIRE (row.id != "chopstation");
}

TEST_CASE ("a registry row links to its installed VST3 by product name", "[catalog]")
{
    std::vector<nc::PluginInfo> registry;
    registry.push_back (registryInfo ("overlay-delay", "Overlay Delay", "effect",
                                      nc::PluginStatus::external, "Overlay Delay"));
    registry.push_back (registryInfo ("chopstation", "Chop Station", "instrument",
                                      nc::PluginStatus::native));

    nc::PluginCatalog catalog (registry);
    REQUIRE (catalog.snapshot().size() == 2);

    const auto delay = makeDescription ("Overlay Delay", "C:/VST3/Overlay Delay.vst3", 301);
    const auto other = makeDescription ("Some Other Synth", "C:/VST3/other.vst3", 302);

    catalog.mergeScannedDescriptions ({ delay, other });

    // Registry rows plus one scanned row the registry does not own.
    REQUIRE (catalog.snapshot().size() == 3);

    const auto linked = catalog.findDescription ("overlay-delay");
    REQUIRE (linked.has_value());
    REQUIRE (linked->name == "Overlay Delay");
    REQUIRE (linked->createIdentifierString() == delay.createIdentifierString());

    REQUIRE_FALSE (catalog.findDescription ("chopstation").has_value());

    const auto extra = catalog.asPluginInfo();
    REQUIRE (extra.size() == 1);
    REQUIRE (extra[0].name == "Some Other Synth");

    const auto entry = catalog.find ("overlay-delay");
    REQUIRE (entry.has_value());
    REQUIRE (entry->registryRow);
    REQUIRE (entry->scanned);
    REQUIRE (entry->external);
}

TEST_CASE ("an uninstalled registry row stays non-loadable", "[catalog]")
{
    std::vector<nc::PluginInfo> registry;
    registry.push_back (registryInfo ("temporal-delay", "Temporal Delay", "effect",
                                      nc::PluginStatus::external, "Temporal Delay Not Installed"));

    nc::PluginCatalog catalog (registry, std::make_unique<FakeScannerBackend> (nc::ScannerReply {}));

    REQUIRE (catalog.snapshot().size() == 1);
    REQUIRE_FALSE (catalog.findDescription ("temporal-delay").has_value());
    REQUIRE (catalog.asPluginInfo().empty());

    juce::String error;
    REQUIRE_FALSE (catalog.resolveDescription ("temporal-delay", error).has_value());
    REQUIRE (error.containsIgnoreCase ("not installed"));

    juce::String unknownError;
    REQUIRE_FALSE (catalog.resolveDescription ("not-a-plugin", unknownError).has_value());
    REQUIRE (unknownError.containsIgnoreCase ("scanned"));
}

TEST_CASE ("the shipped registry instantiates its installed suite plugins", "[.registry]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto registryFile = juce::File::getCurrentWorkingDirectory()
                                  .getChildFile ("plugins")
                                  .getChildFile ("registry.json");

    if (! registryFile.existsAsFile())
    {
        WARN ("plugins/registry.json not found from " << juce::File::getCurrentWorkingDirectory().getFullPathName());
        return;
    }

    const auto registry = nc::loadRegistry (registryFile);
    REQUIRE (registry.plugins.size() > 0);

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    std::vector<juce::PluginDescription> scanned;
    const auto contentsSegment = juce::File::getSeparatorString() + "Contents"
                               + juce::File::getSeparatorString();

    for (auto* format : formats.getFormats())
    {
        if (format->getName() != "VST3")
            continue;

        const auto locations = format->getDefaultLocationsToSearch();

        for (int i = 0; i < locations.getNumPaths(); ++i)
        {
            if (! locations[i].isDirectory())
                continue;

            for (const auto& entry : juce::RangedDirectoryIterator (locations[i], true, "*.vst3",
                                                                   juce::File::findFilesAndDirectories))
            {
                const auto file = entry.getFile();

                if (file.getFullPathName().containsIgnoreCase (contentsSegment))
                    continue;

                juce::OwnedArray<juce::PluginDescription> descriptions;
                format->findAllTypesForFile (descriptions, file.getFullPathName());

                for (const auto* description : descriptions)
                    if (description != nullptr)
                        scanned.push_back (*description);
            }
        }
    }

    // The test executable is not a scanner worker, so feed the catalog the same
    // descriptions a folder scan would produce.
    nc::PluginCatalog catalog (registry.plugins);
    catalog.mergeScannedDescriptions (scanned);

    auto checked = 0;

    for (const auto& info : registry.plugins)
    {
        if (info.kind != "effect" || info.vst3BundleName.isEmpty())
            continue;

        const auto description = catalog.findDescription (info.id);

        if (! description.has_value())
            continue;   // not installed on this machine

        INFO ("Registry id " << info.id << " -> " << description->name);
        REQUIRE (description->name == info.vst3BundleName);

        juce::String error;
        auto instance = catalog.createSync (*description, 48000.0, 512, error);
        REQUIRE (instance != nullptr);

        // Effects must also pass the insert wrapper's stereo-input check.
        auto hosted = nc::HostedInsert::create (std::move (instance), 48000.0, 512, error);
        REQUIRE (hosted != nullptr);
        ++checked;
    }

    WARN ("Instantiated " << checked << " installed suite effects from registry ids");
    REQUIRE (checked > 0);
}

TEST_CASE ("an additional search root resolves a bundle outside the default locations",
           "[catalog]")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("nc-catalog-roots-"
                                         + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    REQUIRE (root.createDirectory().wasOk());

    const auto bundle = root.getChildFile ("Root Only.vst3");
    REQUIRE (bundle.createDirectory().wasOk());

    std::vector<nc::PluginInfo> registry;
    registry.push_back (registryInfo ("root-only", "Root Only", "effect",
                                      nc::PluginStatus::external, "Root Only"));

    nc::ScannerReply reply;
    reply.ok = true;
    reply.descriptionsXml = descriptionsXml ({ makeDescription ("Root Only",
                                                                bundle.getFullPathName(), 401) });

    nc::PluginCatalog withRoot (registry, std::make_unique<FakeScannerBackend> (reply));
    withRoot.setAdditionalSearchRoots ({ root });

    juce::String error;
    const auto resolved = withRoot.resolveDescription ("root-only", error);
    REQUIRE (resolved.has_value());
    REQUIRE (resolved->name == "Root Only");
    REQUIRE (error.isEmpty());

    // The same registry row is unloadable without the root: this is the state a
    // local build is in before search-paths.txt names its folder.
    nc::PluginCatalog defaultOnly (registry, std::make_unique<FakeScannerBackend> (reply));
    juce::String missing;
    REQUIRE_FALSE (defaultOnly.resolveDescription ("root-only", missing).has_value());
    REQUIRE (missing.containsIgnoreCase ("not installed"));

    root.deleteRecursively();
}
