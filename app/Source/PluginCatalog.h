#pragma once

#include "PluginRegistry.h"
#include "PluginScanner.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace nc
{
/** One row the browser can display. Registry entries and scanned VST3s share
    this shape; only external rows can be instantiated in Phase 2. */
struct CatalogEntry
{
    juce::String id;
    juce::String name;
    juce::String manufacturer;
    juce::String category;
    PluginStatus status = PluginStatus::external;
    bool external = false;
    /** True only for rows produced by the scanner, which are the only rows that
        carry a real PluginDescription and can be instantiated. */
    bool scanned = false;
    /** True for a plugins/registry.json row, which owns the display identity. */
    bool registryRow = false;
    /** Product name the registry expects the installed VST3 to report. */
    juce::String vst3BundleName;
    bool isInstrument = false;
    juce::PluginDescription description;
};

class PluginCatalog final : private juce::Thread
{
public:
    explicit PluginCatalog (std::vector<PluginInfo> registryEntries = {},
                            std::unique_ptr<ScannerBackend> scannerToUse = {});
    ~PluginCatalog() override;

    std::vector<CatalogEntry> snapshot() const;
    std::optional<CatalogEntry> find (const juce::String& id) const;
    std::optional<juce::PluginDescription> findDescription (const juce::String& id) const;

    /** Resolves an id for loading. When the folder scan has not run yet, this
        scans the single installed bundle the registry names, then links it. */
    std::optional<juce::PluginDescription> resolveDescription (const juce::String& id,
                                                               juce::String& error);

    std::vector<PluginInfo> asPluginInfo() const;

    void scanAsync (std::function<void (bool, juce::String)> finished = {});
    int scanSync();
    void cancelScan();
    bool isScanning() const { return scanning.load (std::memory_order_acquire); }
    float scanProgress() const { return progress.load (std::memory_order_relaxed); }
    juce::String lastScanStatus() const;

    /** Interprets one worker reply. Exposed so tests can cover timeout, crash
        and unreadable-response handling without a real VST3 or a child process. */
    static bool appendDescriptionsFromReply (const ScannerReply& reply,
                                             std::vector<juce::PluginDescription>& destination,
                                             juce::String& error);

    /** Replaces the scanned rows, keeping the registry rows above them. */
    void mergeScannedDescriptions (std::vector<juce::PluginDescription>);

    /** Seeds the registry rows the app shows, then relinks any scanned rows. */
    void setRegistryEntries (std::vector<PluginInfo>);

    /** Folders to search in addition to the format's default VST3 locations.
        Local builds live outside %CommonProgramFiles%, so without this a folder
        of .vst3 bundles on disk is invisible to both the scan and the load path.
        Non-directories are dropped; duplicates are removed when scanning. */
    void setAdditionalSearchRoots (std::vector<juce::File> roots);
    std::vector<juce::File> additionalSearchRoots() const;

    void createAsync (const juce::PluginDescription&,
                      double sampleRate, int blockSize,
                      std::function<void (std::unique_ptr<juce::AudioPluginInstance>,
                                          juce::String)> callback);

    std::unique_ptr<juce::AudioPluginInstance> createSync (const juce::PluginDescription&,
                                                           double sampleRate,
                                                           int blockSize,
                                                           juce::String& error);

private:
    void run() override;

    struct ScanResult
    {
        std::vector<juce::PluginDescription> descriptions;
        juce::String error;
        int failedFiles = 0;
    };

    ScanResult scanAll();
    juce::File findInstalledBundle (const juce::String& bundleName) const;
    const PluginInfo* registryInfoFor (const juce::String& id) const;
    // Non-const format: getDefaultLocationsToSearch() is not const in this JUCE.
    std::vector<juce::File> scanLocationsFor (juce::AudioPluginFormat&) const;

    std::vector<PluginInfo> registryEntries;
    std::vector<juce::File> extraRoots;
    mutable juce::CriticalSection rootsLock;
    std::vector<juce::PluginDescription> scannedDescriptions;
    std::vector<CatalogEntry> entries;
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<ScannerBackend> scanner;
    std::shared_ptr<std::atomic<bool>> alive;
    std::function<void (bool, juce::String)> finishedCallback;

    std::atomic<bool> scanning { false };
    std::atomic<bool> cancelRequested { false };
    std::atomic<float> progress { 0.0f };
    mutable juce::CriticalSection statusLock;
    juce::String statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginCatalog)
};
} // namespace nc
