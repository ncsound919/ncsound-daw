#include "AudioEngine.h"

#include <nc/engine/Export.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
juce::File tempFile (const juce::String& name)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    file.deleteFile();
    return file;
}

/** A mono sine at `hz`, `seconds` long, written at the source's own rate. */
juce::File writeSineWav (const juce::String& name, double rate, double hz, double seconds,
                         float amplitude)
{
    const auto file = tempFile (name);
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

struct Rendered
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;

    bool isReadable() const { return audio.getNumSamples() > 0 && audio.getNumChannels() > 0; }
};

Rendered readWav (const juce::File& file, int skipFrames, int frames)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    Rendered result;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return result;

    result.sampleRate = reader->sampleRate;
    result.audio.setSize (1, frames);
    reader->read (&result.audio, 0, frames, skipFrames, true, false);
    return result;
}

/** Amplitude of the DFT bin at `hz` over `n` frames (rectangular window).
    Both test tones sit exactly on bins, so no window is needed. */
double binAmplitude (const float* x, int n, double hz, double rate)
{
    double re = 0.0;
    double im = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * hz * i / rate;
        re += x[i] * std::cos (phase);
        im -= x[i] * std::sin (phase);
    }

    return 2.0 * std::sqrt (re * re + im * im) / n;
}

double toDb (double amplitude)
{
    return 20.0 * std::log10 (juce::jmax (1.0e-12, amplitude));
}

/** Renders one clip of `assetId` from bar 1 for `bars` bars, at `rate`. */
juce::File renderClip (nc::AudioEngine& engine, int assetId, double bars, double rate)
{
    nc::AudioEngine::TimelineClip clip;
    clip.assetId = assetId;
    clip.track = 0;
    clip.startBar = 1.0;
    clip.lengthBars = bars;
    clip.gainDb = 0.0f;

    std::vector<nc::AudioEngine::TimelineClip> clips { clip };

    if (engine.setTimelineClips (std::move (clips)) != 0)
        return {};

    // Let the device-open reset land, then keep the callback off the render.
    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    nc::engine::export_::ExportOptions options;
    options.format = nc::engine::export_::Format::wav;
    options.bitDepth = 24;
    options.sampleRate = static_cast<int> (std::lround (rate));
    options.dither = false;

    const auto file = tempFile ("nc-resample-render.wav");
    juce::String error;

    if (! engine.renderToFile (file, 1.0, 1.0 + bars, options, error))
        return {};

    return file;
}
} // namespace

// The defect this covers: an imported or demo asset keeps its own sample rate,
// and playback used to join neighbouring frames with a straight line. A line is
// the worst legal interpolator: it folds the source's upper octave back into the
// audible band (a 10 kHz tone in a 24 kHz file gains a 14 kHz image only ~6 dB
// down). Ardour and Lemon Studio both band-limit this conversion instead.
TEST_CASE ("a lower-rate clip plays without interpolation images", "[resample]")
{
    // A 24 kHz source: Nyquist is 12 kHz, so the 14 kHz image can only come
    // from the rate conversion, never from the material.
    constexpr double sourceRate = 24000.0;
    constexpr double toneHz = 10000.0;
    constexpr double sessionRate = 48000.0;
    constexpr float amplitude = 0.25f;

    const auto source = writeSineWav ("nc-resample-source-24k.wav", sourceRate, toneHz, 1.0,
                                      amplitude);
    REQUIRE (source.existsAsFile());

    nc::AudioEngine engine;
    engine.setBpm (120.0);

    const auto asset = engine.loadAudioFile (source);
    REQUIRE (asset >= 0);
    REQUIRE (engine.assetSampleRate (asset) == sourceRate);

    // 0.6 bar at 120 BPM = 1.2 s: the 1 s tone plus headroom past its end.
    const auto rendered = renderClip (engine, asset, 0.6, sessionRate);
    REQUIRE (rendered.existsAsFile());

    // Skip the first 0.1 s so transport smoothing is out of the measurement;
    // the remaining 0.5 s keeps 10 kHz and 14 kHz exactly on FFT bins.
    constexpr int measured = 24000;
    const auto audio = readWav (rendered, 4800, measured);
    REQUIRE (audio.isReadable());
    REQUIRE (audio.sampleRate == sessionRate);

    const auto* samples = audio.audio.getReadPointer (0);
    const auto fundamentalDb = toDb (binAmplitude (samples, measured, toneHz, sessionRate));
    const auto imageDb = toDb (binAmplitude (samples, measured, sessionRate / 2.0 - toneHz,
                                             sessionRate));

    INFO ("10 kHz fundamental: " << fundamentalDb << " dBFS");
    INFO ("14 kHz interpolation image: " << imageDb << " dBFS");
    INFO ("image relative to fundamental: " << (imageDb - fundamentalDb) << " dB");

    // The tone must survive the rate conversion (the strip's centre-pan law
    // and baked master EQ take their own few dB, so this is a sanity floor).
    REQUIRE (fundamentalDb > -20.0);
    // Band-limited conversion puts the image far below audibility; a line
    // leaves it around -6 dB, i.e. plainly audible grit on every 24 kHz asset.
    REQUIRE (imageDb - fundamentalDb < -40.0);
}

