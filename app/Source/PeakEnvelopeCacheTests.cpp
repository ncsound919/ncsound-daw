/*  Pins the DAW-04 fix: a second state push must be served from the clip
    waveform cache, not from another scan of the audio.

    Every route reply rebuilds state, and buildState() asks for one 96-bin
    envelope per audio clip through AudioEngine::clipWaveform(). This test
    drives that exact entry point twice and reads the engine's hit/miss
    counters, so the assertion is about the scan not happening rather than
    about two results merely matching.
*/

#include <catch2/catch_test_macros.hpp>

#include "AudioEngine.h"

#include <cmath>

namespace
{
juce::File writeSineWav (const juce::String& name, double rate, double hz, double seconds,
                         float amplitude)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    file.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

    if (stream == nullptr)
        return {};

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), rate, 1, 24, {}, 0));

    if (writer == nullptr)
        return {};

    stream.release();

    const auto frames = static_cast<int> (std::lround (rate * seconds));
    juce::AudioBuffer<float> buffer (1, frames);
    auto* data = buffer.getWritePointer (0);

    for (int i = 0; i < frames; ++i)
        data[i] = amplitude * static_cast<float> (
            std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));

    const auto ok = writer->writeFromAudioSampleBuffer (buffer, 0, frames);
    writer.reset();

    return ok ? file : juce::File();
}
} // namespace

TEST_CASE ("a second state push reuses the cached clip waveform instead of rescanning the audio",
           "[peaks]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);

    const auto file = writeSineWav ("nc-peaks-cache.wav", 48000.0, 220.0, 2.0, 0.25f);
    REQUIRE (file.existsAsFile());

    const auto asset = engine.loadAudioFile (file);
    REQUIRE (asset >= 0);

    // The same arguments buildState() uses for an audio clip: the bars the clip
    // covers, measured from the clip's own start, in 96 bins.
    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());
    const auto hitsBefore = engine.peakEnvelopeCacheHits();
    const auto missesBefore = engine.peakEnvelopeCacheMisses();

    const auto firstPush = engine.clipWaveform (asset, 0.0, 2, secondsPerBar);
    const auto secondPush = engine.clipWaveform (asset, 0.0, 2, secondsPerBar);

    REQUIRE (firstPush.size() == 96);
    REQUIRE (secondPush == firstPush);

    // One scan for two pushes: the first missed, the second was a hit. A rescan
    // would advance the miss counter again, which is what this test exists to
    // catch, and a broken cache would leave the hit counter where it started.
    REQUIRE (engine.peakEnvelopeCacheMisses() == missesBefore + 1);
    REQUIRE (engine.peakEnvelopeCacheHits() == hitsBefore + 1);
}
