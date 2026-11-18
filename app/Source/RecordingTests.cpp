/*
    Recording behaviour, driven through the same render path the device callback
    uses (AudioEngine::processBlockForTesting), so capture can be verified on a
    machine-independent signal instead of a live microphone.

    These mirror the rules the Ardour recorder follows:
      - capture is input-aligned and the head is trimmed by the reported latency,
      - only armed tracks record; unarmed tracks keep playing from disk,
      - each take is its own file,
      - a capture that cannot keep up is counted, never silently truncated.
*/

#include <catch2/catch_test_macros.hpp>

#include "AudioEngine.h"

#include <cmath>
#include <vector>

namespace
{
juce::File recordingFolder (const juce::String& name)
{
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("nc-record-" + name);
    folder.deleteRecursively();
    folder.createDirectory();
    return folder;
}

// The device opens asynchronously in the engine constructor.
bool waitForDevice (nc::AudioEngine& engine, int timeoutMs = 3000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);

    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (engine.currentSampleRate() > 0.0 && engine.isDeviceOpen())
            return true;

        juce::Thread::sleep (20);
    }

    return engine.currentSampleRate() > 0.0;
}

/** Feeds one block of the given signal into the engine's render path. */
void feedBlock (nc::AudioEngine& engine, const std::vector<float>& left,
                const std::vector<float>& right, float* outLeft, float* outRight)
{
    const float* inputs[2] { left.data(), right.data() };
    float* outputs[2] { outLeft, outRight };
    engine.processBlockForTesting (inputs, 2, outputs, 2, static_cast<int> (left.size()));
}

std::vector<float> readWav (const juce::File& file, int64_t& frames, double& rate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return {};

    frames = static_cast<int64_t> (reader->lengthInSamples);
    rate = reader->sampleRate;

    std::vector<float> data (static_cast<size_t> (frames));
    float* dest[1] { data.data() };
    reader->read (dest, 1, 0, static_cast<int> (frames));
    return data;
}
} // namespace

TEST_CASE ("an armed track captures what arrives at its input", "[record]")
{
    nc::AudioEngine engine;

    if (! waitForDevice (engine))
    {
        WARN ("No audio device on this machine; skipping recording capture test");
        return;
    }
    engine.suspendAudioCallbackForTesting();

    const auto folder = recordingFolder ("armed");
    const auto blockSize = 256;
    const auto blocks = 8;

    engine.setRecordArmed (0, true);
    REQUIRE (engine.isRecordArmed (0));

    juce::String error;
    REQUIRE (engine.startRecording (error));
    REQUIRE (engine.isRecording());
    // Arming a track also opens its input monitor for the take; the UI has no
    // separate monitor switch yet, so recording must not leave it silent.
    REQUIRE (engine.isTrackMonitor (0));

    // A ramp, so the take can be checked sample by sample rather than by length.
    std::vector<float> left (static_cast<size_t> (blockSize));
    std::vector<float> right (static_cast<size_t> (blockSize));
    std::vector<float> outLeft (static_cast<size_t> (blockSize));
    std::vector<float> outRight (static_cast<size_t> (blockSize));

    for (int block = 0; block < blocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto index = block * blockSize + i;
            left[static_cast<size_t> (i)] = static_cast<float> (index) * 0.0004f;
            right[static_cast<size_t> (i)] = -static_cast<float> (index) * 0.0004f;
        }

        feedBlock (engine, left, right, outLeft.data(), outRight.data());
    }

    REQUIRE (engine.recordedFrames() == static_cast<int64_t> (blocks * blockSize));
    REQUIRE (engine.recordOverflows() == 0);

    const auto takes = engine.stopRecording (folder, 24, error);

    REQUIRE (takes.size() == 1);
    REQUIRE_FALSE (engine.isRecording());
    REQUIRE_FALSE (engine.isTrackMonitor (0));
    REQUIRE (takes[0].track == 0);
    REQUIRE (takes[0].file.existsAsFile());

    int64_t frames = 0;
    double rate = 0.0;
    const auto samples = readWav (takes[0].file, frames, rate);

    REQUIRE (frames == static_cast<int64_t> (blocks * blockSize));
    REQUIRE (rate == engine.currentSampleRate());

    // First sample is the start of the capture, not silence pushed in by the mix.
    REQUIRE (std::abs (samples[0] - 0.0f) < 1.0e-4f);
    REQUIRE (std::abs (samples[100] - 0.04f) < 1.0e-3f);

    folder.deleteRecursively();
}

