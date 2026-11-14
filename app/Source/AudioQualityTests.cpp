#include "AudioEngine.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
juce::File qualityOutput (const juce::String& name)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    file.deleteFile();
    return file;
}

/** Lets the device-open reset land, then detaches the callback so the export is
    the only thing rendering. Same dance the export tests use. */
void detachDevice (nc::AudioEngine& engine)
{
    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();
}
} // namespace

// Hidden by the leading dot: these are measurement fixtures, not assertions.
// Run them by hand with NC_AUDIO_DUMP set, then analyse the pre/post captures
// with scripts/compare-dump.mjs.

TEST_CASE ("quality capture: the shipped demo session at unity gain", "[.quality-demo]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);

    std::vector<nc::AudioEngine::TimelineClip> clips;

    for (int track = 0; track < nc::AudioEngine::demoTrackCount; ++track)
    {
        const auto asset = engine.demoAssetId (track);

        if (asset < 0)
            continue;

        nc::AudioEngine::TimelineClip clip;
        clip.assetId = asset;
        clip.track = track;
        clip.startBar = 1.0;
        clip.lengthBars = 2.0;
        clip.gainDb = 0.0f;
        clips.push_back (clip);
    }

    REQUIRE (clips.size() == static_cast<size_t> (nc::AudioEngine::demoTrackCount));
    REQUIRE (engine.setTimelineClips (std::move (clips)) == 0);

    detachDevice (engine);

    juce::String error;
    const auto file = qualityOutput ("nc-quality-demo.wav");
    REQUIRE (engine.renderToWav (file, 1.0, 3.0, 24, error));
    INFO ("renderToWav said: " << error);
}

TEST_CASE ("quality capture: the stock saw instrument, held chord", "[.quality-synth]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);
    engine.setBuiltinInstrument (0, 2); // Wave::saw

    nc::AudioEngine::MidiTimelineClip clip;
    clip.track = 0;
    clip.startBar = 1.0;
    clip.lengthBars = 2.0;
    clip.name = "QUALITY SYNTH";
    // A held C4 major triad: a steady spectrum makes aliasing measurable.
    clip.notes = { { 0.0, 7.0, 60, 100, 0 },
                   { 0.0, 7.0, 64, 100, 0 },
                   { 0.0, 7.0, 67, 100, 0 } };

    engine.setMidiClips ({ clip });

    detachDevice (engine);

    juce::String error;
    const auto file = qualityOutput ("nc-quality-synth.wav");
    REQUIRE (engine.renderToWav (file, 1.0, 3.0, 24, error));
    INFO ("renderToWav said: " << error);
}
