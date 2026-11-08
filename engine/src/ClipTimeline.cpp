#include <nc/engine/ClipTimeline.hpp>

#include <algorithm>
#include <cmath>

namespace nc::engine
{
double secondsPerBar (double bpm, int beatsPerBar) noexcept
{
    // Non-finite tempo never reaches the audio math as NaN/inf: fall back to
    // 120 BPM so position mapping stays finite and the transport keeps time.
    const auto safeBpm = std::isfinite (bpm) ? std::max (1.0, bpm) : 120.0;
    const auto safeBeats = std::max (1, beatsPerBar);
    return static_cast<double> (safeBeats) * 60.0 / safeBpm;
}

double barToSeconds (double bar, double bpm, int beatsPerBar) noexcept
{
    return std::max (0.0, bar - 1.0) * secondsPerBar (bpm, beatsPerBar);
}

bool clipActiveAt (double timelineSeconds,
                   double startSeconds,
                   double lengthSeconds) noexcept
{
    // Any non-finite bound means "not playing here", never "playing everywhere".
    if (! std::isfinite (timelineSeconds)
        || ! std::isfinite (startSeconds)
        || ! std::isfinite (lengthSeconds))
        return false;
    return lengthSeconds > 0.0
        && timelineSeconds >= startSeconds
        && timelineSeconds < startSeconds + lengthSeconds;
}

double sourceSecondsAt (double timelineSeconds,
                        double startSeconds,
                        double sourceOffsetSeconds) noexcept
{
    const auto safeOffset = std::isfinite (sourceOffsetSeconds) ? sourceOffsetSeconds : 0.0;
    if (! std::isfinite (timelineSeconds) || ! std::isfinite (startSeconds))
        return std::max (0.0, safeOffset);
    return std::max (0.0, safeOffset + std::max (0.0, timelineSeconds - startSeconds));
}

double clipGainAt (double localSeconds,
                   double lengthSeconds,
                   double fadeInSeconds,
                   double fadeOutSeconds) noexcept
{
    // NaN cursor must stay silent: comparisons against NaN are false, so check
    // finiteness first instead of relying on relational guards alone.
    if (! std::isfinite (localSeconds) || ! std::isfinite (lengthSeconds))
        return 0.0;
    if (lengthSeconds <= 0.0 || localSeconds < 0.0 || localSeconds >= lengthSeconds)
        return 0.0;

    // Fades never consume more than 45% of the clip (the split-edge rule in
    // ClipEdit and Comping::clampCrossfadeBars). Clamping here means every
    // playback path honours it even if a caller passes a longer fade.
    const auto fadeCap = std::max (0.0, lengthSeconds * 0.45);
    // Non-finite fades degrade to a hard cut rather than poisoning the gain.
    const auto safeFadeIn = std::min (std::isfinite (fadeInSeconds) ? fadeInSeconds : 0.0, fadeCap);
    const auto safeFadeOut = std::min (std::isfinite (fadeOutSeconds) ? fadeOutSeconds : 0.0, fadeCap);

    auto gain = 1.0;

    if (safeFadeIn > 0.0)
        gain = std::min (gain, localSeconds / safeFadeIn);

    if (safeFadeOut > 0.0)
        gain = std::min (gain, (lengthSeconds - localSeconds) / safeFadeOut);

    return std::isfinite (gain) ? std::clamp (gain, 0.0, 1.0) : 0.0;
}

double clipGainCrossfadeAt (double localSeconds,
                            double lengthSeconds,
                            double fadeInSeconds,
                            double fadeOutSeconds) noexcept
{
    if (! std::isfinite (localSeconds) || ! std::isfinite (lengthSeconds))
        return 0.0;
    if (lengthSeconds <= 0.0 || localSeconds < 0.0 || localSeconds >= lengthSeconds)
        return 0.0;

    const auto fadeCap = std::max (0.0, lengthSeconds * 0.45);
    const auto safeFadeIn = std::min (std::isfinite (fadeInSeconds) ? fadeInSeconds : 0.0, fadeCap);
    const auto safeFadeOut = std::min (std::isfinite (fadeOutSeconds) ? fadeOutSeconds : 0.0, fadeCap);

    constexpr double halfPi = 1.5707963267948966;
    auto gain = 1.0;

    if (safeFadeIn > 0.0)
    {
        const auto t = std::clamp (localSeconds / safeFadeIn, 0.0, 1.0);
        gain = std::min (gain, std::sin (t * halfPi));
    }

    if (safeFadeOut > 0.0)
    {
        // Remaining fraction: 1 at fade start, 0 at the clip end.
        // Equal-power outgoing gain is cos(progress*PI/2) == sin(remaining*PI/2).
        const auto remaining = std::clamp ((lengthSeconds - localSeconds) / safeFadeOut, 0.0, 1.0);
        gain = std::min (gain, std::sin (remaining * halfPi));
    }

    return std::isfinite (gain) ? std::clamp (gain, 0.0, 1.0) : 0.0;
}
} // namespace nc::engine
