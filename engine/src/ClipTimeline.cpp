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

    // Non-finite fades degrade to a hard cut rather than poisoning the gain.
    const auto safeFadeIn = std::isfinite (fadeInSeconds) ? fadeInSeconds : 0.0;
    const auto safeFadeOut = std::isfinite (fadeOutSeconds) ? fadeOutSeconds : 0.0;

    auto gain = 1.0;

    if (safeFadeIn > 0.0)
        gain = std::min (gain, localSeconds / safeFadeIn);

    if (safeFadeOut > 0.0)
        gain = std::min (gain, (lengthSeconds - localSeconds) / safeFadeOut);

    return std::isfinite (gain) ? std::clamp (gain, 0.0, 1.0) : 0.0;
}
} // namespace nc::engine
