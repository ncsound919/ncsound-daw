#include <catch2/catch_test_macros.hpp>

#include "AudioEngine.h"
#include "MockAudioInterface.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <vector>

namespace
{
juce::File getMockTestFolder (const juce::String& subfolder)
{
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("nc-mock-interface-" + subfolder);
    folder.deleteRecursively();
    folder.createDirectory();
    return folder;
}

struct MockAudioFixture
{
    nc::AudioEngine engine;
    nc::mock::DeepMockAudioIODevice* mockDevice = nullptr;

    MockAudioFixture()
    {
        // Close any system default soundcard devices first to ensure headless repeatability
        engine.audioDeviceManager().closeAudioDevice();

        auto mockType = std::make_unique<nc::mock::DeepMockAudioDeviceType>();
        engine.audioDeviceManager().addAudioDeviceType (std::move (mockType));

        // Switch to the mock ASIO device
        engine.audioDeviceManager().setCurrentAudioDeviceType ("DeepMock ASIO", true);

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        setup.outputDeviceName = "DeepMock Hardware Audio Interface";
        setup.inputDeviceName = "DeepMock Hardware Audio Interface";
        setup.sampleRate = 48000.0;
        setup.bufferSize = 512;
        setup.inputChannels.setRange (0, 4, true);
        setup.outputChannels.setRange (0, 4, true);
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;

        auto err = engine.audioDeviceManager().setAudioDeviceSetup (setup, true);
        REQUIRE (err.isEmpty());

        mockDevice = dynamic_cast<nc::mock::DeepMockAudioIODevice*> (
            engine.audioDeviceManager().getCurrentAudioDevice());
        REQUIRE (mockDevice != nullptr);
    }

    ~MockAudioFixture()
    {
        if (mockDevice != nullptr)
            mockDevice->stopStreamingThread();

        engine.audioDeviceManager().closeAudioDevice();
    }
};

struct AudioStats
{
    int64_t totalFrames = 0;
    double sampleRate = 0.0;
    float peak = 0.0f;
    float rms = 0.0f;
    int zeroCrossings = 0;
};

AudioStats analyzeWavFile (const juce::File& file, int channelToAnalyze = 0)
{
    AudioStats stats;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return stats;

    stats.totalFrames = static_cast<int64_t> (reader->lengthInSamples);
    stats.sampleRate = reader->sampleRate;

    if (stats.totalFrames <= 0)
        return stats;

    juce::AudioBuffer<float> buffer (reader->numChannels, static_cast<int> (stats.totalFrames));
    reader->read (&buffer, 0, static_cast<int> (stats.totalFrames), 0, true, true);

    const auto ch = juce::jlimit (0, static_cast<int> (reader->numChannels) - 1, channelToAnalyze);
    const auto* samples = buffer.getReadPointer (ch);
    const auto numSamples = static_cast<int> (stats.totalFrames);

    double sumSq = 0.0;
    float maxAbs = 0.0f;
    int crossings = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float s = samples[i];
        const float a = std::abs (s);
        if (a > maxAbs)
            maxAbs = a;
        sumSq += s * s;

        if (i > 0 && ((samples[i - 1] < 0.0f && s >= 0.0f) || (samples[i - 1] > 0.0f && s <= 0.0f)))
            ++crossings;
    }

    stats.peak = maxAbs;
    stats.rms = static_cast<float> (std::sqrt (sumSq / numSamples));
    stats.zeroCrossings = crossings;
    return stats;
}

} // namespace

