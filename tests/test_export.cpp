#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nc/engine/Export.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace nc::engine::export_;
using Catch::Approx;

namespace
{
std::vector<float> sine (size_t frames, double hz, double rate, double amplitude)
{
    std::vector<float> samples (frames);
    for (size_t i = 0; i < frames; ++i)
        samples[i] = static_cast<float> (amplitude * std::sin (2.0 * 3.141592653589793 * hz * static_cast<double> (i) / rate));
    return samples;
}
} // namespace

TEST_CASE ("TPDF dither is deterministic for a fixed seed and band-limited")
{
    std::vector<float> a (1000, 0.25f);
    std::vector<float> b (1000, 0.25f);
    DitherRng rngA;
    DitherRng rngB;
    rngA.seed (42);
    rngB.seed (42);

    applyTpdfDither (a.data(), a.size(), 16, rngA);
    applyTpdfDither (b.data(), b.size(), 16, rngB);

    // Same seed -> identical bits (null-testable re-export).
    REQUIRE (a == b);

    // Dither stays within +-1 LSB of 16-bit (2^-15) around the source.
    const auto lsb = std::ldexp (1.0, 1 - 16);
    for (const auto v : a)
        REQUIRE (std::abs (v - 0.25f) < static_cast<float> (lsb + 1e-9));

    // 32-bit float is never dithered.
    std::vector<float> untouched (8, 0.5f);
    DitherRng rng;
    rng.seed (1);
    applyTpdfDither (untouched.data(), untouched.size(), 32, rng);
    for (const auto v : untouched)
        REQUIRE (v == Approx (0.5f));
}

TEST_CASE ("sine at -23 dBFS measures near -23 LUFS integrated")
{
    constexpr double rate = 48000.0;
    const auto frames = static_cast<size_t> (rate * 3.0); // 3 s
    // 1 kHz sine reads about 3 dB below its peak RMS after K-weighting.
    const auto amplitude = static_cast<double> (std::pow (10.0, -23.0 / 20.0)) * 1.41;
    const auto left = sine (frames, 1000.0, rate, amplitude);
    const auto right = sine (frames, 1000.0, rate, amplitude);

    const auto result = measureLoudness (left.data(), right.data(), frames, rate);

    REQUIRE_FALSE (result.silent);
    REQUIRE (result.integratedLufs == Approx (-23.0).margin (3.0));
    REQUIRE (result.samplePeakDb == Approx (-23.0 + 3.0).margin (3.0));
    REQUIRE (result.durationSeconds == Approx (3.0));
}

TEST_CASE ("silence measures as silent with no gated energy")
{
    std::vector<float> zeros (48000, 0.0f);
    const auto result = measureLoudness (zeros.data(), zeros.data(), zeros.size(), 48000.0);

    REQUIRE (result.silent);
    REQUIRE (result.integratedLufs == Approx (-120.0));
    REQUIRE (gainDbForTarget (result, -14.0) == Approx (0.0));
}

TEST_CASE ("loudness target gain is target minus measured")
{
    LoudnessResult measured;
    measured.integratedLufs = -20.0;
    measured.silent = false;

    REQUIRE (gainDbForTarget (measured, -14.0) == Approx (6.0));
    REQUIRE (gainDbForTarget (measured, -23.0) == Approx (-3.0));
}

TEST_CASE ("true-peak ceiling gain pulls hot renders under the ceiling")
{
    LoudnessResult hot;
    hot.truePeakDbTp = 0.5; // above a -1 dBTP ceiling
    REQUIRE (gainDbForTruePeak (hot, -1.0) == Approx (-1.5));

    LoudnessResult cool;
    cool.truePeakDbTp = -3.0;
    REQUIRE (gainDbForTruePeak (cool, -1.0) == Approx (0.0));
}

