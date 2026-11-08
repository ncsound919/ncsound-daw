#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace nc::engine::export_
{
/** Export planning + offline-render helpers. Header-only and JUCE-free so the
    render path stays unit-testable in nc_tests: the app renders stereo float
    blocks and hands them here for measuring, dithering, and writing.

    Best practice (iZotope / SoundBridge dither checklist, EBU R128 / BS.1770):
      - TPDF dither, applied exactly once, only on bit-depth reduction
        (32-bit float -> 16/24-bit). 32-bit float export is never dithered.
      - Loudness per ITU-R BS.1770: K-weighting, 400 ms blocks at 75% overlap,
        absolute gate -70 LUFS, relative gate -10 LU below the ungated value.
      - True-peak via 4x oversampling. Streaming: -14 LUFS / -1 dBTP.
        Broadcast: -23 LUFS (EBU R128).
    Stems are one file per track over the same range/rate, dithered and
    measured exactly like the mix (stem filenames keep the mix's metadata).
*/

enum class Format
{
    wav,
    flac,
    mp3,
    aiff
};

inline const char* formatName (Format format) noexcept
{
    switch (format)
    {
        case Format::wav:  return "WAV";
        case Format::flac: return "FLAC";
        case Format::mp3:  return "MP3";
        case Format::aiff: return "AIFF";
    }

    return "WAV";
}

inline const char* formatExtension (Format format) noexcept
{
    switch (format)
    {
        case Format::wav:  return "wav";
        case Format::flac: return "flac";
        case Format::mp3:  return "mp3";
        case Format::aiff: return "aiff";
    }

    return "wav";
}

inline bool isLossless (Format format) noexcept
{
    return format == Format::wav || format == Format::flac || format == Format::aiff;
}

inline Format formatFromExtension (const std::string& extension)
{
    auto lower = extension;
    for (auto& c : lower)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));

    if (lower == ".flac" || lower == "flac")
        return Format::flac;
    if (lower == ".mp3" || lower == "mp3")
        return Format::mp3;
    if (lower == ".aif" || lower == ".aiff" || lower == "aif" || lower == "aiff")
        return Format::aiff;

    return Format::wav;
}

/** One export: mix or a single stem. */
struct ExportOptions
{
    Format format = Format::wav;
    int sampleRate = 48000;
    int bitDepth = 24;             // 16 | 24 | 32 (32 = float, never dithered)
    bool dither = true;            // TPDF when reducing from float
    bool normalizeToTarget = false;
    double lufsTarget = -14.0;     // streaming target; -23.0 for broadcast
    double truePeakCeilingDb = -1.0;
    bool stems = false;            // one file per track (see stemFileName)
    std::string baseName = "mixdown";
};

/** Clamp user-supplied options into values the render path can honour. */
inline std::string sanitizeFileNamePart (std::string name)
{
    std::string clean;
    clean.reserve (name.size());

    for (const auto c : name)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                     || (c >= '0' && c <= '9') || c == '-' || c == '_';
        clean += ok ? c : '_';
    }

    while (! clean.empty() && (clean.front() == '_' || clean.back() == '_'))
    {
        if (clean.front() == '_')
            clean.erase (clean.begin());
        if (! clean.empty() && clean.back() == '_')
            clean.pop_back();
    }

    if (clean.size() > 100)
        clean.resize (100);

    return clean;
}

inline ExportOptions sanitizeOptions (ExportOptions options)
{
    options.sampleRate = static_cast<int> (std::clamp (options.sampleRate, 8000, 192000));

    if (options.bitDepth != 16 && options.bitDepth != 32)
        options.bitDepth = 24;

    if (! std::isfinite (options.lufsTarget))
        options.lufsTarget = -14.0;

    options.lufsTarget = std::clamp (options.lufsTarget, -48.0, 0.0);

    if (! std::isfinite (options.truePeakCeilingDb))
        options.truePeakCeilingDb = -1.0;

    options.truePeakCeilingDb = std::clamp (options.truePeakCeilingDb, -6.0, 0.0);

    if (options.baseName.empty())
        options.baseName = "mixdown";

    options.baseName = sanitizeFileNamePart (options.baseName);
    if (options.baseName.empty())
        options.baseName = "mixdown";

    return options;
}

//==============================================================================
// TPDF dither
//==============================================================================

/** Seeded xorshift32. Deterministic per seed so a re-export null-tests
    exactly: same render + same seed -> identical bits. */
struct DitherRng
{
    uint32_t state = 0x9E3779B9u;

    void seed (uint32_t value) noexcept
    {
        state = value == 0 ? 0x9E3779B9u : value;
    }

