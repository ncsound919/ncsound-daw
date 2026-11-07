#pragma once

namespace nc::engine
{
/** Timeline math shared by the audio engine and offline tests.

    Bars are 1-based. A clip at bar 1 starts at sample 0. The helpers are pure
    so the audio callback and the test suite use exactly the same mapping.
*/
inline constexpr int defaultBeatsPerBar = 4;

double secondsPerBar (double bpm, int beatsPerBar = defaultBeatsPerBar) noexcept;
double barToSeconds (double bar, double bpm, int beatsPerBar = defaultBeatsPerBar) noexcept;

bool clipActiveAt (double timelineSeconds,
                   double startSeconds,
                   double lengthSeconds) noexcept;

double sourceSecondsAt (double timelineSeconds,
                        double startSeconds,
                        double sourceOffsetSeconds) noexcept;

/** Clips accepted from a requested schedule length under a fixed capacity.

    Pure so the message thread and the test suite agree on what the audio
    callback will hold: the head of the timeline plays, the tail is reported
    as dropped instead of wrapping or allocating.
*/
inline constexpr int truncateClipCount (int requested, int capacity) noexcept
{
    if (requested <= 0 || capacity <= 0)
        return 0;
    return requested < capacity ? requested : capacity;
}

/** Linear clip gain including the fade-in and fade-out ramps.

    Returns 0 outside the clip. At a zero-length fade the edge is a hard cut.
*/
double clipGainAt (double localSeconds,
                   double lengthSeconds,
                   double fadeInSeconds,
                   double fadeOutSeconds) noexcept;
} // namespace nc::engine
