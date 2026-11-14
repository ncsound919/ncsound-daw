/*
    Voice-to-MIDI regression coverage. The transcription path reads a file
    through juce::AudioFormatReader::read, whose AudioBuffer overload returns
    a bool - an earlier draft compared that bool against the requested frame
    count, which failed every file longer than one frame ("Could not read the
    full audio file"). This suite keeps the success path pinned.
*/

#include <catch2/catch_test_macros.hpp>

#include "AudioEngine.h"

#include <cmath>

namespace
{
juce::File voiceTestFolder (const char* name)
{
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("nc-voice-" + juce::String (name));
    folder.deleteRecursively();
    folder.createDirectory();
    return folder;
}

/** A clean sine, so YIN has an unambiguous f0 to report. */
juce::File writeSineWav (const juce::File& folder, double sampleRate,
                         double frequency, double seconds)
{
    const auto frames = (int) (sampleRate * seconds);
    juce::AudioBuffer<float> buffer (1, frames);

    for (int i = 0; i < frames; ++i)
        buffer.setSample (0, i, 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                          * frequency * i / sampleRate));

    juce::File file (folder.getChildFile ("sine.wav"));
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, 1, 16, {}, 0));

    if (writer == nullptr)
        return file;

    // The writer owns the stream once created; release the local handle so
    // scope exit does not double-delete it.
    stream.release();

    writer->writeFromAudioSampleBuffer (buffer, 0, frames);
    writer.reset();
    return file;
}
} // namespace

TEST_CASE ("transcription reads a real wav and reports a note", "[voicetomidi]")
{
    nc::AudioEngine engine;
    const auto folder = voiceTestFolder ("sine");
    const auto file = writeSineWav (folder, 48000.0, 440.0, 0.5);

    juce::String error;
    const auto clip = engine.voiceToMidiClip (file, 0, 120.0, &error);

    // The regression: an empty clip plus a "Could not read" error was the
    // signature of the bool-vs-frames comparison bug.
    REQUIRE (error.isEmpty());
    REQUIRE (clip.isMidi);
    REQUIRE (clip.track == 0);
    REQUIRE_FALSE (clip.notes.empty());

    // A440 must land on MIDI note 69 (±1 semitone for analysis drift).
    const auto pitch = clip.notes.front().pitch;
    REQUIRE (pitch >= 68);
    REQUIRE (pitch <= 70);
}

TEST_CASE ("transcription of a missing file reports why, without throwing")
{
    nc::AudioEngine engine;
    const auto folder = voiceTestFolder ("missing");
    const auto file = folder.getChildFile ("not-here.wav");

    juce::String error;
    const auto clip = engine.voiceToMidiClip (file, 0, 120.0, &error);

    REQUIRE (error.isNotEmpty());
    REQUIRE (clip.isMidi);
    REQUIRE (clip.notes.empty());
}