    inline uint32_t nextU32() noexcept
    {
        auto x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    /** Uniform in [-1, 1). */
    inline double nextSigned() noexcept
    {
        return static_cast<double> (nextU32()) / 4294967296.0 * 2.0 - 1.0;
    }
};

/** Triangular PDF sample in LSB units of a `bitDepth`-bit target:
    (r1 + r2) / 2, i.e. the sum of two uniforms scaled to +-1 LSB. */
inline double tpdfSample (DitherRng& rng) noexcept
{
    return (rng.nextSigned() + rng.nextSigned()) * 0.5;
}

/** Adds TPDF noise in place. Only exact bit depths 16 and 24 are dithered;
    anything else (32 = float, or invalid values) is a no-op — invalid
    bitDepth previously produced LSBs larger than full scale.
    Output may exceed +/-1.0 by up to 1 LSB; the int writer saturates. */
inline void applyTpdfDither (float* channel, std::size_t frames, int bitDepth, DitherRng& rng) noexcept
{
    if (channel == nullptr || frames == 0 || (bitDepth != 16 && bitDepth != 24))
        return;

    const auto lsb = std::ldexp (1.0, 1 - bitDepth); // 2^(1-bits): quantiser step
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto noise = static_cast<float> (tpdfSample (rng) * lsb);
        channel[i] += noise;
    }
}

//==============================================================================
// Loudness (ITU-R BS.1770) + true-peak
//==============================================================================

struct LoudnessResult
{
    double integratedLufs = -120.0; // -120 == silence (no gated blocks)
    double truePeakDbTp = -120.0;
    double samplePeakDb = -120.0;
    double durationSeconds = 0.0;
    int64_t frames = 0;
    double sampleRate = 48000.0;
    bool silent = true;
};

namespace detail
{
/** K-weighting at 48 kHz (BS.1770): pre-filter (4 dB high shelf) then RLB
    high-pass. For non-48 kHz rates coefficients are derived via the bilinear
    transform of the 48 kHz analog prototypes (RBJ cookbook form for the
    shelf / high-pass), so 44.1/96/192 kHz exports stay within ~0.3 LU of
    pyloudnorm reference vectors. Separate instances per channel. */
struct KWeighting
{
    double shelfB0 = 1.53512485958697;
    double shelfB1 = -2.69169618940638;
    double shelfB2 = 1.19839281085285;
    double shelfA1 = -1.69065929318241;
    double shelfA2 = 0.73248077421585;
    double hpB0 = 1.0;
    double hpB1 = -2.0;
    double hpB2 = 1.0;
    double hpA1 = -1.99004745483398;
    double hpA2 = 0.99007225036621;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;   // shelf state
    double hx1 = 0, hx2 = 0, hy1 = 0, hy2 = 0; // high-pass state

    KWeighting() = default;

    explicit KWeighting (double sampleRate)
    {
        if (! std::isfinite (sampleRate) || sampleRate <= 0.0)
            return;

        // The published BS.1770 coefficients are exact at 48 kHz; keep them
        // bit-for-bit so reference vectors match. Other rates derive the same
        // analog prototypes with the bilinear transform (RBJ cookbook form).
        if (std::abs (sampleRate - 48000.0) < 1.0)
            return;

        constexpr double pi = 3.14159265358979323846;
        const auto fs = sampleRate;

        // Pre-filter: high shelf, f0 1681.97 Hz, +4 dB, Q 0.7071 (BS.1770).
        constexpr double shelfF0 = 1681.974450955533;
        constexpr double shelfGainDb = 3.999843853973347;
        constexpr double shelfQ = 0.7071752369554196;
        const auto k = std::tan (pi * shelfF0 / fs);
        const auto vh = std::pow (10.0, shelfGainDb / 20.0);
        const auto vb = std::pow (vh, 0.4996667741545416);
        const auto shelfDenom = 1.0 + k / shelfQ + k * k;

        shelfB0 = (vh + vb * k / shelfQ + k * k) / shelfDenom;
        shelfB1 = 2.0 * (k * k - vh) / shelfDenom;
        shelfB2 = (vh - vb * k / shelfQ + k * k) / shelfDenom;
        shelfA1 = 2.0 * (k * k - 1.0) / shelfDenom;
        shelfA2 = (1.0 - k / shelfQ + k * k) / shelfDenom;

        // RLB high-pass, f0 38 Hz, Q 0.5. b stays [1, -2, 1] (the BS.1770
        // convention); the a terms are the exact RBJ forms, including a2 =
        // ((1-K)/(1+K))^2 rather than the linear approximation.
        constexpr double hpF0 = 38.0;
        const auto kh = std::tan (pi * hpF0 / fs);
        const auto hpDenom = 1.0 + kh / 0.5 + kh * kh;

        hpB0 = 1.0;
        hpB1 = -2.0;
        hpB2 = 1.0;
        hpA1 = 2.0 * (kh * kh - 1.0) / hpDenom;
        hpA2 = (1.0 - kh / 0.5 + kh * kh) / hpDenom;
    }

