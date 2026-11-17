#include "PluginCatalog.h"

#include "PluginIndexCache.h"

#include <algorithm>
#include <set>

namespace nc
{
PluginCatalog::PluginCatalog (std::vector<PluginInfo> registryToUse,
                              std::unique_ptr<ScannerBackend> scannerToUse,
                              juce::File indexFileToUse)
    : juce::Thread ("NCSound plugin scanner"),
      registryEntries (std::move (registryToUse)),
      indexFile (std::move (indexFileToUse)),
      scanner (std::move (scannerToUse)),
      alive (std::make_shared<std::atomic<bool>> (true))
{
    if (scanner == nullptr)
        scanner = std::make_unique<ChildProcessScannerBackend>();

    juce::addDefaultFormatsToManager (formats);
    mergeScannedDescriptions ({});
}

PluginCatalog::~PluginCatalog()
{
    alive->store (false, std::memory_order_release);
    cancelRequested.store (true, std::memory_order_release);

    if (scanner != nullptr)
        scanner->cancel();

    stopThread (2000);
}

std::vector<CatalogEntry> PluginCatalog::snapshot() const
{
    return entries;
}

std::optional<CatalogEntry> PluginCatalog::find (const juce::String& id) const
{
    for (const auto& entry : entries)
        if (entry.id == id)
            return entry;

    return std::nullopt;
}

std::optional<juce::PluginDescription> PluginCatalog::findDescription (const juce::String& id) const
{
    for (const auto& entry : entries)
        if (entry.scanned && entry.external && entry.id == id)
            return entry.description;

    return std::nullopt;
}

bool PluginCatalog::isMissing (const CatalogEntry& entry) const
{
    return entry.external
        && ! entry.scanned
        && scanCompleted.load (std::memory_order_acquire)
        && ! isScanning();
}

std::vector<PluginInfo> PluginCatalog::asPluginInfo() const
{
    std::vector<PluginInfo> result;
    result.reserve (entries.size());

    for (const auto& entry : entries)
    {
        if (! entry.scanned || ! entry.external || entry.registryRow)
            continue;

        PluginInfo info;
        info.id = entry.id;
        info.name = entry.name;
        info.kind = entry.isInstrument ? "instrument" : "effect";
        info.source = "VST3";
        info.note = entry.manufacturer;
        info.status = PluginStatus::external;
        result.push_back (std::move (info));
    }

    return result;
}

void PluginCatalog::scanAsync (std::function<void (bool, juce::String)> finished)
{
    if (scanning.exchange (true, std::memory_order_acq_rel))
        return;

    cancelRequested.store (false, std::memory_order_release);
    progress.store (0.0f, std::memory_order_relaxed);
    finishedCallback = std::move (finished);

    {
        const juce::ScopedLock lock (statusLock);
        statusText = "Scanning VST3 folders...";
    }

    startThread();
}

int PluginCatalog::scanSync()
{
    if (scanning.exchange (true, std::memory_order_acq_rel))
        return static_cast<int> (entries.size());

    cancelRequested.store (false, std::memory_order_release);
    progress.store (0.0f, std::memory_order_relaxed);
    auto result = scanAll();
    mergeScannedDescriptions (std::move (result.descriptions));

    // A cancelled scan has not checked the whole disk, so it must not turn
    // rows Missing: only a complete pass may make that claim.
    if (! cancelRequested.load (std::memory_order_acquire))
        scanCompleted.store (true, std::memory_order_release);

    // Progress first: isScanning() is what a poller watches, so the final
    // reading must already be visible when it turns false.
    progress.store (1.0f, std::memory_order_relaxed);
    scanning.store (false, std::memory_order_release);

    {
        const juce::ScopedLock lock (statusLock);
        statusText = result.cached
                         ? "cached (" + juce::String (static_cast<int> (entries.size())) + " plug-ins)"
                         : (result.error.isNotEmpty()
                                ? result.error
                                : juce::String (static_cast<int> (entries.size())) + " plug-ins indexed");
    }

    return static_cast<int> (entries.size());
}

void PluginCatalog::cancelScan()
{
    cancelRequested.store (true, std::memory_order_release);
}

juce::String PluginCatalog::lastScanStatus() const
{
    const juce::ScopedLock lock (statusLock);
    return statusText;
}

bool PluginCatalog::appendDescriptionsFromReply (const ScannerReply& reply,
                                                 std::vector<juce::PluginDescription>& destination,
                                                 juce::String& error)
{
    if (! reply.ok)
    {
        error = reply.timedOut ? "A plugin scan timed out and was killed"
              : reply.connectionLost ? "A plugin scanner worker crashed"
              : reply.error.isNotEmpty() ? reply.error
              : "The plugin scan failed";
        return false;
    }

    const auto xml = juce::XmlDocument::parse (reply.descriptionsXml);

    if (xml == nullptr)
    {
        error = "The scanner returned an unreadable response";
        return false;
    }

    for (const auto* item : xml->getChildIterator())
    {
        juce::PluginDescription description;

        if (description.loadFromXml (*item))
            destination.push_back (description);
    }

    return true;
}

void PluginCatalog::createAsync (const juce::PluginDescription& description,
                                 double sampleRate, int blockSize,
                                 std::function<void (std::unique_ptr<juce::AudioPluginInstance>,
                                                     juce::String)> callback)
{
    auto lifetime = alive;
    formats.createPluginInstanceAsync (description, sampleRate, blockSize,
                                       [lifetime, callback = std::move (callback)]
                                       (std::unique_ptr<juce::AudioPluginInstance> instance,
                                        const juce::String& error)
                                       {
                                           if (! lifetime->load (std::memory_order_acquire))
                                               return;

                                           if (callback)
                                               callback (std::move (instance), error);
                                       });
}

std::unique_ptr<juce::AudioPluginInstance> PluginCatalog::createSync (const juce::PluginDescription& description,
                                                                      double sampleRate,
                                                                      int blockSize,
                                                                      juce::String& error)
{
    return formats.createPluginInstance (description, sampleRate, blockSize, error);
}

void PluginCatalog::run()
{
    auto result = scanAll();

    auto lifetime = alive;
    auto callback = finishedCallback;

    juce::MessageManager::callAsync ([this, lifetime, callback,
                                      result = std::move (result)]() mutable
    {
        if (! lifetime->load (std::memory_order_acquire))
            return;

        const auto cancelled = cancelRequested.load (std::memory_order_acquire);
        mergeScannedDescriptions (std::move (result.descriptions));

        if (! cancelled)
            scanCompleted.store (true, std::memory_order_release);

        progress.store (1.0f, std::memory_order_relaxed);
        scanning.store (false, std::memory_order_release);

        {
            const juce::ScopedLock lock (statusLock);
            statusText = cancelled
                             ? "Scan cancelled"
                             : (result.cached
                                    ? "cached (" + juce::String (static_cast<int> (entries.size()))
                                          + " plug-ins)"
                                    : (result.error.isNotEmpty()
                                           ? result.error
                                           : juce::String (static_cast<int> (entries.size()))
                                                 + " plug-ins indexed"));
        }

        if (callback)
            callback (! cancelled && result.error.isEmpty(), statusText);

        finishedCallback = {};
    });
}

PluginCatalog::ScanResult PluginCatalog::scanAll()
{
    ScanResult result;

    // A warm index that still matches every bundle on disk means no worker is
    // launched at all: that is the whole point of the cache, so it is tried
    // before anything touches a bundle.
    if (indexFile != juce::File() && scanner->injectedBundles().empty())
    {
        auto cached = nc::pluginindex::loadDescriptions (indexFile);

        if (! cached.empty())
        {
            result.descriptions = std::move (cached);
            result.cached = true;
            progress.store (1.0f, std::memory_order_relaxed);

            std::sort (result.descriptions.begin(), result.descriptions.end(),
                       [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
            return result;
        }
    }
    juce::AudioPluginFormatManager scanFormats;
    juce::addDefaultFormatsToManager (scanFormats);

    // Two passes: enumerate first, scan second. The enumeration is what makes
    // progress mean something -- a percentage of bundles *found*, not of
    // plug-ins discovered, so a folder of three bundles that yields one plug-in
    // still reports 33%, 66%, 100% instead of a bar stuck near zero.
    struct Candidate
    {
        juce::String formatName;
        juce::String path;
    };

    std::vector<Candidate> candidates;

    const auto injected = scanner->injectedBundles();

    if (! injected.empty())
    {
        for (const auto& path : injected)
            candidates.push_back ({ "VST3", path });
    }
    else
    {
        std::set<juce::String> seen;

        for (auto* format : scanFormats.getFormats())
        {
            if (cancelRequested.load (std::memory_order_acquire))
                break;

            const auto locations = scanLocationsFor (*format);

            for (int directoryIndex = 0; directoryIndex < static_cast<int> (locations.size()); ++directoryIndex)
            {
                if (cancelRequested.load (std::memory_order_acquire))
                    break;

                const auto directory = locations[static_cast<size_t> (directoryIndex)];

                if (! directory.isDirectory())
                    continue;

                for (const auto& entry : juce::RangedDirectoryIterator (directory, true, "*",
                                                                         juce::File::findFiles))
                {
                    if (cancelRequested.load (std::memory_order_acquire))
                        break;

                    const auto file = entry.getFile();

                    if (! format->fileMightContainThisPluginType (file.getFullPathName()))
                        continue;

                    if (! seen.insert (file.getFullPathName()).second)
                        continue;

                    candidates.push_back ({ format->getName(), file.getFullPathName() });

                    if (static_cast<int> (candidates.size()) >= 512)
                        break;
                }
            }
        }
    }

    const auto total = std::max (1, static_cast<int> (candidates.size()));
    progress.store (0.0f, std::memory_order_relaxed);

    for (int index = 0; index < static_cast<int> (candidates.size()); ++index)
    {
        if (cancelRequested.load (std::memory_order_acquire))
            break;

        const auto& candidate = candidates[static_cast<size_t> (index)];
        const auto reply = scanner->scan (candidate.formatName, candidate.path);
        juce::String replyError;

        if (! PluginCatalog::appendDescriptionsFromReply (reply, result.descriptions, replyError))
        {
            ++result.failedFiles;
            result.error = replyError;
        }

        // A timed-out or failed bundle still consumed its slot: the bar keeps
        // moving so one broken plug-in cannot freeze the scan at its percent.
        progress.store (juce::jmin (1.0f, static_cast<float> (index + 1) / static_cast<float> (total)),
                        std::memory_order_relaxed);
    }

    if (result.failedFiles > 0 && result.descriptions.empty())
        result.error = juce::String (result.failedFiles) + " VST3 files could not be scanned";

    std::sort (result.descriptions.begin(), result.descriptions.end(),
               [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });

    // Only a complete scan is worth caching: a cancelled scan holds a partial
    // list, and writing it would make the next start trust a truncated index.
    if (indexFile != juce::File() && ! cancelRequested.load (std::memory_order_acquire))
        nc::pluginindex::save (indexFile, result.descriptions);

    return result;
}

void PluginCatalog::setAdditionalSearchRoots (std::vector<juce::File> roots)
{
    std::vector<juce::File> usable;

    for (auto& root : roots)
    {
        if (! root.isDirectory())
            continue;

        const auto canonical = root.getFullPathName();

        if (std::none_of (usable.begin(), usable.end(),
                          [&canonical] (const juce::File& f) { return f.getFullPathName() == canonical; }))
            usable.push_back (root);
    }

    const juce::ScopedLock lock (rootsLock);
    extraRoots = std::move (usable);
}

std::vector<juce::File> PluginCatalog::additionalSearchRoots() const
{
    const juce::ScopedLock lock (rootsLock);
    return extraRoots;
}

std::vector<juce::File> PluginCatalog::scanLocationsFor (juce::AudioPluginFormat& format) const
{
    std::vector<juce::File> directories;
    std::set<juce::String> seen;

    const auto locations = format.getDefaultLocationsToSearch();

    for (int i = 0; i < locations.getNumPaths(); ++i)
        if (locations[i].isDirectory() && seen.insert (locations[i].getFullPathName()).second)
            directories.push_back (locations[i]);

    // Extra roots come last so a bundle installed in the standard location wins
    // when the same plug-in exists in more than one folder.
    for (const auto& root : additionalSearchRoots())
        if (root.isDirectory() && seen.insert (root.getFullPathName()).second)
            directories.push_back (root);

    return directories;
}

void PluginCatalog::setRegistryEntries (std::vector<PluginInfo> entriesToUse)
{
    registryEntries = std::move (entriesToUse);
    mergeScannedDescriptions (std::move (scannedDescriptions));
}

void PluginCatalog::mergeScannedDescriptions (std::vector<juce::PluginDescription> descriptions)
{
    scannedDescriptions = std::move (descriptions);
    entries.clear();

    for (const auto& info : registryEntries)
    {
        CatalogEntry entry;
        entry.id = info.id;
        entry.name = info.name;
        entry.category = info.kind;
        entry.status = info.status;
        entry.external = info.status == PluginStatus::external;
        entry.registryRow = true;
        entry.vst3BundleName = info.vst3BundleName;
        entry.isInstrument = info.isInstrument();
        entries.push_back (std::move (entry));
    }

    // A registry row owns the scanned description whose product name it names,
    // so the suite appears once under its stable id instead of twice.
    std::set<juce::String> claimed;

    for (const auto& description : scannedDescriptions)
    {
        const auto match = std::find_if (entries.begin(), entries.end(),
                                         [&description] (const CatalogEntry& entry)
                                         {
                                             return entry.registryRow && ! entry.scanned
                                                 && entry.vst3BundleName.isNotEmpty()
                                                 && entry.vst3BundleName.equalsIgnoreCase (description.name);
                                         });

        if (match == entries.end())
            continue;

        match->scanned = true;
        match->description = description;
        match->isInstrument = description.isInstrument;

        if (description.manufacturerName.isNotEmpty())
            match->manufacturer = description.manufacturerName;

        claimed.insert (description.createIdentifierString());
    }

    std::set<juce::String> seenIds;

    for (const auto& description : scannedDescriptions)
    {
        const auto id = description.createIdentifierString();

        if (claimed.count (id) > 0 || ! seenIds.insert (id).second)
            continue;

        CatalogEntry entry;
        entry.id = id;
        entry.name = description.name;
        entry.manufacturer = description.manufacturerName;
        entry.category = description.category;
        entry.status = PluginStatus::external;
        entry.external = true;
        entry.scanned = true;
        entry.isInstrument = description.isInstrument;
        entry.description = description;
        entries.push_back (std::move (entry));
    }

    std::sort (entries.begin(), entries.end(),
               [] (const auto& a, const auto& b)
               {
                   return a.name.compareIgnoreCase (b.name) < 0;
               });
}

const PluginInfo* PluginCatalog::registryInfoFor (const juce::String& id) const
{
    for (const auto& info : registryEntries)
        if (info.id == id)
            return &info;

    return nullptr;
}

juce::File PluginCatalog::findInstalledBundle (const juce::String& bundleName) const
{
    if (bundleName.isEmpty())
        return {};

    const auto contentsSegment = juce::File::getSeparatorString() + "Contents"
                               + juce::File::getSeparatorString();

    for (auto* format : formats.getFormats())
    {
        if (format->getName() != "VST3")
            continue;

        const auto locations = scanLocationsFor (*format);

        for (size_t i = 0; i < locations.size(); ++i)
        {
            const auto directory = locations[i];

            if (! directory.isDirectory())
                continue;

            for (const auto& entry : juce::RangedDirectoryIterator (directory, true, "*.vst3",
                                                                   juce::File::findFilesAndDirectories))
            {
                const auto file = entry.getFile();

                if (file.getFullPathName().containsIgnoreCase (contentsSegment))
                    continue;

                if (file.getFileNameWithoutExtension().equalsIgnoreCase (bundleName))
                    return file;
            }
        }
    }

    return {};
}

std::optional<juce::PluginDescription> PluginCatalog::resolveDescription (const juce::String& id,
                                                                          juce::String& error)
{
    if (const auto found = findDescription (id))
        return found;

    const auto* info = registryInfoFor (id);

    if (info == nullptr || info->vst3BundleName.isEmpty())
    {
        error = "That plugin is not in the scanned VST3 catalog";
        return std::nullopt;
    }

    const auto bundle = findInstalledBundle (info->vst3BundleName);

    if (! bundle.exists())
    {
        error = info->name + " is not installed (no " + info->vst3BundleName + ".vst3 bundle found)";
        return std::nullopt;
    }

    const auto reply = scanner->scan ("VST3", bundle.getFullPathName());

    if (! reply.ok)
    {
        error = reply.error.isNotEmpty() ? reply.error : "The plugin scan failed";
        return std::nullopt;
    }

    std::vector<juce::PluginDescription> descriptions;
    juce::String parseError;

    if (! appendDescriptionsFromReply (reply, descriptions, parseError) || descriptions.empty())
    {
        error = parseError.isNotEmpty() ? parseError
                                        : info->name + " reported no loadable audio class";
        return std::nullopt;
    }

    auto merged = scannedDescriptions;

    for (const auto& description : descriptions)
        merged.push_back (description);

    mergeScannedDescriptions (std::move (merged));

    if (const auto found = findDescription (id))
        return found;

    error = info->name + " did not report the product name " + info->vst3BundleName;
    return std::nullopt;
}
} // namespace nc
