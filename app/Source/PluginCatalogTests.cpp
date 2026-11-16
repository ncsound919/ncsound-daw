#include <catch2/catch_test_macros.hpp>

#include "HostedInsert.h"
#include "PluginCatalog.h"
#include "PluginIndexCache.h"

#include "TestPaths.h"

#include <algorithm>
#include <functional>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace
{
/** Returns canned replies without launching a child process, so the catalog's
    reply handling is covered deterministically on machines with no VST3s. */
class FakeScannerBackend final : public nc::ScannerBackend
{
public:
    explicit FakeScannerBackend (nc::ScannerReply cannedReply) : reply (std::move (cannedReply)) {}

    nc::ScannerReply scan (const juce::String& formatName, const juce::String& file) override
    {
        const auto index = scans++;
        lastFormat = formatName;
        lastFile = file;
        return onScan ? onScan (index) : reply;
    }

    void cancel() override {}
    std::vector<juce::String> injectedBundles() const override { return bundles; }

    std::vector<juce::String> bundles;
    std::function<nc::ScannerReply (int)> onScan;
    int scans = 0;
    juce::String lastFormat;
    juce::String lastFile;

private:
    nc::ScannerReply reply;
};

/** plugins/registry.json from the repository root, which CMake baked into the
    build. Reading it relative to the CWD made these cases skip under CTest. */
juce::File repoRegistryFile()
{
    return juce::File (juce::String::fromUTF8 (ncRepoRoot))
               .getChildFile ("plugins")
               .getChildFile ("registry.json");
}

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

TEST_CASE ("the shipped registry links its own built instruments instead of duplicating them", "[catalog]")
{
    // Chop Station and 808 Designer are built by this repository. Before the
    // registry named their bundles they appeared twice: a 'native' row that
    // could never load, and a separate scanned row that could. Each suite
    // instrument has to be exactly one row, and that row has to load.
    const auto registryFile = repoRegistryFile();

    // A hard failure, not a warning: as a warning-and-return this case dropped
    // every assertion below whenever CTest ran it from build/app, and the
    // suite still reported success.
    if (! registryFile.existsAsFile())
        FAIL ("plugins/registry.json not found at " << registryFile.getFullPathName());

    const auto registry = nc::loadRegistry (registryFile);
    nc::PluginCatalog catalog (registry.plugins);

    auto chop = makeDescription ("Chop Station", "C:/build/Chop Station.vst3", 501);
    chop.isInstrument = true;
    chop.category = "Instrument";

    auto eight = makeDescription ("808 Designer", "C:/build/808 Designer.vst3", 502);
    eight.isInstrument = true;
    eight.category = "Instrument";

    catalog.mergeScannedDescriptions ({ chop, eight });

    struct Expected { const char* id; const char* bundle; };

    for (const auto& expected : { Expected { "chopstation", "Chop Station" },
                                  Expected { "eightoheight", "808 Designer" } })
    {
        INFO ("registry id " << expected.id << " -> " << expected.bundle);

        const auto entry = catalog.find (expected.id);
        REQUIRE (entry.has_value());
        REQUIRE (entry->registryRow);
        REQUIRE (entry->scanned);
        REQUIRE (entry->external);
        REQUIRE (entry->isInstrument);

        const auto description = catalog.findDescription (expected.id);
        REQUIRE (description.has_value());
        REQUIRE (description->name == expected.bundle);

        // A dead registry row beside a scanned duplicate is the bug this pins.
        auto rowsWithThisName = 0;

        for (const auto& row : catalog.snapshot())
            if (row.name.equalsIgnoreCase (expected.bundle))
                ++rowsWithThisName;

        REQUIRE (rowsWithThisName == 1);
    }
}

TEST_CASE ("the shipped registry instantiates its installed suite plugins", "[.registry]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto registryFile = repoRegistryFile();

    if (! registryFile.existsAsFile())
    {
        WARN ("plugins/registry.json not found at " << registryFile.getFullPathName());
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

TEST_CASE ("scan progress advances once per bundle", "[catalog]")
{
    auto backend = std::make_unique<FakeScannerBackend> (nc::ScannerReply {});
    backend->bundles = { "a.vst3", "b.vst3", "c.vst3" };
    backend->onScan = [] (int)
    {
        juce::Thread::sleep (40);
        return nc::ScannerReply {};
    };

    nc::PluginCatalog catalog ({}, std::move (backend));

    // scanSync on a worker thread, not scanAsync: the async path publishes its
    // final state through MessageManager::callAsync, which never runs in a
    // console test, so the progress polling below would wait forever.
    std::atomic<bool> finished { false };
    std::thread worker ([&]
    {
        catalog.scanSync();
        finished.store (true, std::memory_order_release);
    });

    std::vector<float> seen;

    while (! finished.load (std::memory_order_acquire))
    {
        seen.push_back (catalog.scanProgress());
        juce::Thread::sleep (5);
    }

    worker.join();

    REQUIRE (seen.size() > 3);
    CHECK (seen.front() < 0.5f);
    CHECK (std::is_sorted (seen.begin(), seen.end()));
    CHECK (catalog.scanProgress() == 1.0f);
}

TEST_CASE ("a finished scan reports every bundle it consumed", "[catalog]")
{
    auto backend = std::make_unique<FakeScannerBackend> (nc::ScannerReply {});
    backend->bundles = { "a.vst3", "b.vst3" };
    auto* raw = backend.get();

    nc::PluginCatalog catalog ({}, std::move (backend));
    catalog.scanSync();

    CHECK (raw->scans == 2);
    CHECK (catalog.scanProgress() == 1.0f);
}

namespace
{
juce::File freshIndexDir()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("nc-index-"
                                        + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    dir.createDirectory();
    return dir;
}
} // namespace

TEST_CASE ("the index round-trips and drops a bundle whose size changed", "[index]")
{
    const auto dir = freshIndexDir();
    const auto bundle = dir.getChildFile ("x.vst3");
    REQUIRE (bundle.replaceWithText ("one"));
    const auto index = dir.getChildFile ("plugin-index.json");

    juce::PluginDescription description;
    description.name = "X";
    description.fileOrIdentifier = bundle.getFullPathName();

    REQUIRE (nc::pluginindex::save (index, { description }));

    const auto loaded = nc::pluginindex::loadValid (index);
    REQUIRE (loaded.size() == 1);
    CHECK (loaded.front().path == bundle.getFullPathName());
    CHECK (loaded.front().size == bundle.getSize());

    // The description survives the round trip, not just the path.
    const auto descriptions = nc::pluginindex::loadDescriptions (index);
    REQUIRE (descriptions.size() == 1);
    CHECK (descriptions.front().name == "X");

    // A bigger bundle is stale even though the path and the folder are unchanged.
    REQUIRE (bundle.replaceWithText ("one and a lot more"));
    CHECK (nc::pluginindex::loadValid (index).empty());

    dir.deleteRecursively();
}

TEST_CASE ("a rewritten index ignores an older schema wholesale", "[index]")
{
    const auto dir = freshIndexDir();
    const auto bundle = dir.getChildFile ("y.vst3");
    REQUIRE (bundle.replaceWithText ("one"));
    const auto index = dir.getChildFile ("plugin-index.json");

    juce::PluginDescription description;
    description.name = "Y";
    description.fileOrIdentifier = bundle.getFullPathName();
    REQUIRE (nc::pluginindex::save (index, { description }));
    REQUIRE (nc::pluginindex::loadValid (index).size() == 1);

    // Edit the parsed tree rather than the text: the writer's spacing is its
    // own business, and a text match would silently stop testing anything.
    auto parsed = juce::JSON::parse (index.loadFileAsString());
    auto* root = parsed.getDynamicObject();
    REQUIRE (root != nullptr);
    root->setProperty ("version", 0);
    REQUIRE (index.replaceWithText (juce::JSON::toString (parsed, false)));

    // Not "the valid subset": an unrecognised version means the cache is not
    // trusted at all, so nothing is reused and a full scan follows.
    CHECK (nc::pluginindex::loadValid (index).empty());

    dir.deleteRecursively();
}

TEST_CASE ("a corrupt index is ignored, not half-read", "[index]")
{
    const auto dir = freshIndexDir();
    const auto index = dir.getChildFile ("plugin-index.json");
    REQUIRE (index.replaceWithText ("{ this is not json"));

    CHECK (nc::pluginindex::loadValid (index).empty());
    CHECK (nc::pluginindex::loadDescriptions (index).empty());
    CHECK (nc::pluginindex::loadValid (dir.getChildFile ("absent.json")).empty());

    dir.deleteRecursively();
}

TEST_CASE ("the index skips a description whose bundle is gone", "[index]")
{
    const auto dir = freshIndexDir();
    const auto bundle = dir.getChildFile ("gone.vst3");
    REQUIRE (bundle.replaceWithText ("one"));
    const auto index = dir.getChildFile ("plugin-index.json");

    juce::PluginDescription description;
    description.name = "Gone";
    description.fileOrIdentifier = bundle.getFullPathName();
    REQUIRE (nc::pluginindex::save (index, { description }));

    bundle.deleteFile();
    CHECK (nc::pluginindex::loadValid (index).empty());

    dir.deleteRecursively();
}

TEST_CASE ("a warm index skips the worker entirely", "[catalog]")
{
    const auto dir = freshIndexDir();
    const auto bundle = dir.getChildFile ("warm.vst3");
    REQUIRE (bundle.replaceWithText ("one"));
    const auto index = dir.getChildFile ("plugin-index.json");

    // First run: a real scan that reports one plug-in and writes the index.
    nc::ScannerReply reply;
    reply.ok = true;
    reply.descriptionsXml = descriptionsXml ({ makeDescription ("Warm", bundle.getFullPathName(), 501) });

    auto firstBackend = std::make_unique<FakeScannerBackend> (reply);
    firstBackend->bundles = { bundle.getFullPathName() };

    nc::PluginCatalog warm ({}, std::move (firstBackend), index);
    warm.scanSync();
    REQUIRE (warm.snapshot().size() == 1);
    REQUIRE (index.existsAsFile());

    // Second run: same index, a backend that must never be asked to scan.
    auto secondBackend = std::make_unique<FakeScannerBackend> (nc::ScannerReply {});
    auto* raw = secondBackend.get();

    nc::PluginCatalog cold ({}, std::move (secondBackend), index);
    cold.scanSync();

    CHECK (raw->scans == 0);
    CHECK (cold.snapshot().size() == 1);
    CHECK (cold.scanProgress() == 1.0f);
    CHECK (cold.lastScanStatus().containsIgnoreCase ("cached"));

    dir.deleteRecursively();
}

TEST_CASE ("a stale index falls back to a real scan", "[catalog]")
{
    const auto dir = freshIndexDir();
    const auto bundle = dir.getChildFile ("stale.vst3");
    REQUIRE (bundle.replaceWithText ("one"));
    const auto index = dir.getChildFile ("plugin-index.json");

    const auto description = makeDescription ("Stale", bundle.getFullPathName(), 502);
    REQUIRE (nc::pluginindex::save (index, { description }));

    // The bundle changed on disk after the index was written.
    REQUIRE (bundle.replaceWithText ("one, but upgraded"));

    nc::ScannerReply reply;
    reply.ok = true;
    reply.descriptionsXml = descriptionsXml ({ makeDescription ("Stale", bundle.getFullPathName(), 502) });

    auto backend = std::make_unique<FakeScannerBackend> (reply);
    auto* raw = backend.get();
    backend->bundles = { bundle.getFullPathName() };

    nc::PluginCatalog catalog ({}, std::move (backend), index);
    catalog.scanSync();

    CHECK (raw->scans == 1);
    CHECK_FALSE (catalog.lastScanStatus().containsIgnoreCase ("cached"));

    dir.deleteRecursively();
}

TEST_CASE ("a cancelled scan never writes a truncated index", "[catalog]")
{
    const auto dir = freshIndexDir();
    const auto index = dir.getChildFile ("plugin-index.json");

    nc::PluginCatalog* catalogPtr = nullptr;

    auto backend = std::make_unique<FakeScannerBackend> (nc::ScannerReply {});
    backend->bundles = { "a.vst3", "b.vst3", "c.vst3" };
    auto* raw = backend.get();
    backend->onScan = [&] (int)
    {
        // Cancel from inside the scan, so the loop stops with bundles left over.
        if (catalogPtr != nullptr)
            catalogPtr->cancelScan();

        return nc::ScannerReply {};
    };

    nc::PluginCatalog catalog ({}, std::move (backend), index);
    catalogPtr = &catalog;
    catalog.scanSync();

    // Two bundles were never scanned, so the list is partial and must not be
    // cached: a truncated index would be trusted by the next start.
    CHECK (raw->scans == 1);
    CHECK_FALSE (index.existsAsFile());

    dir.deleteRecursively();
}