//==============================================================================
TEST_CASE ("deep mock hardware interface life cycle, buffer sizing, and sample rate negotiation",
           "[mock][hardware]")
{
    MockAudioFixture fixture;
    auto& engine = fixture.engine;
    auto* mock = fixture.mockDevice;

    // Verify initial negotiated state (4 In / 4 Out @ 48kHz, 512 samples)
    REQUIRE (engine.isDeviceOpen());
    REQUIRE (engine.currentSampleRate() == 48000.0);
    REQUIRE (engine.currentBufferSize() == 512);
    REQUIRE (mock->getActiveInputChannels().countNumberOfSetBits() == 4);
    REQUIRE (engine.deviceDescription() == "DeepMock Hardware Audio Interface");
    REQUIRE (mock->isOpen());
    REQUIRE (mock->isPlaying());
    REQUIRE (mock->getInputLatencyInSamples() == 128);
    REQUIRE (mock->getOutputLatencyInSamples() == 128);

    // Test dynamic sample-rate and buffer-size renegotiation (e.g. 96 kHz @ 256 samples)
    {
        auto setup = engine.audioDeviceManager().getAudioDeviceSetup();
        setup.sampleRate = 96000.0;
        setup.bufferSize = 256;
        auto err = engine.audioDeviceManager().setAudioDeviceSetup (setup, true);
        REQUIRE (err.isEmpty());

        REQUIRE (engine.currentSampleRate() == 96000.0);
        REQUIRE (engine.currentBufferSize() == 256);
        REQUIRE (engine.isDeviceOpen());
    }

    // Switch back to 48 kHz @ 512 samples
    {
        auto setup = engine.audioDeviceManager().getAudioDeviceSetup();
        setup.sampleRate = 48000.0;
        setup.bufferSize = 512;
        auto err = engine.audioDeviceManager().setAudioDeviceSetup (setup, true);
        REQUIRE (err.isEmpty());

        REQUIRE (engine.currentSampleRate() == 48000.0);
        REQUIRE (engine.currentBufferSize() == 512);
    }

    // Stop device
    engine.audioDeviceManager().closeAudioDevice();
    REQUIRE_FALSE (engine.isDeviceOpen());
    REQUIRE (engine.currentBufferSize() == 0);
}

//==============================================================================
TEST_CASE ("deep mock audio interface captures hardware input, live monitoring responds, and take WAV is verified",
           "[mock][hardware][record]")
{
    MockAudioFixture fixture;
    auto& engine = fixture.engine;
    auto* mock = fixture.mockDevice;

    const auto folder = getMockTestFolder ("take-verification");

    // Configure Channel 0 with a calibrated 1 kHz sine tone at -6 dBFS (0.501187f)
    mock->setChannelSignal (0, nc::mock::DeepMockAudioIODevice::SignalType::CalibratedSine, 1000.0, 0.5011872f);

    // Arm track 0 for recording and enable monitoring
    engine.setRecordArmed (0, true);
    REQUIRE (engine.isRecordArmed (0));

    juce::String error;
    REQUIRE (engine.startRecording (folder, error));
    REQUIRE (engine.isRecording());
    REQUIRE (engine.isTrackMonitor (0));

    engine.setPlaying (true);

    // Drive 3.0 seconds through the hardware interface (282 blocks @ 48kHz / 512 samples = 144,384 frames)
    const int numBlocks = 282;
    for (int b = 0; b < numBlocks; ++b)
        mock->driveBlock();

    // Verify real-time metering responds to the injected signal:
    // Track 0 peak reflects monitored signal through the strip fader (approx 0.28f)
    const float peak = engine.trackPeak (0);
    REQUIRE (peak >= 0.20f);
    REQUIRE (peak <= 0.60f);

    // Stop recording and transport
    auto takes = engine.stopRecording (folder, 24, error);
    engine.setPlaying (false);

    REQUIRE (takes.size() == 1);
    REQUIRE (takes[0].track == 0);
    REQUIRE (takes[0].frames >= 48000 * 3);
    REQUIRE (takes[0].file.existsAsFile());
    REQUIRE (engine.recordOverflows() == 0);

    // Analyze the flushed WAV file on disk
    const auto stats = analyzeWavFile (takes[0].file, 0);

    // 1. Duration: ~3 seconds (144,384 frames)
    REQUIRE (stats.totalFrames >= 140000);
    REQUIRE (stats.sampleRate == 48000.0);

    // 2. Peak amplitude matches calibrated -6 dBFS (0.501f)
    REQUIRE (stats.peak >= 0.48f);
    REQUIRE (stats.peak <= 0.52f);

    // 3. RMS for pure sine is peak / sqrt(2) ≈ 0.501 / 1.4142 ≈ 0.354
    REQUIRE (stats.rms >= 0.33f);
    REQUIRE (stats.rms <= 0.38f);

    // 4. Frequency validation:
    // 1 kHz sine over ~3.0s has ~3,000 full cycles, which yields ~6,000 zero crossings
    REQUIRE (stats.zeroCrossings >= 5800);
    REQUIRE (stats.zeroCrossings <= 6200);

    folder.deleteRecursively();
}

