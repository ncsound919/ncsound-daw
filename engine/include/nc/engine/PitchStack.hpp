#pragma once

// Pitch tuning shared across global / track / instrument scopes.
//
// Ported from ChordStudio Source/audio/PitchStack.h (pure C++, no JUCE).
// The atomics live one layer up (AudioEngine / TrackInsertRack / HostedInsert)
// following the gain/pan pattern; this struct is the maths.

#include <algorithm>
#include <cmath>

namespace nc::engine
{
    // One scope's three knobs.
    struct PitchStack
    {
        static constexpr int kTransposeMin = -48;
        static constexpr int kTransposeMax = 48;
        static constexpr float kPitchMinCents = -100.0f;
        static constexpr float kPitchMaxCents = 100.0f;
        static constexpr float kHzMin = 415.0f;
        static constexpr float kHzMax = 466.0f;
        static constexpr float kHzDefault = 440.0f;

        int transpose { 0 };        // semitones
        float pitchCents { 0.0f };  // fine pitch, cents
        float tuningHz { kHzDefault }; // A4 reference

        // Clamp to valid ranges (hostile project values).
        void clamp() noexcept
        {
            if (! std::isfinite (pitchCents))
                pitchCents = 0.0f;
            if (! std::isfinite (tuningHz))
                tuningHz = kHzDefault;
            transpose = std::clamp (transpose, kTransposeMin, kTransposeMax);
            pitchCents = std::clamp (pitchCents, kPitchMinCents, kPitchMaxCents);
            tuningHz = std::clamp (tuningHz, kHzMin, kHzMax);
        }

        // Hz -> cents offset from A440.
        static float hzToCents (float hz) noexcept
        {
            if (! std::isfinite (hz) || hz <= 0.0f)
                return 0.0f;
            hz = std::clamp (hz, kHzMin, kHzMax);
            return 1200.0f * std::log2 (hz / 440.0f);
        }

        // Effective cents for this scope alone.
        float cents() const noexcept
        {
            return transpose * 100.0f + pitchCents + hzToCents (tuningHz);
        }
    };

    // Global + track + instrument summed.
    inline float sumCents (const PitchStack& g, const PitchStack& t,
                           const PitchStack& i) noexcept
    {
        return g.cents() + t.cents() + i.cents();
    }

    inline float pitchRatio (float totalCents) noexcept
    {
        return std::pow (2.0f, totalCents / 1200.0f);
    }
} // namespace nc::engine
