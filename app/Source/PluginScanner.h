#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <chrono>
#include <memory>
#include <vector>

namespace nc
{
inline constexpr const char* scannerProcessUID = "nc-sound-plugin-scanner";

struct ScannerReply
{
    bool ok = false;
    bool timedOut = false;
    bool connectionLost = false;
    juce::String descriptionsXml;
    juce::String error;
};

/** A scan request boundary. Production uses a child process; tests can inject
    a fake backend without launching a second executable. */
class ScannerBackend
{
public:
    virtual ~ScannerBackend() = default;

    virtual ScannerReply scan (const juce::String& formatName,
                               const juce::String& fileOrIdentifier) = 0;
    virtual void cancel() = 0;

    /** Test seam: a non-empty list makes the catalog scan exactly these paths
        instead of discovering bundles on disk, so scan-order tests stay
        hermetic and never touch the user's installed plug-ins. Production
        returns {} and discovery runs as normal. */
    virtual std::vector<juce::String> injectedBundles() const { return {}; }
};

/** Coordinator-side scanner. It launches the current executable in worker mode,
    sends one file at a time, and enforces a total deadline. */
class ChildProcessScannerBackend final : public ScannerBackend
{
public:
    explicit ChildProcessScannerBackend (std::chrono::milliseconds timeout = std::chrono::milliseconds { 10000 });
    ~ChildProcessScannerBackend() override;

    ScannerReply scan (const juce::String& formatName,
                       const juce::String& fileOrIdentifier) override;
    void cancel() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
    std::chrono::milliseconds timeout;
};

/** Worker-side scanner. Main.cpp constructs this before the window and calls
    initialiseFromCommandLine(); if it returns true, the process is a scanner. */
class PluginScannerSubprocess final : private juce::ChildProcessWorker
{
public:
    PluginScannerSubprocess();
    ~PluginScannerSubprocess() override = default;

    using juce::ChildProcessWorker::initialiseFromCommandLine;

private:
    void handleMessageFromCoordinator (const juce::MemoryBlock&) override;
    void handleConnectionLost() override;

    juce::AudioPluginFormatManager formats;
};
} // namespace nc