TEST_CASE ("a streamed take writes its audio to disk", "[record]")
{
    nc::AudioEngine engine;

    if (! waitForDevice (engine))
    {
        WARN ("No audio device on this machine; skipping streamed recording test");
        return;
    }
    engine.suspendAudioCallbackForTesting();

    const auto folder = recordingFolder ("streamed");
    const auto blockSize = 256;

    engine.setRecordArmed (0, true);

    juce::String error;
    REQUIRE (engine.startRecording (folder, error));

    std::vector<float> left (static_cast<size_t> (blockSize));
    std::vector<float> right (static_cast<size_t> (blockSize));
    std::vector<float> outLeft (static_cast<size_t> (blockSize));
    std::vector<float> outRight (static_cast<size_t> (blockSize));

    for (int i = 0; i < blockSize; ++i)
        left[static_cast<size_t> (i)] = static_cast<float> (i) * 0.001f;

    feedBlock (engine, left, right, outLeft.data(), outRight.data());
    feedBlock (engine, left, right, outLeft.data(), outRight.data());

    const auto takes = engine.stopRecording (folder, 24, error);
    REQUIRE (takes.size() == 1);
    REQUIRE (takes[0].frames == static_cast<int64_t> (2 * blockSize));
    REQUIRE (takes[0].file.existsAsFile());

    int64_t frames = 0;
    double rate = 0.0;
    const auto samples = readWav (takes[0].file, frames, rate);

    // The threaded writer must have flushed the queued blocks before the take
    // was returned; a header-only WAV is the regression this pins.
    REQUIRE (frames == static_cast<int64_t> (2 * blockSize));
    REQUIRE (rate == engine.currentSampleRate());
    REQUIRE (std::abs (samples[10] - 0.01f) < 1.0e-3f);

    folder.deleteRecursively();
}

TEST_CASE ("an unarmed track records nothing and unarmed start is refused", "[record]")
{
    nc::AudioEngine engine;

    if (! waitForDevice (engine))
    {
        WARN ("No audio device on this machine; skipping recording arming test");
        return;
    }
    engine.suspendAudioCallbackForTesting();

    juce::String error;
    REQUIRE_FALSE (engine.startRecording (error));
    REQUIRE (error.containsIgnoreCase ("arm"));
    REQUIRE_FALSE (engine.isRecording());

    engine.setRecordArmed (1, true);
    REQUIRE (engine.isRecordArmed (1));
    engine.setRecordArmed (1, false);
    REQUIRE_FALSE (engine.isRecordArmed (1));
}

TEST_CASE ("input latency is trimmed from the head of the take", "[record]")
{
    nc::AudioEngine engine;

    if (! waitForDevice (engine))
    {
        WARN ("No audio device on this machine; skipping recording latency test");
        return;
    }
    engine.suspendAudioCallbackForTesting();

    const auto folder = recordingFolder ("latency");
    const auto blockSize = 512;

    engine.setRecordArmed (0, true);
    engine.setRecordLatencySamples (128);
    REQUIRE (engine.recordLatencySamples() == 128);

    juce::String error;
    REQUIRE (engine.startRecording (error));

    std::vector<float> left (static_cast<size_t> (blockSize));
    std::vector<float> right (static_cast<size_t> (blockSize));
    std::vector<float> outLeft (static_cast<size_t> (blockSize));
    std::vector<float> outRight (static_cast<size_t> (blockSize));

    for (int i = 0; i < blockSize; ++i)
        left[static_cast<size_t> (i)] = static_cast<float> (i) / 1024.0f;

    feedBlock (engine, left, right, outLeft.data(), outRight.data());

    const auto takes = engine.stopRecording (folder, 24, error);
    REQUIRE (takes.size() == 1);

    // 512 captured, 128 dropped as round-trip latency.
    REQUIRE (takes[0].frames == static_cast<int64_t> (blockSize - 128));

    int64_t frames = 0;
    double rate = 0.0;
    const auto samples = readWav (takes[0].file, frames, rate);

    REQUIRE (frames == static_cast<int64_t> (blockSize - 128));
    REQUIRE (std::abs (samples[0] - 128.0f / 1024.0f) < 1.0e-5f);   // starts after the trimmed head
    REQUIRE (std::abs (samples[1] - 129.0f / 1024.0f) < 1.0e-5f);

    folder.deleteRecursively();
}

TEST_CASE ("two armed tracks produce two takes with their own content", "[record]")
{
    nc::AudioEngine engine;

    if (! waitForDevice (engine))
    {
        WARN ("No audio device on this machine; skipping multitrack recording test");
        return;
    }
    engine.suspendAudioCallbackForTesting();

    const auto folder = recordingFolder ("multitrack");
    const auto blockSize = 256;

    engine.setRecordArmed (0, true);
    engine.setRecordArmed (2, true);

    juce::String error;
    REQUIRE (engine.startRecording (error));

    std::vector<float> left (static_cast<size_t> (blockSize), 0.25f);
    std::vector<float> right (static_cast<size_t> (blockSize), -0.5f);
    std::vector<float> outLeft (static_cast<size_t> (blockSize));
    std::vector<float> outRight (static_cast<size_t> (blockSize));

    feedBlock (engine, left, right, outLeft.data(), outRight.data());

    const auto takes = engine.stopRecording (folder, 24, error);

    REQUIRE (takes.size() == 2);
    REQUIRE (takes[0].track == 0);
    REQUIRE (takes[1].track == 2);
    REQUIRE (takes[0].file != takes[1].file);

    int64_t frames = 0;
    double rate = 0.0;

    const auto first = readWav (takes[0].file, frames, rate);
    REQUIRE (frames == static_cast<int64_t> (blockSize));
    REQUIRE (std::abs (first[0] - 0.25f) < 1.0e-4f);

    // Track 2 is armed second, so it takes the second input channel pair.
    const auto second = readWav (takes[1].file, frames, rate);
    REQUIRE (frames == static_cast<int64_t> (blockSize));
    REQUIRE (std::abs (second[0] - (-0.5f)) < 1.0e-4f);

    folder.deleteRecursively();
}
