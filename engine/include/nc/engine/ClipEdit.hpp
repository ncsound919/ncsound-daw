#pragma once

#include <nc/engine/ClipTimeline.hpp>
#include <nc/engine/ProjectState.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace nc::engine
{
/** Pure clip-edit operations on the serialisable clip list. These used to
    live inside the arrange view (int bars, UI-owned), which made them
    untestable and sample-inaccurate. Here they work on ClipState
    (fractional bars) so the view, the undo stack, and the tests all share
    one implementation.

    All operations clamp to the same ceilings sanitize() enforces and are
    no-ops (return false, list unchanged) for out-of-range input. */

/** Splits the clip at `index` at `atBar` (must lie strictly inside it).
    The right-hand clip keeps the same asset/notes with sourceOffsetSeconds
    advanced by the split distance in seconds (bars * secondsPerBar at
    `bpm`); the split edges lose their fades and the outer fades are
    clamped to the new lengths, matching ArrangeView::splitClip. Returns
    false if the split point is at/over either edge. */
inline bool splitClipAt (std::vector<ClipState>& clips, std::size_t index, double atBar,
                         double bpm = 120.0)
{
    if (index >= clips.size() || ! std::isfinite (atBar))
        return false;

    const auto& clip = clips[index];
    const auto endBar = clip.startBar + clip.lengthBars;

    if (atBar <= clip.startBar || atBar >= endBar)
        return false;

    const auto leftLength = atBar - clip.startBar;
    const auto rightLength = clip.lengthBars - leftLength;

    if (leftLength <= 0.0 || rightLength <= 0.0)
        return false;

    ClipState right = clip;
    right.startBar = atBar;
    right.lengthBars = rightLength;
    right.sourceOffsetSeconds = clip.sourceOffsetSeconds
                              + leftLength * secondsPerBar (bpm);
    right.fadeInBars = 0.0;
    right.fadeOutBars = std::min (clip.fadeOutBars, rightLength * 0.45);

    clips[index].lengthBars = leftLength;
    clips[index].fadeOutBars = 0.0;
    clips[index].fadeInBars = std::min (clip.fadeInBars, leftLength * 0.45);
    clips.insert (clips.begin() + static_cast<std::ptrdiff_t> (index + 1), std::move (right));
    return true;
}

/** Moves a clip to a new bar/track, clamped to the session ceilings. */
inline bool moveClip (std::vector<ClipState>& clips, std::size_t index,
                      double newStartBar, int newTrack, int activeTrackCount)
{
    if (index >= clips.size() || ! std::isfinite (newStartBar))
        return false;

    const auto maxBar = static_cast<double> (nc::limits::maxBars);
    const auto lastTrack = std::max (0, activeTrackCount - 1);

    clips[index].startBar = std::clamp (newStartBar, 1.0, maxBar);
    clips[index].track = std::clamp (newTrack, 0, lastTrack);
    return true;
}

/** Trims the head of a clip by `deltaBars` (positive = later start).
    Advances sourceOffsetSeconds by the same distance in seconds so the
    audible source stays aligned. Returns false if the trim would consume
    the whole clip. */
inline bool trimClipLeft (std::vector<ClipState>& clips, std::size_t index, double deltaBars,
                          double bpm = 120.0)
{
    if (index >= clips.size() || ! std::isfinite (deltaBars))
        return false;

    auto& clip = clips[index];
    const auto delta = std::clamp (deltaBars, -clip.startBar + 1.0, clip.lengthBars - 1.0e-6);

    if (delta == 0.0)
        return false;

    clip.startBar += delta;
    clip.lengthBars -= delta;
    clip.sourceOffsetSeconds = std::max (0.0, clip.sourceOffsetSeconds + delta * secondsPerBar (bpm));
    return true;
}

/** Trims the tail of a clip by `deltaBars` (positive = shorter). */
inline bool trimClipRight (std::vector<ClipState>& clips, std::size_t index, double deltaBars)
{
    if (index >= clips.size() || ! std::isfinite (deltaBars))
        return false;

    auto& clip = clips[index];
    const auto delta = std::clamp (deltaBars, -(clip.lengthBars - 1.0e-6), clip.lengthBars - 1.0e-6);

    if (delta == 0.0)
        return false;

    clip.lengthBars -= delta;
    return true;
}

/** Deletes the clip at `index`. The caller pushes an undo snapshot first. */
inline bool deleteClipAt (std::vector<ClipState>& clips, std::size_t index)
{
    if (index >= clips.size())
        return false;

    clips.erase (clips.begin() + static_cast<std::ptrdiff_t> (index));
    return true;
}
} // namespace nc::engine
