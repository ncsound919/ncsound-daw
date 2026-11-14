#include "AudioEngine.h"

#include <nc/engine/Export.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// Two different questions live here.
//
// 1. Does the mix chain itself distort? A clean sine through the real export
//    path answers that with a number (total harmonic distortion + noise).
// 2. Does the shipped demo session sound like music? A session whose spectral
//    weight sits in the 6-12 kHz band instead of the low mids is a noise bed,
//    which is what "electronic noise" on first launch turns out to be.

namespace
{
juce::File tempFile (const juce::String& name)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    file.deleteFile();
    return file;
}

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

/** Channel 0 of a rendered WAV, from `skipFrames` for `frames`. */
std::vector<float> readMono (const juce::File& file, int skipFrames, int frames)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::vector<float> out;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return out;

    juce::AudioBuffer<float> buffer (1, frames);
    reader->read (&buffer, 0, frames, skipFrames, true, false);

    out.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
    return out;
}

/** RBJ band-pass, used to weigh two bands instead of summing an FFT. */
struct BandPass
{
    BandPass (double centreHz, double widthHz, double rate)
    {
        const auto w0 = juce::MathConstants<double>::twoPi * centreHz / rate;
        const auto alpha = std::sin (w0) / (2.0 * (centreHz / widthHz));
        const auto a0 = 1.0 + alpha;

        b0 = alpha / a0;
        b1 = 0.0;
        b2 = -alpha / a0;
        a1 = (-2.0 * std::cos (w0)) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double process (double x)
    {
        const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }

    double b0 = 0.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
};

double rms (const std::vector<float>& x, int from, int count)
{
    double sum = 0.0;

    for (int i = from; i < from + count; ++i)
        sum += static_cast<double> (x[static_cast<size_t> (i)])
               * static_cast<double> (x[static_cast<size_t> (i)]);

    return std::sqrt (sum / juce::jmax (1, count));
}

double bandRms (const std::vector<float>& x, double centre, double width, double rate)
{
    BandPass left (centre, width, rate), right (centre, width, rate);
    double sumL = 0.0, sumR = 0.0;

    // Two cascaded passes: one RBJ section's skirts are too wide to separate
    // 6-12 kHz from the low mids cleanly.
    for (const auto sample : x)
    {
        const auto a = left.process (sample);
        const auto b = right.process (a);
        sumL += b * b;
        sumR += a * a;
    }

    juce::ignoreUnused (sumR);
    return std::sqrt (sumL / juce::jmax<size_t> (1, x.size()));
}

double toDb (double value)
{
    return 20.0 * std::log10 (juce::jmax (1.0e-12, value));
}

double zeroCrossingRate (const std::vector<float>& x, double rate)
{
    int crossings = 0;

    for (size_t i = 1; i < x.size(); ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
            ++crossings;

    return static_cast<double> (crossings) * rate / juce::jmax<size_t> (1, x.size());
}

/** Distortion + noise of a tone, relative to its own fundamental: least-squares
    fit of the tone, then the RMS of what is left over. */
double thdPlusNoiseDb (const float* x, int n, double hz, double rate)
{
    double re = 0.0, im = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * hz * i / rate;
        re += x[i] * std::cos (phase);
        im += x[i] * std::sin (phase);
    }

    re *= 2.0 / n;
    im *= 2.0 / n;

    double residual = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * hz * i / rate;
        const auto fit = re * std::cos (phase) + im * std::sin (phase);
        const auto error = x[i] - fit;
        residual += error * error;
    }

    return 10.0 * std::log10 ((residual / n) / juce::jmax (1.0e-30, re * re + im * im));
}

/** The whole demo session on the timeline, exported at `rate`. */
juce::File renderDemoSession (nc::AudioEngine& engine, const juce::String& name, double rate)
{
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
        clips.push_back (clip);
    }

    if (engine.setTimelineClips (std::move (clips)) != 0)
        return {};

    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    nc::engine::export_::ExportOptions options;
    options.format = nc::engine::export_::Format::wav;
    options.bitDepth = 24;
    options.sampleRate = static_cast<int> (std::lround (rate));
    options.dither = false;

    const auto file = tempFile (name);
    juce::String error;

    if (! engine.renderToFile (file, 1.0, 3.0, options, error))
        return {};

    return file;
}
} // namespace

