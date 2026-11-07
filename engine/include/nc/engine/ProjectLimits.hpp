#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace nc::limits
{
// Central ceiling for song projects. The preview build capped sessions at
// 8 tracks / 256 clips / 128 bars / 10-minute RAM assets. These raised
// ceilings keep the real-time guarantees (no alloc/lock/I-O on the audio
// callback) while leaving headroom for real songs. Streaming replaces the
// RAM-asset cap when long projects matter.
//
// Chosen so: commandCapacity > maxClips (clear + all adds fit the SPSC rail),
// maxBars covers a 10-minute song at 120 BPM (~150 bars) with margin, and
// maxAssetSeconds covers a 30-minute take at 48 kHz stereo (~330 MB float,
// still bounded and explicit).
inline constexpr int maxTracks = 64;
inline constexpr int maxClips = 1024;
inline constexpr int maxMidiNotesPerClip = 4096;
inline constexpr int64_t maxMidiNotesTotal = 65536;
inline constexpr int commandCapacity = 4096;
inline constexpr int maxBars = 1024;
inline constexpr int maxAssetSeconds = 30 * 60;
inline constexpr double minBpm = 20.0;
inline constexpr double maxBpm = 300.0;

// A project file is untrusted input. Nesting is capped at the same depth JUCE
// uses for its own JSON parser (256), so a hostile file cannot exhaust the
// recursive-descent stack. The byte and value caps bound the allocations that
// happen before sanitise() can apply the track/clip/insert ceilings; a real
// project with hosted-plugin state sits far below all three.
inline constexpr int maxJsonNestingDepth = 256;
inline constexpr int64_t maxProjectJsonBytes = 256 * 1024 * 1024;
inline constexpr int maxJsonValues = 1'000'000;

inline constexpr double clampBpm (double bpm) noexcept
{
    return std::clamp (bpm, minBpm, maxBpm);
}

inline constexpr int clampTrack (int track, int trackCount) noexcept
{
    return std::clamp (track, 0, trackCount - 1);
}

inline constexpr bool isTrackValid (int track, int trackCount) noexcept
{
    return track >= 0 && track < trackCount;
}

// Bounded RAM-asset cap: 30 minutes at the file's own rate, clamped to the
// 8 kHz..192 kHz sanity band so a corrupt header can never size a 2 GB
// buffer. 192 kHz * 1800 s = 345.6 M frames, well inside int64 and int32.
inline constexpr int64_t maxAssetFrames (double sampleRate) noexcept
{
    const auto safeRate = std::clamp (sampleRate, 8000.0, 192000.0);
    return static_cast<int64_t> (safeRate * static_cast<double> (maxAssetSeconds));
}

// Rows a session starts with. Tracks beyond this are real but empty until the
// user adds them; the 64-track ceiling is the same one the engine allocates.
inline constexpr int defaultActiveTracks = 16;

inline constexpr int clampActiveTracks (int count) noexcept
{
    return std::clamp (count, 1, maxTracks);
}

inline constexpr int nextActiveTracks (int current, int delta) noexcept
{
    return clampActiveTracks (current + delta);
}

// Imports longer than this stream from disk instead of occupying RAM; the
// resident read-ahead window is a few seconds, so a long take stays cheap.
inline constexpr double streamThresholdSeconds = 60.0;
inline constexpr double streamReadAheadSeconds = 4.0;

// Startup plugin scan is bounded: a runaway or corrupt registry is rejected
// rather than parsed, so a bad file cannot stall the message thread.
inline constexpr int64_t maxRegistryBytes = 4 * 1024 * 1024;

inline constexpr bool registrySizeAcceptable (int64_t bytes) noexcept
{
    return bytes > 0 && bytes <= maxRegistryBytes;
}

inline constexpr int64_t clampAssetFrames (int64_t requestedFrames, double sampleRate) noexcept
{
    if (requestedFrames <= 0)
        return 0;
    const auto cap = maxAssetFrames (sampleRate);
    return requestedFrames < cap ? requestedFrames : cap;
}
} // namespace nc::limits
