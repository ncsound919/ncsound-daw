#pragma once

#include <nc/engine/ProjectLimits.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace nc::engine
{
/** A named song position (INTRO, VERSE, ...). PPQ is quarter notes from
    the song start, so a marker tracks the music across tempo changes
    instead of drifting with wall-clock seconds. */
struct Marker
{
    double ppq = 0.0;
    std::string name;
};

/** Sorts by PPQ, drops non-finite positions, clamps to the bar ceiling,
    caps name length, and caps the count. Idempotent. */
inline void sanitizeMarkers (std::vector<Marker>& markers)
{
    constexpr std::size_t maxMarkers = 256;
    constexpr std::size_t maxNameLength = 64;

    const auto maxPpq = static_cast<double> (nc::limits::maxBars) * 4.0;

    std::vector<Marker> kept;
    kept.reserve (markers.size());

    for (auto marker : markers)
    {
        if (! std::isfinite (marker.ppq))
            continue;

        marker.ppq = std::clamp (marker.ppq, 0.0, maxPpq);

        if (marker.name.size() > maxNameLength)
            marker.name.resize (maxNameLength);

        kept.push_back (std::move (marker));

        if (kept.size() >= maxMarkers)
            break;
    }

    std::sort (kept.begin(), kept.end(),
               [] (const Marker& a, const Marker& b) { return a.ppq < b.ppq; });

    markers = std::move (kept);
}

/** First marker at or after a PPQ position, or nullptr. For "next section"
    transport jumps. */
inline const Marker* markerAtOrAfter (const std::vector<Marker>& markers, double ppq) noexcept
{
    if (! std::isfinite (ppq))
        return nullptr;

    for (const auto& marker : markers)
        if (marker.ppq >= ppq)
            return &marker;

    return nullptr;
}
} // namespace nc::engine