// The chain is allowed to change level, pan and EQ a sine. It is not allowed to
// add harmonics or broadband noise: a maximizer that reacts to a steady tone, a
// compressor with a broken envelope, or a stray clamp all show up here as a
// residual tens of dB above the export floor.
TEST_CASE ("a clean sine passes the mix chain without audible distortion", "[chain]")
{
    constexpr double rate = 48000.0;
    constexpr double toneHz = 1000.0;
    constexpr float amplitude = 0.5f;

    // Long enough for the aux delays to fill: an echo stack still adding taps
    // is a settling transient, not distortion, and would read as one.
    const auto source = writeSineWav ("nc-chain-sine.wav", rate, toneHz, 12.0, amplitude);
    REQUIRE (source.existsAsFile());

    nc::AudioEngine engine;
    engine.setBpm (120.0);

    // The aux returns are a reverb and a delay: their wet path is a legitimate
    // part of the mix, but it is not the chain measured here. Zero the sends so
    // this reads the dry chain; the aux bus has its own echo-time test.
    for (int track = 0; track < nc::AudioEngine::trackCount; ++track)
    {
        engine.setTrackSendA (track, 0.0f);
        engine.setTrackSendB (track, 0.0f);
    }

    const auto asset = engine.loadAudioFile (source);
    REQUIRE (asset >= 0);
    REQUIRE (engine.assetSampleRate (asset) == rate);

    nc::AudioEngine::TimelineClip clip;
    clip.assetId = asset;
    clip.track = 0;
    clip.startBar = 1.0;
    clip.lengthBars = 6.0;

    std::vector<nc::AudioEngine::TimelineClip> clips { clip };
    REQUIRE (engine.setTimelineClips (std::move (clips)) == 0);

    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    nc::engine::export_::ExportOptions options;
    options.format = nc::engine::export_::Format::wav;
    options.bitDepth = 24;
    options.sampleRate = static_cast<int> (lround (rate));
    options.dither = false;

    const auto file = tempFile ("nc-chain-render.wav");
    juce::String error;
    // The range is in bars, not seconds: bar 5 is 8 s at 120 BPM, by which
    // point the aux delays have long stopped adding echo taps.
    const auto rendered = engine.renderToFile (file, 5.0, 6.0, options, error);
    INFO ("renderToFile said: " << error);
    REQUIRE (rendered);

    // A full second, eight seconds in: past transport edges and past the point
    // where the aux returns have stopped adding echo taps.
    constexpr int measured = 48000;
    const auto samples = readMono (file, 44100, measured);
    REQUIRE (samples.size() == static_cast<size_t> (measured));

    // Least-squares fit of the tone, then whatever is left over is distortion
    // plus noise. Not a bin of an FFT: the residual must cover every harmonic.
    double re = 0.0, im = 0.0;

    for (int i = 0; i < measured; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * toneHz * i / rate;
        re += samples[static_cast<size_t> (i)] * std::cos (phase);
        im += samples[static_cast<size_t> (i)] * std::sin (phase);
    }

    re *= 2.0 / measured;
    im *= 2.0 / measured;
    const auto fundamentalDb = toDb (std::sqrt (re * re + im * im));

    double residual = 0.0;

    for (int i = 0; i < measured; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * toneHz * i / rate;
        const auto fit = re * std::cos (phase) + im * std::sin (phase);
        const auto error_sample = samples[static_cast<size_t> (i)] - fit;
        residual += error_sample * error_sample;
    }

    const auto residualRms = std::sqrt (residual / measured);
    const auto thdDb = toDb (residualRms) - fundamentalDb;

    INFO ("1 kHz fundamental: " << fundamentalDb << " dBFS");
    INFO ("distortion + noise: " << thdDb << " dB relative to the tone");

    // A linear float chain with 24-bit output sits near -100 dB here; the
    // threshold is far above that so ordinary EQ ripple cannot trip it, but far
    // below anything a listener would miss.
    // The baked strip EQ, the centre-pan law and the master trim legitimately
    // take a few dB; this only guards against a dead or wildly hot path.
    REQUIRE (fundamentalDb > -20.0);
    REQUIRE (thdDb < -60.0);
}

