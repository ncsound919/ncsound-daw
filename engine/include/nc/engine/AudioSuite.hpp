#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace nc::engine::audiosuite
{
/** Non-owning stereo view of the frames an offline process may rewrite.

    AudioSuite processing is destructive on a clip, never real-time, so this
    layer is plain C++ with no JUCE dependency: every effect is deterministic
    and unit-tested, and the app only has to hand it a buffer.
*/
struct RenderView
{
    float* left = nullptr;
    float* right = nullptr;
    int frames = 0;
    double sampleRate = 48000.0;
};

struct LoFiParams
{
    float bits = 8.0f;   // effective bit depth, 1..16
    float crush = 4.0f;  // sample-and-hold factor, 1 = no crush
    float wet = 1.0f;    // 0 = untouched source, 1 = fully processed
};

/** Tape stop / tape start: playback rate ramps across the clip. */
struct VarFiParams
{
    double startRate = 1.0;
    double endRate = 0.0;
};

/** Fast Track FX: one-knob drive with a high-pass and a presence shelf. */
struct FastTrackParams
{
    float driveDb = 6.0f;
    float highPassHz = 90.0f;
    float presenceDb = 3.0f;
};

struct Trim
{
    int firstFrame = 0;
    int frameCount = 0;
};

enum class Kind
{
    lofi,
    varifi,
    fastTrack,
    normalize,
    reverse,
    invert,
    gain,
    silence,
    stripSilence
};

inline const char* kindName (Kind kind) noexcept
{
    switch (kind)
    {
        case Kind::lofi:         return "LoFi";
        case Kind::varifi:       return "Var-Fi";
        case Kind::fastTrack:    return "Fast Track FX";
        case Kind::normalize:    return "Normalize";
        case Kind::reverse:      return "Reverse";
        case Kind::invert:       return "Invert Polarity";
        case Kind::gain:         return "Gain";
        case Kind::silence:      return "Silence";
        case Kind::stripSilence: return "Strip Silence";
    }

    return "Process";
}

inline float dbToGain (float db) noexcept
{
    return std::pow (10.0f, db / 20.0f);
}

inline float peakOf (RenderView view) noexcept
{
    float peak = 0.0f;

    for (int i = 0; i < view.frames; ++i)
    {
        peak = std::max (peak, std::abs (view.left[i]));
        peak = std::max (peak, std::abs (view.right[i]));
    }

    return peak;
}

/** Linear interpolation; an exact integer position returns the sample itself. */
inline float sampleLinear (const float* data, int frames, int64_t index, float fraction) noexcept
{
    if (data == nullptr || frames <= 0 || index < 0 || index >= frames)
        return 0.0f;

    if (fraction <= 0.0f)
        return data[index];

    const auto next = index + 1 < frames ? index + 1 : index;
    return data[index] + (data[next] - data[index]) * fraction;
}

/** Bit-depth reduction plus sample-and-hold, blended by the wet control. */
inline void processLoFi (RenderView view, const LoFiParams& params) noexcept
{
    if (view.frames <= 0)
        return;

    const auto wet = std::clamp (params.wet, 0.0f, 1.0f);

    if (wet <= 0.0f)
        return; // exact passthrough

    const auto levels = std::pow (2.0f, std::clamp (params.bits, 1.0f, 16.0f) - 1.0f);
    // A hostile or automated parameter must not overflow the cast; the hold is
    // bounded to a musically useful range instead.
    const auto hold = static_cast<int> (std::lround (std::clamp (params.crush, 1.0f, 1024.0f)));

    float heldLeft = 0.0f;
    float heldRight = 0.0f;

    for (int i = 0; i < view.frames; ++i)
    {
        if (i % hold == 0)
        {
            heldLeft = std::round (view.left[i] * levels) / levels;
            heldRight = std::round (view.right[i] * levels) / levels;
        }

        view.left[i] = wet * heldLeft + (1.0f - wet) * view.left[i];
        view.right[i] = wet * heldRight + (1.0f - wet) * view.right[i];
    }
}

/** Varispeed render: reads the source at a ramping rate into a fresh buffer. */
inline void processVarFiInto (RenderView dest, RenderView source, const VarFiParams& params) noexcept
{
    if (dest.frames <= 0)
        return;

    const auto startRate = std::clamp (params.startRate, 0.0, 8.0);
    const auto endRate = std::clamp (params.endRate, 0.0, 8.0);
    double position = 0.0;

    for (int i = 0; i < dest.frames; ++i)
    {
        const auto t = dest.frames > 1 ? static_cast<double> (i) / static_cast<double> (dest.frames - 1) : 0.0;
        const auto rate = startRate + (endRate - startRate) * t;
        const auto index = static_cast<int64_t> (std::floor (position));
        const auto fraction = static_cast<float> (position - static_cast<double> (index));

        dest.left[i] = sampleLinear (source.left, source.frames, index, fraction);
        dest.right[i] = sampleLinear (source.right, source.frames, index, fraction);

        // Advance after reading: dest[0] always mirrors source[0], which keeps
        // a unity rate an exact identity.
        position += rate;
    }
}

inline void processVarFi (RenderView view, const VarFiParams& params)
{
    if (view.frames <= 0)
        return;

    std::vector<float> left (view.left, view.left + view.frames);
    std::vector<float> right (view.right, view.right + view.frames);
    const RenderView source { left.data(), right.data(), view.frames, view.sampleRate };

    processVarFiInto (view, source, params);
}

/** Neutral settings (0 drive, 0 Hz high-pass, 0 dB presence) are a no-op. */
inline void processFastTrack (RenderView view, const FastTrackParams& params)
{
    if (view.frames <= 0)
        return;

    const auto highPass = std::max (0.0f, params.highPassHz);
    const auto presence = params.presenceDb;
    const auto drive = std::max (0.0f, params.driveDb);

    if (highPass <= 0.0f && presence == 0.0f && drive <= 0.0f)
        return;

    const auto rate = view.sampleRate > 0.0 ? view.sampleRate : 48000.0;
    const auto highPassCoef = highPass > 0.0f
                                  ? static_cast<float> (1.0 - std::exp (-2.0 * 3.14159265358979323846 * highPass / rate))
                                  : 0.0f;
    const auto shelfCoef = static_cast<float> (1.0 - std::exp (-2.0 * 3.14159265358979323846 * 1800.0 / rate));
    const auto presenceGain = dbToGain (presence) - 1.0f;
    const auto driveGain = drive > 0.0f ? dbToGain (drive) : 1.0f;
    const auto driveNorm = drive > 0.0f ? std::tanh (driveGain) : 1.0f;

    auto channel = [&] (float* data)
    {
        float dcState = 0.0f;
        float shelfState = 0.0f;

        for (int i = 0; i < view.frames; ++i)
        {
            auto value = data[i];

            if (highPass > 0.0f)
            {
                dcState += (value - dcState) * highPassCoef;
                value -= dcState;
            }

            if (presence != 0.0f)
            {
                shelfState += (value - shelfState) * shelfCoef;
                value += (value - shelfState) * presenceGain;
            }

            if (drive > 0.0f)
                value = std::tanh (value * driveGain) / driveNorm;

            data[i] = value;
        }
    };

    if (view.left != nullptr)
        channel (view.left);

    if (view.right != nullptr)
        channel (view.right);
}

inline void normalize (RenderView view, float targetPeak = 0.891f) noexcept
{
    const auto peak = peakOf (view);

    if (peak <= 0.0f || targetPeak <= 0.0f)
        return;

    const auto scale = targetPeak / peak;

    for (int i = 0; i < view.frames; ++i)
    {
        view.left[i] *= scale;
        view.right[i] *= scale;
    }
}

inline void reverse (RenderView view) noexcept
{
    for (int i = 0, j = view.frames - 1; i < j; ++i, --j)
    {
        std::swap (view.left[i], view.left[j]);
        std::swap (view.right[i], view.right[j]);
    }
}

inline void invert (RenderView view) noexcept
{
    for (int i = 0; i < view.frames; ++i)
    {
        view.left[i] = -view.left[i];
        view.right[i] = -view.right[i];
    }
}

inline void applyGain (RenderView view, float db) noexcept
{
    // An unclamped dB value becomes inf and poisons every downstream stage, so
    // the process clamps to a level a real operator could ask for.
    const auto gain = dbToGain (std::clamp (db, -48.0f, 48.0f));

    for (int i = 0; i < view.frames; ++i)
    {
        view.left[i] *= gain;
        view.right[i] *= gain;
    }
}

/** Zeroes count frames from startFrame, clamped to the buffer. */
inline void silenceRange (RenderView view, int startFrame, int count) noexcept
{
    if (view.frames <= 0 || count <= 0 || startFrame >= view.frames)
        return;

    const auto start = std::max (0, startFrame);
    const auto end = std::min (view.frames, start + count);

    for (int i = start; i < end; ++i)
    {
        view.left[i] = 0.0f;
        view.right[i] = 0.0f;
    }
}

/** Leading and trailing silence around the first audible sample. */
/** First audible frame and the length that follows it. */
inline Trim scanSilence (const float* left, const float* right, int frames,
                         float threshold = 0.003f) noexcept
{
    const auto limit = std::max (0.0f, threshold);
    int first = -1;
    int last = -1;

    for (int i = 0; i < frames; ++i)
    {
        if (std::abs (left[i]) >= limit || std::abs (right[i]) >= limit)
        {
            if (first < 0)
                first = i;

            last = i;
        }
    }

    if (first < 0)
        return {};

    return { first, last - first + 1 };
}

inline Trim stripSilence (RenderView view, float threshold = 0.003f) noexcept
{
    return scanSilence (view.left, view.right, view.frames, threshold);
}

/** One AudioSuite selection: which process, and the settings it runs with. */
struct Process
{
    Kind kind = Kind::normalize;
    LoFiParams lofi {};
    VarFiParams varifi {};
    FastTrackParams fastTrack {};
    float gainDb = 0.0f;
    float targetPeak = 0.891f;
};

inline void runProcess (RenderView view, const Process& process)
{
    switch (process.kind)
    {
        case Kind::lofi:      processLoFi (view, process.lofi); break;
        case Kind::varifi:    processVarFi (view, process.varifi); break;
        case Kind::fastTrack: processFastTrack (view, process.fastTrack); break;
        case Kind::normalize: normalize (view, process.targetPeak); break;
        case Kind::reverse:   reverse (view); break;
        case Kind::invert:    invert (view); break;
        case Kind::gain:      applyGain (view, process.gainDb); break;
        case Kind::silence:
        case Kind::stripSilence:
            break; // range edits are clip operations, not buffer processes
    }
}
} // namespace nc::engine::audiosuite
