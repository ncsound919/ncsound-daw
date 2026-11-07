#pragma once

#include <algorithm>
#include <cstdint>

namespace nc::engine
{
/** A half-open run of source frames, in the asset's own sample rate.

    Streaming keeps only a bounded window resident. These helpers are pure so
    the reader (message thread) and the mixer (audio callback) agree on what
    must already be in memory before a clip can sound.
*/
struct StreamWindow
{
    int64_t startFrame = 0;
    int64_t lengthFrames = 0;

    constexpr bool isEmpty() const noexcept { return lengthFrames <= 0; }
};

/** Frames left after positionFrames, clamped to zero outside the asset. */
inline constexpr int64_t framesRemaining (int64_t positionFrames, int64_t totalFrames) noexcept
{
    if (totalFrames <= 0)
        return 0;

    const auto position = std::max<int64_t> (0, positionFrames);
    return position >= totalFrames ? 0 : totalFrames - position;
}

/** Window to keep resident for a cursor at positionFrames: never past the end. */
inline constexpr StreamWindow planWindow (int64_t positionFrames,
                                          int64_t totalFrames,
                                          int64_t readAheadFrames) noexcept
{
    const auto start = std::max<int64_t> (0, positionFrames);
    const auto want = std::max<int64_t> (0, readAheadFrames);

    return { start, std::min (want, framesRemaining (start, totalFrames)) };
}

/** True when the resident window already holds the requested frame range. */
inline constexpr bool windowCovers (const StreamWindow& resident,
                                   int64_t positionFrames,
                                   int64_t neededFrames) noexcept
{
    if (neededFrames <= 0)
        return true;

    if (resident.lengthFrames <= 0)
        return false;

    return positionFrames >= resident.startFrame
        && positionFrames + neededFrames <= resident.startFrame + resident.lengthFrames;
}

/** The read a background pass should issue, or an empty window when covered. */
inline constexpr StreamWindow planRead (const StreamWindow& resident,
                                       const StreamWindow& target) noexcept
{
    if (target.isEmpty() || windowCovers (resident, target.startFrame, target.lengthFrames))
        return {};

    return target;
}
} // namespace nc::engine
