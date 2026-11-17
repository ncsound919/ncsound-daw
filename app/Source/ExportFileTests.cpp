#include "AudioEngine.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

TEST_CASE ("a mixdown writes a readable stereo WAV", "[export]")
{
    nc::AudioEngine engine;
    engine.setActiveTrackCount (2);

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("nc-export-test");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto wav = dir.getChildFile ("mix.wav");

    juce::String error;
    REQUIRE (engine.renderToWav (wav, 1.0, 3.0, 24, error)); // bars 1-3, 24-bit

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));
    REQUIRE (reader != nullptr);
    CHECK (reader->numChannels == 2);
    CHECK (reader->lengthInSamples > 48000);

    dir.deleteRecursively();
}

TEST_CASE ("stem export writes one file per active track", "[export]")
{
    nc::AudioEngine engine;
    engine.setActiveTrackCount (3);

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("nc-stems-test");
    dir.deleteRecursively();
    dir.createDirectory();

    nc::engine::export_::ExportOptions options; // defaults: WAV, no normalisation
    std::vector<juce::File> written;
    juce::String error;
    const auto count = engine.exportStems (dir, 1.0, 3.0, options, written, error);

    CHECK (count == 3);
    CHECK (written.size() == 3);
    CHECK (dir.findChildFiles (juce::File::findFiles, false, "*.wav").size() == 3);
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))
        CHECK (f.getSize() > 44); // more than a bare RIFF header

    dir.deleteRecursively();
}