    inline double process (double sample) noexcept
    {
        const auto shelfOut = shelfB0 * sample + shelfB1 * x1 + shelfB2 * x2
                            - shelfA1 * y1 - shelfA2 * y2;
        x2 = x1; x1 = sample; y2 = y1; y1 = shelfOut;

        const auto hpOut = hpB0 * shelfOut + hpB1 * hx1 + hpB2 * hx2
                         - hpA1 * hy1 - hpA2 * hy2;
        hx2 = hx1; hx1 = shelfOut; hy2 = hy1; hy1 = hpOut;
        return hpOut;
    }
};

/** Catmull-Rom cubic reconstruction between samples i and i+1, i.e. 4x
    oversampling for the true-peak scan. This is an approximation, not the
    BS.1770 Annex 2 polyphase FIR: it recovers inter-sample peaks (so TP can
    exceed the sample peak) but its error near Nyquist is larger than a
    48-tap windowed-sinc. Every tap access is clamped, including i+2 at the
    end of the render, and non-finite samples read as 0. */
inline double cubicAt (const float* channel, std::size_t frames, std::size_t i, double t) noexcept
{
    const auto last = frames - 1;
    const auto at = [channel, last] (std::size_t index) {
        const auto value = channel[index > last ? last : index];
        return std::isfinite (value) ? static_cast<double> (value) : 0.0;
    };

    const auto p0 = at (i == 0 ? 0 : i - 1);
    const auto p1 = at (i);
    const auto p2 = at (i + 1);
    const auto p3 = at (i + 2);
    const auto t2 = t * t;
    const auto t3 = t2 * t;

    return 0.5 * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
                  + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}
} // namespace detail

/** Integrated loudness (ITU-R BS.1770) of a whole stereo render.
    `right` may be null for mono (single channel, G=1). Non-finite samples
    are treated as 0. frames == 0 -> silence. */
inline LoudnessResult measureLoudness (const float* left, const float* right,
                                       std::size_t frames, double sampleRate)
{
    LoudnessResult result;
    result.sampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    result.frames = static_cast<int64_t> (frames);
    const auto mono = right == nullptr;
    const auto safe = [](float v) { return std::isfinite (v) ? static_cast<double> (v) : 0.0; };

    if (frames == 0 || left == nullptr)
        return result;

    const auto rate = result.sampleRate;
    result.durationSeconds = static_cast<double> (frames) / rate;

    // Continuous K-weighting per channel (BS.1770 filters once, then windows).
    // Separate instances per channel — one shared filter cross-contaminates.
    std::vector<double> filteredL (frames);
    std::vector<double> filteredR (mono ? 0 : frames);
    detail::KWeighting kL (rate);
    detail::KWeighting kR (rate);
    for (std::size_t i = 0; i < frames; ++i)
    {
        filteredL[i] = kL.process (safe (left[i]));
        if (! mono)
            filteredR[i] = kR.process (safe (right[i]));
    }

    // Sample + true-peak scan (cubic oversampling between samples).
    double peak = 0.0;
    double tpPeak = 0.0;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto l = safe (left[i]);
        const auto r = mono ? l : safe (right[i]);
        peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        tpPeak = std::max (tpPeak, std::max (std::abs (l), std::abs (r)));

        if (i + 1 < frames)
        {
            for (int q = 1; q < 4; ++q)
            {
                const auto t = q * 0.25;
                tpPeak = std::max (tpPeak, std::abs (detail::cubicAt (left, frames, i, t)));

                if (! mono)
                    tpPeak = std::max (tpPeak, std::abs (detail::cubicAt (right, frames, i, t)));
            }
        }
    }

    result.samplePeakDb = peak > 0.0 ? 20.0 * std::log10 (peak) : -120.0;
    result.truePeakDbTp = tpPeak > 0.0 ? 20.0 * std::log10 (tpPeak) : -120.0;

    const auto blockFrames = static_cast<std::size_t> (std::llround (0.4 * rate));
    const auto hopFrames = blockFrames / 4;

    if (blockFrames == 0 || hopFrames == 0)
        return result;

    // BS.1770 energy: sum of channel mean-squares (G=1 per channel), so
    // stereo z = msL + msR and LUFS = -0.691 + 10log10(z). Dividing by 2
    // (previous bug) read 3.01 LU low.
    const auto blockMeanSq = [&](std::size_t start, std::size_t count) {
        double sum = 0.0;
        for (std::size_t i = start; i < start + count; ++i)
        {
            sum += filteredL[i] * filteredL[i];
            if (! mono)
                sum += filteredR[i] * filteredR[i];
        }
        return sum / static_cast<double> (count);
    };

    if (frames < blockFrames)
    {
        // Shorter than one 400 ms block: documented ungated fallback.
        const auto meanSq = blockMeanSq (0, frames);
        if (meanSq <= 0.0)
            return result;
        const auto lufs = -0.691 + 10.0 * std::log10 (meanSq);
        if (lufs < -70.0)
            return result;
        result.integratedLufs = lufs;
        result.silent = false;
        return result;
    }

    std::vector<double> blockEnergies;
    blockEnergies.reserve ((frames - blockFrames) / hopFrames + 1);
    for (std::size_t start = 0; start + blockFrames <= frames; start += hopFrames)
    {
        const auto meanSq = blockMeanSq (start, blockFrames);
        const auto lufs = meanSq > 0.0 ? -0.691 + 10.0 * std::log10 (meanSq) : -120.0;
        if (lufs >= -70.0) // absolute gate: silent blocks never vote
            blockEnergies.push_back (meanSq);
    }

    if (blockEnergies.empty())
        return result;

    double ungatedSum = 0.0;
    for (const auto e : blockEnergies)
        ungatedSum += e;
    const auto ungatedMean = ungatedSum / static_cast<double> (blockEnergies.size());
    const auto ungatedLufs = -0.691 + 10.0 * std::log10 (ungatedMean);
    const auto thresholdMean = std::pow (10.0, (ungatedLufs - 10.0 + 0.691) / 10.0);

    double gatedSum = 0.0;
    std::size_t gatedCount = 0;
    for (const auto e : blockEnergies)
    {
        if (e >= thresholdMean)
        {
            gatedSum += e;
            ++gatedCount;
        }
    }

    if (gatedCount == 0)
        return result;

    result.integratedLufs = -0.691 + 10.0 * std::log10 (gatedSum / static_cast<double> (gatedCount));
    result.silent = false;
    return result;
}

