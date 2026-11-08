#include "AudioEngine.h"

#include <nc/engine/ClipTimeline.hpp>
#include <nc/engine/Export.hpp>

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

TEST_CASE ("variant export writes FLAC and reports LUFS loudness", "[export]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);
    REQUIRE (scheduleDemoClip (engine, 2.0));

    nc::engine::export_::ExportOptions options;
    options.format = nc::engine::export_::Format::flac;
    options.bitDepth = 24;
    options.dither = true;
    options.sampleRate = static_cast<int> (engine.currentSampleRate());

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("nc-mixdown.flac");
    file.deleteFile();

    juce::String error;
    REQUIRE (engine.renderToFile (file, 1.0, 3.0, options, error));
    REQUIRE (file.existsAsFile());
    REQUIRE (file.getSize() > 64);

    const auto rendered = readWav (file);
    REQUIRE (rendered.isReadable());
    REQUIRE (rendered.channels == 2);

    const auto loudness = engine.lastExportLoudness();
    REQUIRE_FALSE (loudness.silent);
    REQUIRE (loudness.durationSeconds > 0.0);
}

TEST_CASE ("stems export writes one file per active track", "[export]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);
    REQUIRE (scheduleDemoClip (engine, 1.0));

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("nc-stems");
    folder.deleteRecursively();

    nc::engine::export_::ExportOptions options;
    options.format = nc::engine::export_::Format::wav;
    options.bitDepth = 24;
    options.dither = true;

    std::vector<juce::File> written;
    juce::String error;
    const auto count = engine.exportStems (folder, 1.0, 2.0, options, written, error);

    REQUIRE (count == nc::AudioEngine::demoTrackCount);
    REQUIRE (written.size() == static_cast<size_t> (nc::AudioEngine::demoTrackCount));

    for (const auto& stem : written)
        REQUIRE (stem.existsAsFile());
}

TEST_CASE ("export restores the session sample rate it borrowed", "[export]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);
    REQUIRE (scheduleDemoClip (engine, 1.0));

    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    const auto sessionRate = engine.currentSampleRate();
    REQUIRE (sessionRate > 0.0);

    nc::engine::export_::ExportOptions options;
    options.format = nc::engine::export_::Format::wav;
    options.bitDepth = 24;
    options.sampleRate = sessionRate > 44100.0 ? 44100 : 48000;

    const auto file = tempWav ("nc-mixdown-rate-restore.wav");
    juce::String error;
    REQUIRE (engine.renderToFile (file, 1.0, 1.5, options, error));
    INFO ("renderToFile said: " << error);

    const auto rendered = readWav (file);
    REQUIRE (rendered.isReadable());
    REQUIRE (rendered.sampleRate == options.sampleRate);

    // prepareForRender() rewrites the engine rate; a 44.1 kHz export must not
    // leave a 48 kHz session running its racks and transport at 44.1.
    REQUIRE (engine.currentSampleRate() == sessionRate);
}

TEST_CASE ("export range follows the tempo map", "[export]")
{
    nc::AudioEngine engine;

    nc::engine::TempoMap map;
    map.points.push_back ({ 0.0, 120.0 });
    map.points.push_back ({ 4.0, 60.0 }); // bar 2 drops to 60 BPM
    engine.setTempoMap (map);

    REQUIRE (scheduleDemoClip (engine, 2.0));

    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    nc::engine::export_::ExportOptions options;
    options.sampleRate = static_cast<int> (engine.currentSampleRate());

    const auto file = tempWav ("nc-mixdown-tempo-map.wav");
    juce::String error;
    REQUIRE (engine.renderToFile (file, 1.0, 3.0, options, error));
    INFO ("renderToFile said: " << error);

    const auto rendered = readWav (file);
    REQUIRE (rendered.isReadable());

    // 0..4 PPQ at 120 = 2 s, 4..8 PPQ at 60 = 4 s. A constant-120 export would
    // write 4 s and cut the second bar off.
    const auto expected = nc::engine::ppqToSeconds (map, 8.0) * rendered.sampleRate;
    REQUIRE (static_cast<double> (rendered.audio.getNumSamples()) == Catch::Approx (expected).margin (2.0));
}