//==============================================================================
TEST_CASE ("deep mock multi-channel concurrent recording separates track 1 and track 2 signals",
           "[mock][hardware][multichannel]")
{
    MockAudioFixture fixture;
    auto& engine = fixture.engine;
    auto* mock = fixture.mockDevice;

    const auto folder = getMockTestFolder ("multichannel");

    // Channel 0: 1000 Hz tone @ -6 dBFS (0.501f)
    mock->setChannelSignal (0, nc::mock::DeepMockAudioIODevice::SignalType::CalibratedSine, 1000.0, 0.5011872f);

    // Channel 1: 440 Hz tone @ -12 dBFS (0.251f)
    mock->setChannelSignal (1, nc::mock::DeepMockAudioIODevice::SignalType::CalibratedSine, 440.0, 0.2511886f);

    // Arm both Track 0 and Track 1 simultaneously
    engine.setRecordArmed (0, true);
    engine.setRecordArmed (1, true);
    REQUIRE (engine.isRecordArmed (0));
    REQUIRE (engine.isRecordArmed (1));

    juce::String error;
    REQUIRE (engine.startRecording (folder, error));
    REQUIRE (engine.isRecording());

    engine.setPlaying (true);

    // Drive 2.0 seconds (188 blocks @ 512 samples = 96,256 frames)
    const int numBlocks = 188;
    for (int b = 0; b < numBlocks; ++b)
        mock->driveBlock();

    // Verify metering disambiguation: Track 0 ~ 0.28f, Track 1 ~ 0.14f
    REQUIRE (engine.trackPeak (0) >= 0.20f);
    REQUIRE (engine.trackPeak (0) <= 0.60f);

    REQUIRE (engine.trackPeak (1) >= 0.08f);
    REQUIRE (engine.trackPeak (1) <= 0.35f);

    auto takes = engine.stopRecording (folder, 24, error);
    engine.setPlaying (false);

    REQUIRE (takes.size() == 2);
    REQUIRE (engine.recordOverflows() == 0);

    // Find take files for each track
    juce::File takeTrack0;
    juce::File takeTrack1;
    for (const auto& t : takes)
    {
        if (t.track == 0) takeTrack0 = t.file;
        if (t.track == 1) takeTrack1 = t.file;
    }

    REQUIRE (takeTrack0.existsAsFile());
    REQUIRE (takeTrack1.existsAsFile());

    const auto stats0 = analyzeWavFile (takeTrack0, 0);
    const auto stats1 = analyzeWavFile (takeTrack1, 0);

    // Track 0: 1000 Hz, peak ~0.50f, ~4000 zero crossings in 2 seconds
    REQUIRE (stats0.peak >= 0.48f);
    REQUIRE (stats0.peak <= 0.52f);
    REQUIRE (stats0.zeroCrossings >= 3800);
    REQUIRE (stats0.zeroCrossings <= 4200);

    // Track 1: 440 Hz, peak ~0.25f, ~1760 zero crossings in 2 seconds
    REQUIRE (stats1.peak >= 0.23f);
    REQUIRE (stats1.peak <= 0.27f);
    REQUIRE (stats1.zeroCrossings >= 1650);
    REQUIRE (stats1.zeroCrossings <= 1850);

    folder.deleteRecursively();
}

//==============================================================================
TEST_CASE ("deep mock real-time streaming thread handles asynchronous capture and clean shutdown",
           "[mock][hardware][async]")
{
    MockAudioFixture fixture;
    auto& engine = fixture.engine;
    auto* mock = fixture.mockDevice;

    const auto folder = getMockTestFolder ("async-stream");

    mock->setChannelSignal (0, nc::mock::DeepMockAudioIODevice::SignalType::CalibratedSine, 1000.0, 0.5011872f);

    engine.setRecordArmed (0, true);

    juce::String error;
    REQUIRE (engine.startRecording (folder, error));
    REQUIRE (engine.isRecording());

    engine.setPlaying (true);

    // Start background streaming thread (unthrottled / fast test mode)
    mock->startStreamingThread (false);

    // Let the streaming thread run concurrently for 150 ms
    juce::Thread::sleep (150);

    // Stop recording while thread is running to verify thread safety and FIFO flush
    auto takes = engine.stopRecording (folder, 24, error);
    engine.setPlaying (false);

    mock->stopStreamingThread();

    REQUIRE (takes.size() == 1);
    REQUIRE (takes[0].file.existsAsFile());
    REQUIRE (engine.recordOverflows() == 0);

    const auto stats = analyzeWavFile (takes[0].file, 0);
    REQUIRE (stats.totalFrames > 0);
    REQUIRE (stats.peak >= 0.45f);

    folder.deleteRecursively();
}