// Hidden diagnostic: what the band-limited read costs per clip, so the kernel
// length stays an honest trade against the audio thread's deadline. Reported,
// never asserted: timing on a loaded machine is not a test.
TEST_CASE ("band-limited playback cost", "[.resample-bench]")
{
    // The read itself, isolated from the render's I/O: 32 taps per channel
    // against the 2 taps the old line used, over the same 2048 frames.
    std::vector<float> source (2048);

    for (int i = 0; i < 2048; ++i)
        source[static_cast<size_t> (i)] = std::sin (0.01f * static_cast<float> (i));

    nc::engine::resample::Bank bank;
    bank.build (24000.0, 48000.0);

    constexpr int reads = 44100 * 4; // four seconds of one clip

    const auto timeTaps = [&source, &bank] (int taps)
    {
        volatile float sink = 0.0f;
        const auto started = juce::Time::getMillisecondCounterHiRes();

        for (int read = 0; read < reads; ++read)
        {
            const auto position = static_cast<double> (read) * 0.5;
            const auto first = static_cast<int64_t> (std::floor (position));
            const auto* weights = bank.weights (position - static_cast<double> (first));
            const auto start = first - (nc::engine::resample::taps / 2 - 1);
            float sum = 0.0f;

            for (int tap = 0; tap < taps; ++tap)
                sum += weights[tap] * source[static_cast<size_t> (
                    juce::jlimit<int64_t> (0, 2047, start + tap))];

            sink += sum;
        }

        return juce::Time::getMillisecondCounterHiRes() - started;
    };

    timeTaps (2); // warm
    const auto bandLimited = timeTaps (nc::engine::resample::taps);
    const auto linear = timeTaps (2);

    WARN ("one clip over 4 s of audio: " << bandLimited << " ms band-limited, " << linear
          << " ms with 2 taps; 16 converted clips cost "
          << (16.0 * bandLimited / 4000.0) << "x realtime");
}

// The conversion must not shade the material it converts. A 24 kHz source and
// a 48 kHz source of the same tone have to arrive at the same level, so the
// kernel cannot droop the top octave (which a short filter does) and the
// rate-1 path must stay a straight copy.
TEST_CASE ("rate conversion keeps the level of the material it converts", "[resample]")
{
    constexpr double toneHz = 10000.0;
    constexpr float amplitude = 0.25f;

    const auto levelOf = [] (double sourceRate, const juce::String& name)
    {
        const auto source = writeSineWav (name, sourceRate, toneHz, 1.0, amplitude);
        REQUIRE (source.existsAsFile());

        nc::AudioEngine engine;
        engine.setBpm (120.0);

        const auto asset = engine.loadAudioFile (source);
        REQUIRE (asset >= 0);
        REQUIRE (engine.assetSampleRate (asset) == sourceRate);

        const auto rendered = renderClip (engine, asset, 0.6, 48000.0);
        REQUIRE (rendered.existsAsFile());

        const auto audio = readWav (rendered, 4800, 24000);
        REQUIRE (audio.isReadable());

        return toDb (binAmplitude (audio.audio.getReadPointer (0), 24000, toneHz, 48000.0));
    };

    const auto fromLowerRate = levelOf (24000.0, "nc-resample-level-24k.wav");
    const auto native = levelOf (48000.0, "nc-resample-level-48k.wav");

    INFO ("24 kHz source, converted: " << fromLowerRate << " dBFS");
    INFO ("48 kHz source, native: " << native << " dBFS");
    REQUIRE (std::abs (fromLowerRate - native) < 0.5);
}
