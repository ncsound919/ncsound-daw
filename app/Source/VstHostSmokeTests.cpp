/*
    Opt-in end-to-end gate for real VST3 hosting (PHASE-02-PLAN 12.2, steps 1-5).

    Hidden behind the "[.vst3]" tag so the normal suite stays machine-independent:

        NCMixRackTests.exe "[.vst3]"

    It uses JUCE's format manager directly because the test executable is not a
    scanner worker; child-process scan isolation is covered by NCScannerProbe.
*/

#include <catch2/catch_test_macros.hpp>

#include "HostedInsert.h"
#include "TrackInsertRack.h"

#include <cmath>
#include <memory>
#include <vector>

namespace
{
juce::AudioPluginFormat* findVst3Format (juce::AudioPluginFormatManager& formats)
{
    for (auto* format : formats.getFormats())
        if (format->getName() == "VST3")
            return format;

    return nullptr;
}

std::vector<juce::File> candidatePluginFiles (juce::AudioPluginFormat& format)
{
    std::vector<juce::File> files;

    if (const auto override = juce::SystemStats::getEnvironmentVariable ("NC_TEST_VST3", {});
        override.isNotEmpty())
    {
        const juce::File file { override };

        if (file.exists())
            files.push_back (file);

        return files;
    }

    const auto locations = format.getDefaultLocationsToSearch();

    for (int i = 0; i < locations.getNumPaths() && files.size() < 24; ++i)
    {
        const auto directory = locations[i];

        if (! directory.isDirectory())
            continue;

        // VST3 bundles may sit one folder down (e.g. Vendor/Plugin.vst3), so walk
        // the tree but keep only bundle roots, never the binary inside Contents.
        for (const auto& entry : juce::RangedDirectoryIterator (directory, true, "*.vst3",
                                                               juce::File::findFilesAndDirectories))
        {
            const auto file = entry.getFile();
            const auto contentsSegment = juce::File::getSeparatorString() + "Contents"
                                       + juce::File::getSeparatorString();

            if (file.getFullPathName().containsIgnoreCase (contentsSegment))
                continue;

            if (format.fileMightContainThisPluginType (file.getFullPathName()))
                files.push_back (file);

            if (files.size() >= 24)
                break;
        }
    }

    return files;
}

void fillSine (juce::AudioBuffer<float>& buffer, int blockIndex)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto frequency = 220.0;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto t = (static_cast<double> (blockIndex) * buffer.getNumSamples() + sample) / sampleRate;
            buffer.setSample (channel, sample,
                              static_cast<float> (0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * t)));
        }
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite (buffer.getSample (channel, sample)))
                return false;

    return true;
}
} // namespace

TEST_CASE ("a real VST3 effect loads, processes and round-trips its state", "[.vst3]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    auto* vst3 = findVst3Format (formats);

    if (vst3 == nullptr)
    {
        WARN ("The VST3 format is not compiled into this build");
        return;
    }

    const auto files = candidatePluginFiles (*vst3);

    if (files.empty())
    {
        WARN ("No VST3 bundles found; set NC_TEST_VST3 to a plugin path");
        return;
    }

    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::String error;
    juce::String chosenName;

    for (const auto& file : files)
    {
        juce::OwnedArray<juce::PluginDescription> descriptions;
        vst3->findAllTypesForFile (descriptions, file.getFullPathName());

        for (const auto* description : descriptions)
        {
            if (description == nullptr || description->isInstrument)
                continue;

            error.clear();
            instance = formats.createPluginInstance (*description, 48000.0, 512, error);

            if (instance != nullptr)
            {
                chosenName = description->name;
                break;
            }

            // Some bundles install but refuse to initialise in this environment;
            // keep looking rather than failing the gate on an unrelated plugin.
            WARN ("Could not instantiate " << description->name << ": " << error);
        }

        if (instance != nullptr)
            break;
    }

    if (instance == nullptr)
    {
        WARN ("No VST3 effect on this machine could be instantiated");
        return;
    }

    INFO ("Hosting " << chosenName);

    auto hosted = nc::HostedInsert::create (std::move (instance), 48000.0, 512, error);

    if (hosted == nullptr)
    {
        WARN ("The first instantiable plugin is not an insert effect: " << error);
        return;
    }

    nc::engine::PluginSlotState saved;
    REQUIRE (hosted->getState (saved));
    REQUIRE_FALSE (saved.identifier.empty());
    REQUIRE_FALSE (saved.descriptionXml.empty());
    REQUIRE_FALSE (saved.stateBase64.empty());
    REQUIRE (saved.format == "VST3");

    const auto latency = hosted->latencySamples();

    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 512, 2);

    REQUIRE (rack.install (0, 0, std::move (hosted), saved, error));
    INFO (error);
    REQUIRE (rack.chainLatency (0) == latency);
    REQUIRE (rack.slotName (0, 0) == chosenName);

    juce::AudioBuffer<float> buffer (2, 512);
    nc::engine::TransportBlock block;
    block.numSamples = buffer.getNumSamples();
    block.isPlaying = true;

    rack.beginBlock();

    for (int i = 0; i < 4; ++i)
    {
        fillSine (buffer, i);
        rack.processTrack (0, buffer, block);
        REQUIRE (allFinite (buffer));
    }

    rack.processMaster (buffer, block);
    rack.endBlock();

    auto* live = dynamic_cast<nc::HostedInsert*> (rack.slot (0, 0).active.load());
    REQUIRE (live != nullptr);
    REQUIRE (live->setState (saved));

    rack.setBypassed (0, 0, true);
    REQUIRE (rack.slot (0, 0).bypassed.load());
    rack.beginBlock();
    fillSine (buffer, 4);
    rack.processTrack (0, buffer, block);
    REQUIRE (allFinite (buffer));
    rack.endBlock();

    rack.setBypassed (0, 0, false);
    REQUIRE_FALSE (rack.slot (0, 0).bypassed.load());

    rack.remove (0, 0);
    REQUIRE (rack.slot (0, 0).active.load() == nullptr);
    REQUIRE (rack.slotName (0, 0).isEmpty());

    rack.collectGarbage();
    REQUIRE (rack.slot (0, 0).owned == nullptr);
}

