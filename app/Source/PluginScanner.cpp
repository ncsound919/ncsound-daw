#include "PluginScanner.h"

#include <condition_variable>
#include <mutex>

namespace nc
{
namespace
{
#if JUCE_DEBUG
// Debug-only manual gates (see PHASE-02-PLAN section 12.2). The app inherits
// these variables, so they reach both the coordinator and the worker.
std::chrono::milliseconds debugTimeoutOverride (std::chrono::milliseconds requested)
{
    const auto text = juce::SystemStats::getEnvironmentVariable ("NC_SCANNER_TIMEOUT_MS", {});
    const auto ms = text.getIntValue();

    return text.isNotEmpty() && ms > 0 ? std::chrono::milliseconds { ms } : requested;
}
#endif

class ScannerSuperprocess final : private juce::ChildProcessCoordinator
{
public:
    ScannerSuperprocess()
    {
        launched = launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                                        scannerProcessUID, 0, 0);
    }

    ScannerReply request (const juce::String& formatName,
                          const juce::String& fileOrIdentifier,
                          std::chrono::milliseconds timeout)
    {
        ScannerReply reply;

        if (! launched)
        {
            reply.error = "The scanner worker did not start";
            return reply;
        }

        juce::MemoryBlock requestBlock;
        juce::MemoryOutputStream stream (requestBlock, false);
        stream.writeString (formatName);
        stream.writeString (fileOrIdentifier);

        {
            const std::lock_guard<std::mutex> lock (mutex);
            done = false;
            lost = false;
            responseXml.clear();
        }

        if (! sendMessageToWorker (requestBlock))
        {
            reply.error = "The scanner worker is not connected";
            reply.connectionLost = true;
            return reply;
        }

        std::unique_lock<std::mutex> lock (mutex);

        if (! condition.wait_for (lock, timeout, [this] { return done; }))
        {
            lock.unlock();
            killWorkerProcess();
            reply.timedOut = true;
            reply.error = "Plugin scan timed out";
            return reply;
        }

        reply.connectionLost = lost;
        reply.descriptionsXml = responseXml;
        reply.ok = ! lost;

        if (lost)
            reply.error = "The scanner worker crashed or disconnected";

        return reply;
    }

    using juce::ChildProcessCoordinator::sendMessageToWorker;

private:
    void handleMessageFromWorker (const juce::MemoryBlock& message) override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        responseXml = message.toString();
        done = true;
        condition.notify_one();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        lost = true;
        done = true;
        condition.notify_one();
    }

    std::mutex mutex;
    std::condition_variable condition;
    juce::String responseXml;
    bool done = false;
    bool lost = false;
    bool launched = false;
};
} // namespace

class ChildProcessScannerBackend::Impl
{
public:
    explicit Impl (std::chrono::milliseconds deadline) : timeout (deadline) {}

    ScannerReply scan (const juce::String& formatName, const juce::String& file)
    {
        if (process == nullptr)
            process = std::make_unique<ScannerSuperprocess>();

        auto reply = process->request (formatName, file, timeout);

        if (reply.timedOut || reply.connectionLost)
            process.reset();

        return reply;
    }

    void cancel()
    {
        process.reset();
    }

    std::chrono::milliseconds timeout;
    std::unique_ptr<ScannerSuperprocess> process;
};

ChildProcessScannerBackend::ChildProcessScannerBackend (std::chrono::milliseconds deadline)
#if JUCE_DEBUG
    : impl (std::make_unique<Impl> (debugTimeoutOverride (deadline))),
      timeout (debugTimeoutOverride (deadline))
#else
    : impl (std::make_unique<Impl> (deadline)),
      timeout (deadline)
#endif
{
}

ChildProcessScannerBackend::~ChildProcessScannerBackend() = default;

ScannerReply ChildProcessScannerBackend::scan (const juce::String& formatName,
                                               const juce::String& fileOrIdentifier)
{
    return impl->scan (formatName, fileOrIdentifier);
}

void ChildProcessScannerBackend::cancel()
{
    impl->cancel();
}

PluginScannerSubprocess::PluginScannerSubprocess()
{
    juce::addDefaultFormatsToManager (formats);
}

void PluginScannerSubprocess::handleMessageFromCoordinator (const juce::MemoryBlock& message)
{
    juce::MemoryInputStream stream (message, false);
    const auto formatName = stream.readString();
    const auto identifier = stream.readString();

#if JUCE_DEBUG
    // Debug-only: lets the manual gate prove the coordinator kills a hung worker.
    // Set NC_SCANNER_TEST_HANG_FILE to hang for one matching file only, so the
    // scan after the kill can be shown to recover.
    {
        const auto hangMs = juce::SystemStats::getEnvironmentVariable ("NC_SCANNER_TEST_HANG_MS", {}).getIntValue();
        const auto hangFile = juce::SystemStats::getEnvironmentVariable ("NC_SCANNER_TEST_HANG_FILE", {});

        if (hangMs > 0 && (hangFile.isEmpty() || identifier.containsIgnoreCase (hangFile)))
            juce::Thread::sleep (hangMs);
    }
#endif

    juce::XmlElement result ("SCAN");
    juce::AudioPluginFormat* matchingFormat = nullptr;

    for (auto* format : formats.getFormats())
        if (format->getName() == formatName)
            matchingFormat = format;

    if (matchingFormat != nullptr)
    {
        juce::OwnedArray<juce::PluginDescription> descriptions;
        matchingFormat->findAllTypesForFile (descriptions, identifier);

        for (const auto* description : descriptions)
            if (description != nullptr)
                if (auto xml = description->createXml())
                    result.addChildElement (xml.release());
    }

    const auto text = result.toString();
    juce::MemoryBlock response (text.toRawUTF8(), text.getNumBytesAsUTF8());
    sendMessageToCoordinator (response);
}

void PluginScannerSubprocess::handleConnectionLost()
{
    juce::JUCEApplicationBase::quit();
}
} // namespace nc