TEST_CASE ("stems ignore an unrelated solo and restore it", "[export]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);
    REQUIRE (scheduleDemoClip (engine, 1.0));

    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    // Solo a track that has no material. Mute is ignored for soloed tracks, so
    // the stem pass must clear the solo flags or every stem goes silent.
    engine.setTrackSolo (1, true);

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("nc-stems-solo");
    folder.deleteRecursively();

    nc::engine::export_::ExportOptions options;
    options.format = nc::engine::export_::Format::wav;
    options.bitDepth = 24;

    std::vector<juce::File> written;
    juce::String error;
    const auto count = engine.exportStems (folder, 1.0, 2.0, options, written, error);

    REQUIRE (count == nc::AudioEngine::demoTrackCount);
    REQUIRE (written.size() == static_cast<size_t> (count));

    const auto track0 = readWav (written.front());
    REQUIRE (track0.isReadable());
    REQUIRE (track0.audio.getMagnitude (0, track0.audio.getNumSamples()) > 0.001f);

    REQUIRE (engine.trackSolo (1));
}

TEST_CASE ("LUFS normalisation can boost when the true-peak ceiling allows it", "[export]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);
    REQUIRE (scheduleDemoClip (engine, 1.0));
    engine.setTrackGain (0, 0.05f); // ~ -26 dBFS: plenty of headroom

    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    juce::String error;

    nc::engine::export_::ExportOptions quiet;
    quiet.sampleRate = static_cast<int> (engine.currentSampleRate());
    REQUIRE (engine.renderToFile (tempWav ("nc-quiet.wav"), 1.0, 2.0, quiet, error));
    const auto quietLoudness = engine.lastExportLoudness();
    REQUIRE_FALSE (quietLoudness.silent);

    auto normalised = quiet;
    normalised.normalizeToTarget = true;
    normalised.lufsTarget = -14.0;
    normalised.truePeakCeilingDb = -1.0;

    REQUIRE (engine.renderToFile (tempWav ("nc-normalised.wav"), 1.0, 2.0, normalised, error));
    INFO ("renderToFile said: " << error);

    const auto loud = engine.lastExportLoudness();
    REQUIRE_FALSE (loud.silent);

    // gainDbForTruePeak() returns <= 0, so clamping with it made every boost a
    // no-op; the ceiling has to be applied as a maximum boost instead. This
    // material's crest factor is wide enough that the ceiling, not -14 LUFS,
    // is what stops the boost, so that is the value to pin.
    REQUIRE (loud.integratedLufs > quietLoudness.integratedLufs + 6.0);

    const auto ceilingLimited = quietLoudness.integratedLufs
                                + (-1.0 - quietLoudness.truePeakDbTp);
    REQUIRE (loud.integratedLufs == Catch::Approx (juce::jmin (-14.0, ceilingLimited)).margin (0.5));
    REQUIRE (loud.truePeakDbTp <= -1.0 + 0.3);
}

TEST_CASE ("a failed export clears the previous loudness report", "[export]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);
    REQUIRE (scheduleDemoClip (engine, 1.0));

    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    juce::String error;
    REQUIRE (engine.renderToWav (tempWav ("nc-ok.wav"), 1.0, 2.0, 24, error));
    REQUIRE_FALSE (engine.lastExportLoudness().silent);

    // Destination is an existing directory: createOutputStream fails for
    // every writer, so the render must report failure and clear LUFS
    // rather than leave the previous file's numbers readable.
    const auto blocked = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("nc-export-blocked");
    blocked.deleteRecursively();
    REQUIRE (blocked.createDirectory());

    REQUIRE_FALSE (engine.renderToWav (blocked, 1.0, 2.0, 24, error));
    REQUIRE_FALSE (error.isEmpty());
    REQUIRE (engine.lastExportLoudness().frames == 0);
    REQUIRE (engine.lastExportLoudness().silent);
}
