#include "AudioEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>

namespace
{
juce::File voiceToneWav (const juce::File& folder, const juce::String& name,
                         double sampleRate, double frequency, int channels, double seconds)
{
    const auto frames = static_cast<int> (std::llround (sampleRate * seconds));
    juce::AudioBuffer<float> buffer (channels, frames);

    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < frames; ++i)
            buffer.setSample (ch, i, 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                               * frequency * i / sampleRate));

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

TEST_CASE ("AudioEngine voice-to-MIDI reports a missing WAV", "[voicetomidi][integration]")
{
    nc::AudioEngine engine;
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("nc-voice-integration")
                          .getChildFile ("not-here.wav");

    const auto clip = engine.voiceToMidiClip (file, 0, 120.0);

    REQUIRE (engine.lastVoiceError().isNotEmpty());
    REQUIRE (clip.notes.empty());
}