#include "AudioEngine.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
juce::File voiceToneWav (const juce::File& folder, const juce::String& name,
                         double sampleRate, double frequency, int channels, double seconds,
                         double vibratoCents = 0.0, double vibratoHz = 5.5)
{
    const auto frames = static_cast<int> (std::llround (sampleRate * seconds));
    juce::AudioBuffer<float> buffer (channels, frames);
    double phase = 0.0;

    for (int i = 0; i < frames; ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        const auto bend = vibratoCents == 0.0
                              ? 1.0
                              : std::pow (2.0, vibratoCents
                                               * std::sin (2.0 * juce::MathConstants<double>::pi
                                                           * vibratoHz * t) / 1200.0);
        phase += 2.0 * juce::MathConstants<double>::pi * frequency * bend / sampleRate;

        for (int ch = 0; ch < channels; ++ch)
            buffer.setSample (ch, i, 0.4f * (float) std::sin (phase));
    }

    const auto file = folder.getChildFile (name);
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    REQUIRE (stream != nullptr);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, channels, 16, {}, 0));
    REQUIRE (writer != nullptr);
    stream.release();
    writer->writeFromAudioSampleBuffer (buffer, 0, frames);
    writer.reset();
    return file;
}
} // namespace

TEST_CASE ("AudioEngine voice-to-MIDI reads a mono WAV", "[voicetomidi][integration]")
{
    nc::AudioEngine engine;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("nc-voice-integration");
    folder.deleteRecursively();
    REQUIRE (folder.createDirectory().wasOk());

    const auto file = voiceToneWav (folder, "a4.wav", 48000.0, 440.0, 1, 0.6);
    const auto clip = engine.voiceToMidiClip (file, 2, 120.0);

    REQUIRE (engine.lastVoiceError().isEmpty());
    REQUIRE (clip.isMidi);
    REQUIRE (clip.track == 2);
    REQUIRE_FALSE (clip.notes.empty());
    REQUIRE (clip.notes.front().pitch == 69);
    REQUIRE (clip.notes.front().startBeat >= 0.0);
    REQUIRE (clip.notes.front().lengthBeats > 0.0);
}

TEST_CASE ("AudioEngine voice-to-MIDI mixes a stereo WAV", "[voicetomidi][integration]")
{
    nc::AudioEngine engine;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("nc-voice-integration");
    REQUIRE (folder.createDirectory().wasOk());

    const auto file = voiceToneWav (folder, "c5-stereo.wav", 48000.0, 523.251, 2, 0.6);
    const auto clip = engine.voiceToMidiClip (file, 0, 120.0);

    REQUIRE (engine.lastVoiceError().isEmpty());
    REQUIRE (clip.isMidi);
    REQUIRE_FALSE (clip.notes.empty());
    REQUIRE (clip.notes.front().pitch == 72);
}

TEST_CASE ("AudioEngine voice-to-MIDI carries pitch bends", "[voicetomidi][integration]")
{
    nc::AudioEngine engine;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("nc-voice-integration");
    REQUIRE (folder.createDirectory().wasOk());

    const auto file = voiceToneWav (folder, "vibrato.wav", 48000.0, 440.0, 1, 0.8, 50.0);
    const auto clip = engine.voiceToMidiClip (file, 1, 120.0);

    REQUIRE (engine.lastVoiceError().isEmpty());
    REQUIRE (clip.isMidi);
    REQUIRE_FALSE (clip.notes.empty());
    REQUIRE_FALSE (clip.bends.empty());

    auto lowest = 8192;
    auto highest = 8192;

    for (const auto& bend : clip.bends)
    {
        REQUIRE (bend.channel == 1);
        REQUIRE (bend.beat >= 0.0);
        lowest = std::min (lowest, bend.bend);
        highest = std::max (highest, bend.bend);
    }

    REQUIRE (lowest < 8192);
    REQUIRE (highest > 8192);
}

TEST_CASE ("AudioEngine voice-to-MIDI reports a missing WAV", "[voicetomidi][integration]")
{
    nc::AudioEngine engine;
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("nc-voice-integration")
                          .getChildFile ("not-here.wav");
    file.deleteFile();

    const auto clip = engine.voiceToMidiClip (file, 0, 120.0);

    REQUIRE (engine.lastVoiceError().isNotEmpty());
    REQUIRE (clip.notes.empty());
}