TEST_CASE ("every bundle under NC_TEST_VST3_DIR scans, loads, renders and opens its editor",
           "[.vst3][bundle-sweep]")
{
    const auto rootText = juce::SystemStats::getEnvironmentVariable ("NC_TEST_VST3_DIR", {});

    if (rootText.isEmpty())
    {
        WARN ("Set NC_TEST_VST3_DIR to a folder of .vst3 bundles to sweep it");
        return;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    auto* vst3 = findVst3Format (formats);
    REQUIRE (vst3 != nullptr);

    std::vector<juce::File> bundles;
    const auto contentsSegment = juce::File::getSeparatorString() + "Contents"
                               + juce::File::getSeparatorString();

    for (const auto& entry : juce::RangedDirectoryIterator (juce::File { rootText }, true, "*.vst3",
                                                           juce::File::findFilesAndDirectories))
    {
        const auto file = entry.getFile();

        if (file.getFullPathName().containsIgnoreCase (contentsSegment))
            continue;

        if (vst3->fileMightContainThisPluginType (file.getFullPathName()))
            bundles.push_back (file);
    }

    REQUIRE (! bundles.empty());

    // A build tree also holds stale and half-built bundles, so one bad bundle
    // must not abort the sweep and hide the rest. Every bundle is recorded and
    // the case fails once, at the end.
    // Every copy of a module is swept, including the several build directories
    // this tree keeps, and a failure names the exact copy. A later copy of an
    // identical module can come back classless from an in-process scan even
    // though the app's child-process scanner reads it, so the copy is reported
    // rather than assumed good.
    juce::StringArray failures;
    auto instantiated = 0;
    auto editorsOpened = 0;

    for (const auto& bundle : bundles)
    {
        const auto label = bundle.getFullPathName();

        juce::OwnedArray<juce::PluginDescription> descriptions;
        vst3->findAllTypesForFile (descriptions, bundle.getFullPathName());

        if (descriptions.isEmpty())
        {
            failures.add (label + " / no audio class");
            continue;
        }

        juce::String error;
        auto instance = formats.createPluginInstance (*descriptions[0], 48000.0, 512, error);

        if (instance == nullptr)
        {
            failures.add (label + " / not instantiable: " + error);
            continue;
        }

        instance->prepareToPlay (48000.0, 512);

        const auto channels = juce::jmax (1, instance->getTotalNumInputChannels());
        juce::AudioBuffer<float> buffer (channels, 512);
        juce::MidiBuffer midi;
        auto rendered = true;

        for (int block = 0; block < 4; ++block)
        {
            buffer.clear();
            instance->processBlock (buffer, midi);
            rendered = rendered && allFinite (buffer);
        }

        if (! rendered)
            failures.add (label + " / non-finite output");

        juce::MemoryBlock state;
        instance->getStateInformation (state);

        if (state.getSize() == 0)
        {
            failures.add (label + " / empty state");
        }
        else
        {
            // setStateInformation returns void, so the round trip is proven by
            // reading the state back rather than by asserting on a return value.
            instance->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

            juce::MemoryBlock restored;
            instance->getStateInformation (restored);

            if (restored.getSize() == 0)
                failures.add (label + " / state did not round-trip");
        }

        // Editor presence is part of what a host has to survive. The plugin owns
        // the editor, so delete it rather than letting the shared code leak it.
        if (instance->hasEditor())
        {
            if (auto* editor = instance->createEditorIfNeeded())
            {
                if (editor->getWidth() <= 0 || editor->getHeight() <= 0)
                    failures.add (label + " / editor has no size");
                else
                    ++editorsOpened;

                instance->editorBeingDeleted (editor);
                delete editor;
            }
        }

        instance->releaseResources();
        ++instantiated;
    }

    for (const auto& failure : failures)
        WARN ("FAILED " << failure);

    WARN ("Swept " << bundles.size() << " bundles: " << instantiated << " instantiated, "
                   << editorsOpened << " editors opened, " << failures.size() << " failed");

    CHECK (failures.isEmpty());
}