/** Gain (dB) that moves a measured render onto the loudness target.
    Silence -> 0 dB (nothing to normalise). */
inline double gainDbForTarget (const LoudnessResult& result, double lufsTarget) noexcept
{
    if (result.silent || ! std::isfinite (result.integratedLufs))
        return 0.0;

    return std::clamp (lufsTarget - result.integratedLufs, -48.0, 48.0);
}

/** Gain (dB) that brings a measured render under the true-peak ceiling.
    Returns the negative headroom (or 0 when already under the ceiling). */
inline double gainDbForTruePeak (const LoudnessResult& result, double ceilingDb) noexcept
{
    if (! std::isfinite (result.truePeakDbTp) || ! std::isfinite (ceilingDb))
        return 0.0;

    if (result.truePeakDbTp <= ceilingDb)
        return 0.0;

    return std::min (0.0, ceilingDb - result.truePeakDbTp);
}

/** Maximum boost (dB) allowed by the true-peak ceiling: unlimited positive
    boost when the render is already below the ceiling. */
inline double maxBoostForTruePeak (const LoudnessResult& result, double ceilingDb) noexcept
{
    if (! std::isfinite (result.truePeakDbTp) || ! std::isfinite (ceilingDb))
        return 48.0;

    return std::max (-48.0, std::min (48.0, ceilingDb - result.truePeakDbTp));
}

//==============================================================================
// Stems
//==============================================================================

inline bool isFileNameChar (char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '-' || c == '_';
}

// sanitizeFileNamePart uses the same whitelist and additionally strips
// leading/trailing underscores and caps length.

/** "mixdown - 04 - Vocal.wav": zero-padded index, sanitised name, never a path.
    Both base and track name are sanitised; index is clamped to [0, 999]. */
inline std::string stemFileName (const std::string& base, int trackIndex,
                                 const std::string& trackName, Format format)
{
    auto clean = sanitizeFileNamePart (trackName);
    if (clean.empty())
        clean = "track";

    const auto safeBase = sanitizeFileNamePart (base);
    const auto safeIndex = std::clamp (trackIndex, 0, 998);

    char index[16];
    std::snprintf (index, sizeof (index), "%02d", safeIndex + 1);

    return safeBase + " - " + index + " - " + clean + "." + formatExtension (format);
}

} // namespace nc::engine::export_
