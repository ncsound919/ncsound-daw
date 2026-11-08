#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace nc::engine::comping
{
/** Crossfade curves + comp-take model. Header-only and JUCE-free.

    Crossfades: Pro Tools / Cubase ship two curves.
      - equal-power (sin/cos): constant loudness through the overlap, the
        default for music because linear crossfades dip ~3-6 dB mid-overlap.
      - linear (equal-gain): dip is acceptable for short, correlated joins.
    This header owns the math; `ClipTimeline::clipGainCrossfadeAt` applies it
    to a clip's edge. Callers clamp the overlap with clampCrossfadeBars().

    Comping: a lane holds recorded takes (never destroyed). Playback renders
    only the active take; switching takes is non-destructive. The clip stores
    the active take index (`ClipState::compTake`); this header is the list
    logic (add/select/cycle) the UI drives.
*/

enum class Curve
{
    linear,
    equalPower
};

inline const char* curveName (Curve curve) noexcept
{
    return curve == Curve::equalPower ? "equal-power" : "linear";
}

/** Gains (outgoing, incoming) at normalised overlap position t in [0, 1].
    Linear: (1-t, t). Equal-power: (cos(t*PI/2), sin(t*PI/2)) — at t=0.5 both
    are ~0.707, so uncorrelated signals hold power instead of dipping. */
inline std::pair<double, double> crossfadeGains (double t, Curve curve) noexcept
{
    if (! std::isfinite (t))
        return { 1.0, 0.0 };

    const auto clamped = std::clamp (t, 0.0, 1.0);

    if (curve == Curve::equalPower)
    {
        constexpr double halfPi = 1.5707963267948966;
        return { std::cos (clamped * halfPi), std::sin (clamped * halfPi) };
    }

    return { 1.0 - clamped, clamped };
}

/** Gain for an incoming clip at normalised position t. */
inline double incomingGain (double t, Curve curve) noexcept
{
    return crossfadeGains (t, curve).second;
}

/** Gain for an outgoing clip at normalised position t. */
inline double outgoingGain (double t, Curve curve) noexcept
{
    return crossfadeGains (t, curve).first;
}

/** Clamp a requested crossfade to what the two neighbours can overlap.
    Mirrors the split-edge rule in ClipEdit (45% of the shorter clip) so a
    crossfade can never consume a clip outright. */
inline double clampCrossfadeBars (double requestedBars, double leftLengthBars,
                                 double rightLengthBars) noexcept
{
    if (! std::isfinite (requestedBars) || requestedBars <= 0.0)
        return 0.0;

    const auto budget = std::min (leftLengthBars, rightLengthBars) * 0.45;
    return std::clamp (requestedBars, 0.0, std::max (0.0, budget));
}

/** Overlap in bars between [aStart, aStart+aLen) and [bStart, bStart+bLen). */
inline double overlapBars (double aStart, double aLen, double bStart, double bLen) noexcept
{
    const auto start = std::max (aStart, bStart);
    const auto end = std::min (aStart + aLen, bStart + bLen);
    return std::max (0.0, end - start);
}

//==============================================================================
// Comp takes
//==============================================================================

struct Take
{
    int index = 0;
    std::string name;
    double gainDb = 0.0;
    bool active = false;
};

struct CompLane
{
    std::vector<Take> takes;
    int activeIndex = 0;

    /** Adds a take (never removes). Returns its index, or -1 when the lane is
        full. Duplicates are legal: punches of the same passage are distinct
        takes. A non-finite gain would reach playback, so it becomes 0 dB. */
    int addTake (std::string name, double gainDb = 0.0)
    {
        if (takes.size() >= maxTakes)
            return -1;

        Take take;
        take.index = static_cast<int> (takes.size());
        take.name = std::move (name);
        take.gainDb = std::isfinite (gainDb) ? gainDb : 0.0;
        take.active = takes.empty();
        takes.push_back (std::move (take));

        if (takes.size() == 1)
            activeIndex = 0;

        return static_cast<int> (takes.size()) - 1;
    }

    /** Selects by lane position. Take::index is display metadata; a hand-built
        or migrated lane may have mismatched indices, and selecting by it would
        silently select nothing. */
    bool selectTake (int index) noexcept
    {
        if (index < 0 || index >= static_cast<int> (takes.size()))
            return false;

        for (std::size_t i = 0; i < takes.size(); ++i)
            takes[i].active = static_cast<int> (i) == index;

        activeIndex = index;
        return true;
    }

    /** Cycles the active take by `delta` (wraps). False when there are no takes. */
    bool cycleTake (int delta) noexcept
    {
        if (takes.empty())
            return false;

        const auto count = static_cast<int> (takes.size());
        const auto current = std::clamp (activeIndex, 0, count - 1);
        auto next = current + delta;
        next %= count;
        if (next < 0)
            next += count;

        return selectTake (next);
    }

    const Take* activeTake() const noexcept
    {
        for (const auto& take : takes)
            if (take.active)
                return &take;

        return takes.empty() ? nullptr : &takes.front();
    }

    static constexpr std::size_t maxTakes = 32;
};

} // namespace nc::engine::comping
