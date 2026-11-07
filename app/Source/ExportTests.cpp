#include "AudioEngine.h"

#include <nc/engine/ClipTimeline.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
juce::File tempWav (const juce::String& name)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    file.deleteFile();
    return file;
}

/** Puts the generated demo asset on track 0 for `bars` bars from bar 1. The
    engine's constructor only builds assets; the app is what schedules clips. */
bool scheduleDemoClip (nc::AudioEngine& engine, double bars)
{
    const auto asset = engine.demoAssetId (0);

    if (asset < 0)
        return false;

    nc::AudioEngine::TimelineClip clip;
    clip.assetId = asset;
    clip.track = 0;
    clip.startBar = 1.0;
    clip.lengthBars = bars;
    clip.sourceOffsetSeconds = 0.0;
    clip.gainDb = 0.0f;
    clip.fadeInBars = 0.0f;
    clip.fadeOutBars = 0.0f;

    std::vector<nc::AudioEngine::TimelineClip> clips { clip };
    return engine.setTimelineClips (std::move (clips)) == 0;
}

struct Rendered
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;
    int channels = 0;

    bool isReadable() const { return channels > 0 && audio.getNumSamples() > 0; }
};

Rendered readWav (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    Rendered result;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return result;

    result.sampleRate = reader->sampleRate;
    result.channels = static_cast<int> (reader->numChannels);
    result.audio.setSize (result.channels, static_cast<int> (reader->lengthInSamples));
    reader->read (&result.audio, 0, result.audio.getNumSamples(), 0, true, true);

    return result;
}
} // namespace

TEST_CASE ("a mixdown covers the requested range at the engine rate", "[export]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);
    engine.setMetronome (true);   // the click must not print

    REQUIRE (scheduleDemoClip (engine, 2.0));
    REQUIRE (engine.contentEndBar() == Catch::Approx (3.0));

    const auto rate = engine.currentSampleRate();
    REQUIRE (rate > 0.0);

    // The engine opens its audio device asynchronously and that start-up raises
    // a one-shot transport reset (audioDeviceAboutToStart -> resetRequested),
    // so let it land before placing the playhead we then check.
    // The device opens asynchronously and asks for a transport reset when it
    // does. Detach it so the position this test sets is the one export sees.
    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    // A transport position the render must leave exactly as it found it.
    engine.setPositionSeconds (2.0);
    const auto positionBefore = engine.positionSeconds();

    const auto file = tempWav ("nc-mixdown-range.wav");
    juce::String error;

    REQUIRE (engine.renderToWav (file, 1.0, 3.0, 24, error));
    INFO ("renderToWav said: " << error);
    REQUIRE (file.existsAsFile());
    REQUIRE (file.getSize() > 64);

    const auto rendered = readWav (file);
    REQUIRE (rendered.isReadable());
    REQUIRE (rendered.channels == 2);
    REQUIRE (rendered.sampleRate == rate);

    const auto expectedFrames = static_cast<juce::int64> (std::llround (
        (nc::engine::barToSeconds (3.0, 120.0) - nc::engine::barToSeconds (1.0, 120.0)) * rate));

    REQUIRE (static_cast<juce::int64> (rendered.audio.getNumSamples()) == expectedFrames);

    // A scheduled demo clip is audible, so an all-silent file would mean the
    // offline path never reached the mix.
    REQUIRE (rendered.audio.getMagnitude (0, rendered.audio.getNumSamples()) > 0.001f);

    REQUIRE (engine.positionSeconds() == Catch::Approx (positionBefore).margin (0.5 / rate));
    REQUIRE (engine.isMetronomeOn());
}

TEST_CASE ("a second mixdown of the same range is the same length and is still audible", "[export]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);
    REQUIRE (scheduleDemoClip (engine, 1.0));

    juce::String error;
    const auto first = tempWav ("nc-mixdown-1.wav");
    const auto second = tempWav ("nc-mixdown-2.wav");

    REQUIRE (engine.renderToWav (first, 1.0, 2.0, 16, error));
    REQUIRE (engine.renderToWav (second, 1.0, 2.0, 16, error));

    const auto a = readWav (first);
    const auto b = readWav (second);

    REQUIRE (a.isReadable());
    REQUIRE (b.isReadable());
    REQUIRE (a.audio.getNumSamples() == b.audio.getNumSamples());
    REQUIRE (b.audio.getMagnitude (0, b.audio.getNumSamples()) > 0.001f);
}

TEST_CASE ("an empty export range is refused and writes no file", "[export]")
{
    nc::AudioEngine engine;
    const auto file = tempWav ("nc-mixdown-empty.wav");

    juce::String error;
    REQUIRE_FALSE (engine.renderToWav (file, 3.0, 3.0, 24, error));
    REQUIRE (error.isNotEmpty());
    REQUIRE_FALSE (file.existsAsFile());
}
