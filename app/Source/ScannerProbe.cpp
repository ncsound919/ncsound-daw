/*
    Manual gate tool for the child-process plugin scanner (PHASE-02-PLAN 12.2).

    The probe is the same executable as its own worker, exactly like the app:
    run it with a VST3 path to see what the coordinator/worker pair returns.

        NCScannerProbe.exe "C:\path\Plugin.vst3"
        NCScannerProbe.exe --timeout-ms 500 "C:\path\Plugin.vst3"

    Exit code is 0 only when every requested file scanned successfully.
*/

#include "PluginCatalog.h"
#include "PluginRegistry.h"
#include "PluginScanner.h"

#include <iostream>

namespace
{
class ScannerProbeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "NCScannerProbe"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        auto worker = std::make_unique<nc::PluginScannerSubprocess>();

        if (worker->initialiseFromCommandLine (commandLine, nc::scannerProcessUID))
        {
            scannerWorker = std::move (worker);
            return;
        }

        auto args = getCommandLineParameterArray();

        auto format = juce::String { "VST3" };
        auto timeoutMs = 10000;

        for (int i = 0; i < args.size();)
        {
            if (args[i] == "--format" && i + 1 < args.size())
            {
                format = args[i + 1];
                args.removeRange (i, 2);
            }
            else if (args[i] == "--timeout-ms" && i + 1 < args.size())
            {
                timeoutMs = args[i + 1].getIntValue();
                args.removeRange (i, 2);
            }
            else
            {
                ++i;
            }
        }

        if (args.contains ("--catalog"))
        {
            args.removeString ("--catalog");

            const juce::File registryFile { args.size() > 0 ? args[0]
                                                           : juce::String ("plugins/registry.json") };
            const auto registry = nc::loadRegistry (registryFile);

            nc::PluginCatalog catalog (registry.plugins);
            const auto scanned = catalog.scanSync();

            auto linked = 0;
            auto loadable = 0;

            for (const auto& entry : catalog.snapshot())
            {
                if (! entry.registryRow)
                    continue;

                const auto hasDescription = catalog.findDescription (entry.id).has_value();

                if (entry.scanned)
                    ++linked;

                if (hasDescription)
                    ++loadable;

                std::cout << "ROW " << entry.id.toStdString()
                          << " loadable=" << (hasDescription ? 1 : 0)
                          << " name=\"" << entry.name.toStdString() << "\"" << std::endl;
            }

            std::cout << "CATALOG registry=" << registry.plugins.size()
                      << " rows=" << catalog.snapshot().size()
                      << " linked=" << linked
                      << " loadable=" << loadable
                      << " extra=" << catalog.asPluginInfo().size()
                      << " scanResult=" << scanned
                      << " status=\"" << catalog.lastScanStatus().toStdString() << "\""
                      << std::endl;

            quit();
            return;
        }

        nc::ChildProcessScannerBackend backend (std::chrono::milliseconds { juce::jmax (1, timeoutMs) });

        auto failures = 0;

        for (const auto& path : args)
        {
            const auto started = juce::Time::getMillisecondCounterHiRes();
            const auto reply = backend.scan (format, path);
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - started;

            juce::StringArray names;

            if (auto xml = juce::XmlDocument::parse (reply.descriptionsXml))
                for (const auto* item : xml->getChildIterator())
                {
                    juce::PluginDescription description;

                    if (description.loadFromXml (*item))
                        names.add (description.name + " [" + description.pluginFormatName + "]");
                }

            std::cout << "SCAN " << path.toStdString()
                      << " ok=" << (reply.ok ? 1 : 0)
                      << " timedOut=" << (reply.timedOut ? 1 : 0)
                      << " lost=" << (reply.connectionLost ? 1 : 0)
                      << " count=" << names.size()
                      << " ms=" << juce::roundToInt (elapsed)
                      << " error=\"" << reply.error.toStdString() << "\""
                      << std::endl;

            for (const auto& name : names)
                std::cout << "  -> " << name.toStdString() << std::endl;

            if (! reply.ok)
                ++failures;
        }

        std::cout << "DONE files=" << args.size() << " failures=" << failures << std::endl;
        setApplicationReturnValue (failures == 0 ? 0 : 1);
        quit();
    }

    void shutdown() override
    {
        scannerWorker = nullptr;
    }

private:
    std::unique_ptr<nc::PluginScannerSubprocess> scannerWorker;
};
} // namespace

START_JUCE_APPLICATION (ScannerProbeApplication)
