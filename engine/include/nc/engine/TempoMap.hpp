#pragma once

#include <nc/engine/ProjectLimits.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nc::engine
{
/** One tempo change: from `ppq` (quarter notes from the song start)
    onwards, the transport runs at `bpm` until the next point. */
struct TempoPoint
{
    double ppq = 0.0;
    double bpm = 120.0;
};

/** Piecewise tempo map. PPQ is the musical position (1 quarter note == 1);
    seconds are wall-clock position. Conversion integrates segment by
    segment, so a clip or automation point placed in PPQ lands at the right
    sample even after a tempo change. A single-point map is exactly the old
    constant-BPM behaviour, which is what makes v1/v2 projects load as a
    one-point map with `bpm`.

    All functions are pure and finite-guarded: a hostile or hand-edited map
    (unsorted, NaN, out-of-range BPM) is sanitised, never propagated. */
struct TempoMap
{
    std::vector<TempoPoint> points;

    bool empty() const noexcept { return points.empty(); }
    std::size_t size() const noexcept { return points.size(); }
};

/** A map that reproduces the legacy single-BPM transport. */
inline TempoMap constantTempoMap (double bpm) noexcept
{
    TempoMap map;
    map.points.push_back ({ 0.0, nc::limits::clampBpm (std::isfinite (bpm) ? bpm : 120.0) });
    return map;
}

/** Sorts by PPQ, drops non-finite points, clamps BPM, and keeps the first
    point at PPQ 0 (inserting the previous/120 BPM if the earliest point is
    later). Idempotent; caps the point count so a hostile file cannot make
    the audio thread walk an unbounded list. */
inline void sanitizeTempoMap (TempoMap& map)
{
    constexpr std::size_t maxPoints = 1024;

    std::vector<TempoPoint> kept;
    kept.reserve (map.points.size());

    for (const auto point : map.points)
    {
        if (! std::isfinite (point.ppq) || ! std::isfinite (point.bpm))
            continue;

        kept.push_back ({ std::max (0.0, point.ppq), nc::limits::clampBpm (point.bpm) });

        if (kept.size() >= maxPoints)
            break;
    }

    std::sort (kept.begin(), kept.end(),
               [] (const TempoPoint& a, const TempoPoint& b) { return a.ppq < b.ppq; });

    // Duplicate PPQs: the later entry wins, earlier ones are unreachable.
    std::vector<TempoPoint> deduped;
    deduped.reserve (kept.size());

    for (const auto point : kept)
    {
        if (! deduped.empty() && deduped.back().ppq == point.ppq)
            deduped.back() = point;
        else
            deduped.push_back (point);
    }

    if (deduped.empty())
    {
        deduped.push_back ({ 0.0, 120.0 });
    }
    else if (deduped.front().ppq > 0.0)
    {
        deduped.insert (deduped.begin(), { 0.0, deduped.front().bpm });
    }

    map.points = std::move (deduped);
}

/** BPM in force at a PPQ position. Empty maps report 120. */
inline double bpmAt (const TempoMap& map, double ppq) noexcept
{
    if (map.points.empty())
        return 120.0;

    const auto position = std::isfinite (ppq) ? std::max (0.0, ppq) : 0.0;
    double bpm = map.points.front().bpm;

    for (const auto& point : map.points)
    {
        if (point.ppq > position)
            break;
        bpm = point.bpm;
    }

    return bpm;
}

/** Seconds at a PPQ position, integrating each tempo segment. */
inline double ppqToSeconds (const TempoMap& map, double ppq) noexcept
{
    if (map.points.empty() || ! std::isfinite (ppq) || ppq <= 0.0)
        return 0.0;

    double seconds = 0.0;
    double previousPpq = 0.0;

    for (const auto& point : map.points)
    {
        if (point.ppq <= previousPpq || point.ppq >= ppq)
            continue;

        const auto bpm = bpmAt (map, previousPpq);
        const auto safeBpm = bpm > 0.0 ? bpm : 120.0;
        seconds += (point.ppq - previousPpq) * (60.0 / safeBpm);
        previousPpq = point.ppq;
    }

    const auto lastBpm = bpmAt (map, previousPpq);
    seconds += (ppq - previousPpq) * (60.0 / (lastBpm > 0.0 ? lastBpm : 120.0));
    return seconds;
}

/** PPQ at a seconds position; the inverse of ppqToSeconds. */
inline double secondsToPpq (const TempoMap& map, double seconds) noexcept
{
    if (map.points.empty() || ! std::isfinite (seconds) || seconds <= 0.0)
        return 0.0;

    double elapsed = 0.0;
    double previousPpq = 0.0;

    for (const auto& point : map.points)
    {
        if (point.ppq <= previousPpq)
            continue;

        // The segment is billed at the tempo in force at its start, matching
        // ppqToSeconds; using the ending point's BPM here would make the
        // two conversions non-inverses after a tempo change.
        const auto segmentBpm = bpmAt (map, previousPpq);
        const auto safeBpm = segmentBpm > 0.0 ? segmentBpm : 120.0;
        const auto segmentSeconds = (point.ppq - previousPpq) * (60.0 / safeBpm);

        if (elapsed + segmentSeconds > seconds)
            break;

        elapsed += segmentSeconds;
        previousPpq = point.ppq;
    }

    const auto bpm = bpmAt (map, previousPpq);
    return previousPpq + (seconds - elapsed) * ((bpm > 0.0 ? bpm : 120.0) / 60.0);
}

/** Samples at a PPQ position at a given rate: ppqToSeconds * rate. */
inline double samplesAtPpqWithMap (const TempoMap& map, double ppq, double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || ! std::isfinite (sampleRate))
        return 0.0;

    return ppqToSeconds (map, ppq) * sampleRate;
}

/** PPQ at a sample position at a given rate. */
inline double ppqAtSamplesWithMap (const TempoMap& map, int64_t samples, double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || ! std::isfinite (sampleRate) || samples <= 0)
        return 0.0;

    return secondsToPpq (map, static_cast<double> (samples) / sampleRate);
}
} // namespace nc::engine
