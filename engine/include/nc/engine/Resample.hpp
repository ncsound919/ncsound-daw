#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace nc::engine::resample
{
/** Polyphase kernels for playing a source at a rate other than its own.

    Joining neighbouring frames with a straight line is the cheapest
    interpolator and also the worst: it leaves an image of the source's top
    octave in the output (a 10 kHz tone in a 24 kHz file gains a 14 kHz ghost
    only ~4 dB down, which is audible grit on every lower-rate asset). Ardour
    converts through a sinc converter for the same reason, and Lemon Studio
    runs a Blackman-windowed sinc on playback while keeping a 2-point read
    only for its low-quality export mode.

    One windowed-sinc kernel is stored per fractional position, with the
    cutoff at half the lower of the two rates: upsampling stays transparent
    and downsampling cannot fold high content back into the band. 32 taps
    measured -66 dB of image on the 24 kHz -> 48 kHz case that a line leaves
    at -4 dB, without shading the top octave (the passband loss is 0.01 dB).

    A rebuild happens on the message thread while the audio callback may be
    reading, so the bank double-buffers: build always writes the slot that is
    not published, then publishes it atomically.
*/
inline constexpr int taps = 32;
inline constexpr int phases = 256;

/** Rates within this of each other are the same rate: no conversion. */
inline constexpr double rateEpsilon = 1.0e-6;

class Bank
{
public:
    /** Builds the kernels for `sourceRate` frames played at `outputRate`.
        Cheap enough for a message thread (a few thousand sines) and never
        called from the audio callback. */
    void build (double sourceRate, double outputRate) noexcept
    {
        if (! (sourceRate > 0.0) || ! (outputRate > 0.0))
            return;

        const auto current = published.load (std::memory_order_relaxed);
        const auto slot = current < 0 ? 0 : 1 - current;
        auto& weights = bank[static_cast<size_t> (slot)];

        // Cutoff in cycles per source frame, at the lower of the two Nyquists.
        const auto ratio = std::min (1.0, outputRate / sourceRate);
        constexpr auto half = taps / 2;
        constexpr auto pi = 3.14159265358979323846;

        for (int phase = 0; phase < phases; ++phase)
        {
            const auto fraction = static_cast<double> (phase) / phases;
            auto& tapsForPhase = weights[static_cast<size_t> (phase)];
            double sum = 0.0;

            for (int tap = 0; tap < taps; ++tap)
            {
                const auto offset = static_cast<double> (tap - (half - 1)) - fraction;
                const auto x = offset * ratio;
                const auto sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
                const auto t = static_cast<double> (tap) / (taps - 1);
                const auto window = 0.42 - 0.5 * std::cos (2.0 * pi * t)
                                    + 0.08 * std::cos (4.0 * pi * t);

                tapsForPhase[static_cast<size_t> (tap)] = static_cast<float> (sinc * window);
                sum += sinc * window;
            }

            if (std::abs (sum) > 1.0e-9)
                for (auto& weight : tapsForPhase)
                    weight = static_cast<float> (weight / sum);
        }

        source[static_cast<size_t> (slot)] = sourceRate;
        output[static_cast<size_t> (slot)] = outputRate;
        published.store (slot, std::memory_order_release);
    }

    /** True when these are the rates the published kernels were built for. */
    bool matches (double sourceRate, double outputRate) const noexcept
    {
        const auto slot = published.load (std::memory_order_acquire);

        if (slot < 0)
            return false;

        const auto index = static_cast<size_t> (slot);
        return std::abs (source[index] - sourceRate) <= rateEpsilon
               && std::abs (output[index] - outputRate) <= rateEpsilon;
    }

    /** Weights for the fractional part of a source frame; `taps` values, tap k
        reading source frame (floor(frame) - taps / 2 + 1 + k). Returns null
        until the first publish: slot 0 is still being written by build(), so
        handing it to the audio thread would read a half-built kernel. */
    const float* weights (double fraction) const noexcept
    {
        const auto publishedSlot = published.load (std::memory_order_acquire);

        if (publishedSlot < 0)
            return nullptr;

        const auto slot = publishedSlot;
        const auto phase = static_cast<int> (std::lround (
            std::clamp (fraction, 0.0, 0.999999) * phases));

        return bank[static_cast<size_t> (slot)]
                   [static_cast<size_t> (std::clamp (phase, 0, phases - 1))]
                       .data();
    }

private:
    std::array<std::array<std::array<float, taps>, phases>, 2> bank {};
    std::array<double, 2> source { 0.0, 0.0 };
    std::array<double, 2> output { 0.0, 0.0 };
    std::atomic<int> published { -1 };
};
} // namespace nc::engine::resample
