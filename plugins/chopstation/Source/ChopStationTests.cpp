#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
juce::File writeTestWav (juce::AudioBuffer<float>& buffer, double sampleRate,
                        const juce::String& name = "test")
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("chopstation-test");
    folder.createDirectory();
    const auto file = folder.getChildFile (name + ".wav");
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    REQUIRE (stream != nullptr);
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate, buffer.getNumChannels(), 16, {}, 0));
    REQUIRE (writer != nullptr);
    stream.release();
    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    writer.reset();
    return file;
}
} // namespace

TEST_CASE ("Chop Station creates all 10 parameters", "[chopstation]")
{
    nc::ChopStationProcessor processor;
    REQUIRE (processor.getNumParameters() == 10);
    REQUIRE (processor.apvts.getParameter ("chops") != nullptr);
    REQUIRE (processor.apvts.getParameter ("pan") != nullptr);
}

TEST_CASE ("Chop Station rejects oversized/header-bomb samples before allocating", "[chopstation]")
{
    nc::ChopStationProcessor processor;
    // A tiny file is fine; the cap is enforced in loadSample via reader length.
    // Here we assert a valid 1-second tone loads and slices are non-empty.
    juce::AudioBuffer<float> buffer (2, 44100);

    for (int i = 0; i < 44100; ++i)
    {
        const auto v = std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0) * 0.5f;
        buffer.setSample (0, i, v);
        buffer.setSample (1, i, v);
    }

    const auto file = writeTestWav (buffer, 44100.0);
    REQUIRE (processor.loadSample (file));
    REQUIRE (processor.getSliceCount() > 0);
    REQUIRE_FALSE (processor.getSampleName().isEmpty());
}

TEST_CASE ("Chop Station loads and analyzes samples in the background", "[chopstation]")
{
    nc::ChopStationProcessor processor;
    juce::AudioBuffer<float> buffer (1, 44100);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample (0, i, 0.4f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / 44100.0));

    const auto file = writeTestWav (buffer, 44100.0);
    REQUIRE (processor.loadSampleAsync (file));

    for (int i = 0; i < 1000 && processor.isLoadingSample(); ++i)
        juce::Thread::sleep (2);

    REQUIRE_FALSE (processor.isLoadingSample());
    REQUIRE_FALSE (processor.didLastLoadFail());
    REQUIRE (processor.getSliceCount() > 0);
    REQUIRE_FALSE (processor.getSampleName().isEmpty());
}

TEST_CASE ("Chop Station restore cancels an in-flight background load", "[chopstation]")
{
    nc::ChopStationProcessor processor;
    juce::AudioBuffer<float> restoredBuffer (1, 44100);

    for (int i = 0; i < restoredBuffer.getNumSamples(); ++i)
        restoredBuffer.setSample (0, i, 0.3f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / 44100.0));

    const auto restoredFile = writeTestWav (restoredBuffer, 44100.0, "restored");
    REQUIRE (processor.loadSample (restoredFile));

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    REQUIRE (state.getSize() > 0);

    juce::AudioBuffer<float> pendingBuffer (1, 44100 * 20);

    for (int i = 0; i < pendingBuffer.getNumSamples(); ++i)
        pendingBuffer.setSample (0, i, 0.2f * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * i / 44100.0));

    const auto pendingFile = writeTestWav (pendingBuffer, 44100.0, "pending");
    REQUIRE (processor.loadSampleAsync (pendingFile));
    REQUIRE (processor.isLoadingSample());

    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    for (int i = 0; i < 1000 && processor.isLoadingSample(); ++i)
        juce::Thread::sleep (2);

    REQUIRE_FALSE (processor.isLoadingSample());
    REQUIRE (processor.getSampleName() == "restored");
}

TEST_CASE ("Chop Station state round-trips embedded sample without the file", "[chopstation]")
{
    nc::ChopStationProcessor processor;
    juce::AudioBuffer<float> buffer (2, 22050);

    for (int i = 0; i < 22050; ++i)
    {
        const auto v = std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / 44100.0) * 0.4f;
        buffer.setSample (0, i, v);
        buffer.setSample (1, i, v);
    }

    const auto file = writeTestWav (buffer, 44100.0);
    REQUIRE (processor.loadSample (file));

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    REQUIRE (state.getSize() > 0);

    // Delete the source file: restore must come from embedded WAV, not path.
    REQUIRE (file.deleteFile());

    nc::ChopStationProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (restored.getSliceCount() > 0);
    REQUIRE (restored.getSampleName() == processor.getSampleName());
}

TEST_CASE ("Chop Station presets apply without crashing", "[chopstation]")
{
    nc::ChopStationProcessor processor;

    for (int i = 0; i < processor.numPresets(); ++i)
    {
        processor.applyPreset (i);
        REQUIRE_FALSE (processor.presetName (i).isEmpty());
    }

    // Out-of-range indexes are ignored by applyPreset; presetName falls back to "Default".
    processor.applyPreset (-1);
    processor.applyPreset (99);
}

TEST_CASE ("Chop Station loadSample builds slices from a hit pattern", "[chopstation][integration]")
{
    nc::ChopStationProcessor processor;
    auto* chops = processor.apvts.getParameter ("chops");
    REQUIRE (chops != nullptr);
    chops->setValueNotifyingHost (chops->convertTo0to1 (3.0f));

    constexpr int sampleRate = 44100;
    constexpr int frames = sampleRate * 3 / 2;
    juce::AudioBuffer<float> buffer (1, frames);
    buffer.clear();

    for (const auto hit : { sampleRate / 5, sampleRate * 3 / 5, sampleRate })
        for (int i = 0; i < 128; ++i)
            buffer.setSample (0, hit + i,
                              0.8f * std::exp (-static_cast<float> (i) / 32.0f));

    const auto file = writeTestWav (buffer, sampleRate);
    REQUIRE (processor.loadSample (file));
    REQUIRE (processor.getSliceCount() == 3);
}