TEST_CASE ("export options sanitize and format helpers round-trip")
{
    auto options = sanitizeOptions (ExportOptions { Format::mp3, 100000, 32, false, false, -30.0, 0.5, false, "" });

    REQUIRE (options.sampleRate == 100000);
    REQUIRE (options.bitDepth == 32);
    REQUIRE (options.lufsTarget == Approx (-30.0));
    REQUIRE (options.truePeakCeilingDb == Approx (0.0)); // clamped to 0 dBTP max
    REQUIRE (options.baseName == "mixdown");

    REQUIRE (formatFromExtension (".flac") == Format::flac);
    REQUIRE (formatFromExtension ("MP3") == Format::mp3);
    REQUIRE (formatFromExtension ("unknown") == Format::wav);
    REQUIRE (isLossless (Format::flac));
    REQUIRE_FALSE (isLossless (Format::mp3));
}

TEST_CASE ("stem filenames sanitize names and pad the index")
{
    REQUIRE (stemFileName ("mixdown", 0, "Lead Vocal", Format::wav) == "mixdown - 01 - Lead_Vocal.wav");
    REQUIRE (stemFileName ("song", 11, "Kick/Drum!", Format::flac) == "song - 12 - Kick_Drum.flac");

    const auto fallback = stemFileName ("mixdown", 3, "!!!", Format::mp3);
    REQUIRE (fallback == "mixdown - 04 - track.mp3");
}

TEST_CASE ("a mono render reads 3 LU below the same signal as dual-mono stereo")
{
    constexpr double rate = 48000.0;
    const auto frames = static_cast<size_t> (rate * 2.0);
    const auto left = sine (frames, 1000.0, rate, 0.5);
    const auto right = left;

    const auto stereo = measureLoudness (left.data(), right.data(), frames, rate);
    const auto mono = measureLoudness (left.data(), nullptr, frames, rate);

    REQUIRE_FALSE (stereo.silent);
    REQUIRE_FALSE (mono.silent);
    REQUIRE (stereo.integratedLufs - mono.integratedLufs == Approx (3.0103).margin (0.1));
}

TEST_CASE ("true peak catches inter-sample peaks the sample scan cannot see")
{
    // 4-sample pattern 0, 0.5, 0.5, 0: the sample peak is 0.5, but the
    // reconstructed waveform peaks around 0.625 between samples.
    std::vector<float> left (48000, 0.0f);
    for (size_t i = 0; i + 2 < left.size(); i += 4)
    {
        left[i + 1] = 0.5f;
        left[i + 2] = 0.5f;
    }

    const auto result = measureLoudness (left.data(), left.data(), left.size(), 48000.0);

    REQUIRE (result.samplePeakDb == Approx (20.0 * std::log10 (0.5)).margin (0.01));
    REQUIRE (result.truePeakDbTp > result.samplePeakDb + 0.5);
}

TEST_CASE ("K-weighting tracks the sample rate instead of assuming 48 kHz")
{
    const auto lufsAt = [] (double rate)
    {
        const auto frames = static_cast<size_t> (rate * 2.0);
        const auto signal = sine (frames, 1000.0, rate, 0.5);
        return measureLoudness (signal.data(), signal.data(), frames, rate).integratedLufs;
    };

    const auto reference = lufsAt (48000.0);
    REQUIRE (lufsAt (44100.0) == Approx (reference).margin (0.5));
    REQUIRE (lufsAt (96000.0) == Approx (reference).margin (0.5));
}

TEST_CASE ("a short render uses the documented ungated fallback")
{
    const auto signal = sine (4800, 1000.0, 48000.0, 0.5); // 100 ms
    const auto result = measureLoudness (signal.data(), signal.data(), signal.size(), 48000.0);

    REQUIRE_FALSE (result.silent);
    REQUIRE (result.durationSeconds == Approx (0.1));
    REQUIRE (result.integratedLufs > -70.0);
}

TEST_CASE ("non-finite samples cannot poison a measurement")
{
    std::vector<float> left (48000, 0.25f);
    left[100] = std::numeric_limits<float>::quiet_NaN();
    left[200] = std::numeric_limits<float>::infinity();

    const auto result = measureLoudness (left.data(), left.data(), left.size(), 48000.0);

    REQUIRE_FALSE (result.silent);
    REQUIRE (std::isfinite (result.integratedLufs));
    REQUIRE (std::isfinite (result.truePeakDbTp));
    REQUIRE (std::isfinite (result.samplePeakDb));
}