// Hidden diagnostic: where does a clean tone stop being clean? Measures the
// tone inside the loaded asset, then the same tone rendered at a matching rate
// and through the band-limited path, so the stage is named by a number.
TEST_CASE ("diagnostic: asset buffer versus rendered clip", "[.asset-probe]")
{
    constexpr double sessionRate = 48000.0;
    constexpr double toneHz = 1000.0;

    const auto source48 = writeSineWav ("nc-probe-48k.wav", 48000.0, toneHz, 4.0, 0.5f);
    const auto source24 = writeSineWav ("nc-probe-24k.wav", 24000.0, toneHz, 4.0, 0.5f);
    REQUIRE (source48.existsAsFile());
    REQUIRE (source24.existsAsFile());

    for (const auto* source : { &source48, &source24 })
    {
        nc::AudioEngine engine;
        engine.setBpm (120.0);

        const auto asset = engine.loadAudioFile (*source);
        REQUIRE (asset >= 0);

        const auto rate = engine.assetSampleRate (asset);
        const auto resident = engine.assetSamples (asset, 0.5, 1.0);
        REQUIRE (resident.getNumSamples() > 1000);

        WARN (source->getFileName() << " in the asset: " << rate << " Hz, THD+N "
              << thdPlusNoiseDb (resident.getReadPointer (0), resident.getNumSamples(),
                                 toneHz, rate) << " dB");

        // Now the same asset through the clip path at the session rate.
        nc::AudioEngine::TimelineClip clip;
        clip.assetId = asset;
        clip.track = 0;
        clip.startBar = 1.0;
        clip.lengthBars = 2.0;
        std::vector<nc::AudioEngine::TimelineClip> clips { clip };
        REQUIRE (engine.setTimelineClips (std::move (clips)) == 0);

        juce::Thread::sleep (300);
        engine.suspendAudioCallbackForTesting();

        nc::engine::export_::ExportOptions options;
        options.format = nc::engine::export_::Format::wav;
        options.bitDepth = 24;
        options.sampleRate = static_cast<int> (sessionRate);
        options.dither = false;

        juce::String error;
        const auto file = tempFile (source->getFileNameWithoutExtension() + "-render.wav");
        REQUIRE (engine.renderToFile (file, 1.0, 3.0, options, error));

        const auto rendered = readMono (file, 44100, 48000);
        REQUIRE (rendered.size() == 48000u);

        WARN (source->getFileName() << " rendered: session " << engine.currentSampleRate()
              << " Hz, THD+N " << thdPlusNoiseDb (rendered.data(), 48000, toneHz, sessionRate)
              << " dB");
    }
}

// The demo record is the first thing anyone hears. If its weight sits in the
// 6-12 kHz band and its zero-crossing rate looks like white noise, no amount of
// engine work makes the app sound right.
TEST_CASE ("the shipped demo session is music, not a noise bed", "[demo]")
{
    constexpr double rate = 48000.0;

    nc::AudioEngine engine;
    engine.setBpm (120.0);

    const auto file = renderDemoSession (engine, "nc-demo-mix.wav", rate);
    REQUIRE (file.existsAsFile());

    constexpr int measured = 96000; // two seconds
    const auto samples = readMono (file, 12000, measured);
    REQUIRE (samples.size() == static_cast<size_t> (measured));

    const auto lowMid = bandRms (samples, 1000.0, 1500.0, rate);
    const auto high = bandRms (samples, std::sqrt (6000.0 * 12000.0), 6000.0, rate);
    const auto ratioDb = toDb (high) - toDb (lowMid);
    const auto zcr = zeroCrossingRate (samples, rate);

    INFO ("6-12 kHz vs 0.5-2 kHz: " << ratioDb << " dB");
    INFO ("zero crossings: " << zcr << " per second");
    INFO ("peak: " << toDb (rms (samples, 0, measured)) << " dBFS rms");

    // Music puts its weight below 2 kHz and crosses zero a few thousand times a
    // second. A white-noise bed does neither.
    REQUIRE (ratioDb < -3.0);
    REQUIRE (zcr < 8000.0);
}

