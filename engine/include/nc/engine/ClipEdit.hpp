#pragma once

#include <nc/engine/ClipTimeline.hpp>
#include <nc/engine/Comping.hpp>
#include <nc/engine/ProjectState.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace nc::engine
{
namespace detail
{
/** Keeps the three edge envelopes inside the length after a trim, matching
    sanitize(): fades and crossfade may never exceed 45% of the clip. */
inline void clampClipEdges (ClipState& clip) noexcept
{
    const auto limit = clip.lengthBars * 0.45;
    const auto fix = [limit] (double value) {
        return std::isfinite (value) ? std::clamp (value, 0.0, limit) : 0.0;
    };

    clip.fadeInBars = fix (clip.fadeInBars);
    clip.fadeOutBars = fix (clip.fadeOutBars);
    clip.crossfadeBars = fix (clip.crossfadeBars);
}
} // namespace detail

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

    const auto original = clips[index];
    const auto endBar = original.startBar + original.lengthBars;

    if (atBar <= original.startBar || atBar >= endBar)
        return false;

    const auto leftLength = atBar - original.startBar;
    const auto rightLength = original.lengthBars - leftLength;

    if (leftLength <= 0.0 || rightLength <= 0.0)
        return false;

    ClipState left = original;
    ClipState right = original;

    right.startBar = atBar;
    right.lengthBars = rightLength;
    right.sourceOffsetSeconds = original.sourceOffsetSeconds
                              + leftLength * secondsPerBar (bpm);
    right.fadeInBars = 0.0;
    right.crossfadeBars = 0.0; // split edge: never inherit a crossfade
    right.fadeOutBars = std::min (original.fadeOutBars, rightLength * 0.45);

    left.lengthBars = leftLength;
    left.fadeOutBars = 0.0;
    left.crossfadeBars = 0.0; // split edge: never inherit a crossfade
    left.fadeInBars = std::min (original.fadeInBars, leftLength * 0.45);

    // MIDI content is timeline-relative: copying the note/CC/bend lists into
    // both halves would double every note. Distribute, rebase the right half,
    // and truncate notes that straddle the split.
    if (original.isMidi)
    {
        const auto splitBeat = leftLength * beatsPerBarMidi;
        const auto leftBeats = leftLength * beatsPerBarMidi;
        const auto rightBeats = rightLength * beatsPerBarMidi;

        left.notes.clear();
        right.notes.clear();
        left.ccs.clear();
        right.ccs.clear();
        left.bends.clear();
        right.bends.clear();

        for (const auto& note : original.notes)
        {
            const auto noteEnd = note.startBeat + note.lengthBeats;

            if (noteEnd <= splitBeat)
            {
                left.notes.push_back (note);
            }
            else if (note.startBeat >= splitBeat)
            {
                auto moved = note;
                moved.startBeat = note.startBeat - splitBeat;
                right.notes.push_back (moved);
            }
            else
            {
                auto head = note;
                head.lengthBeats = splitBeat - note.startBeat;
                left.notes.push_back (head);

                auto tail = note;
                tail.startBeat = 0.0;
                tail.lengthBeats = noteEnd - splitBeat;
                right.notes.push_back (tail);
            }
        }

        for (const auto& cc : original.ccs)
        {
            if (cc.beat < splitBeat)
            {
                left.ccs.push_back (cc);
            }
            else
            {
                auto moved = cc;
                moved.beat = cc.beat - splitBeat;
                right.ccs.push_back (moved);
            }
        }

        for (const auto& bend : original.bends)
        {
            if (bend.beat < splitBeat)
            {
                left.bends.push_back (bend);
            }
            else
            {
                auto moved = bend;
                moved.beat = bend.beat - splitBeat;
                right.bends.push_back (moved);
            }
        }

        sanitizeNotes (left.notes, leftBeats);
        sanitizeNotes (right.notes, rightBeats);
        sanitizeMidiCCs (left.ccs, leftBeats);
        sanitizeMidiCCs (right.ccs, rightBeats);
        sanitizeMidiBends (left.bends, leftBeats);
        sanitizeMidiBends (right.bends, rightBeats);
    }

    clips[index] = std::move (left);
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
    detail::clampClipEdges (clip);
    return true;
}

/** Trims the tail of a clip by `deltaBars` (positive = shorter). */
inline bool trimClipRight (std::vector<ClipState>& clips, std::size_t index, double deltaBars)
{
    if (index >= clips.size() || ! std::isfinite (deltaBars))
        return false;

    auto& clip = clips[index];
    // Growing the tail may not push the clip past the project ceiling that
    // sanitize() enforces on load; shortening is bounded by the clip itself.
    const auto room = std::max (0.0, static_cast<double> (nc::limits::maxBars) - clip.lengthBars);
    // Negative delta grows the tail: cap the growth at the headroom left
    // before the session ceiling. Positive delta shortens: never past zero.
    const auto delta = std::clamp (deltaBars, -room, clip.lengthBars - 1.0e-6);

    if (delta == 0.0)
        return false;

    clip.lengthBars -= delta;
    detail::clampClipEdges (clip);
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

/** Sets an equal-power crossfade on a clip, clamped to 45% of its length
    (the split-edge rule). 0 removes the crossfade; a negative value is
    rejected rather than silently read as 0. */
inline bool setClipCrossfade (std::vector<ClipState>& clips, std::size_t index, double bars)
{
    if (index >= clips.size() || ! std::isfinite (bars) || bars < 0.0)
        return false;

    auto& clip = clips[index];
    clip.crossfadeBars = std::clamp (bars, 0.0, clip.lengthBars * 0.45);
    return true;
}

/** Selects the active comp take (0 = first), clamped to the comping ceiling. */
inline bool selectCompTake (std::vector<ClipState>& clips, std::size_t index, int take)
{
    if (index >= clips.size())
        return false;

    clips[index].compTake = std::clamp (take, 0,
                                        static_cast<int> (comping::CompLane::maxTakes) - 1);
    return true;
}

/** Cycles the active comp take by `delta` (wraps within 0..takeCount-1). */
inline bool cycleCompTake (std::vector<ClipState>& clips, std::size_t index, int delta, int takeCount)
{
    const auto count = std::min (takeCount, static_cast<int> (comping::CompLane::maxTakes));

    if (index >= clips.size() || count <= 0)
        return false;

    const auto current = std::clamp (clips[index].compTake, 0, count - 1);
    auto next = (current + delta) % count;
    if (next < 0)
        next += count;

    clips[index].compTake = next;
    return true;
}
} // namespace nc::engine
