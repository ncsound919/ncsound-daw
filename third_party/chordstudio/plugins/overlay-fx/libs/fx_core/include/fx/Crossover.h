#pragma once

// Phase-coherent multiband splitter: 1..4 bands, Linkwitz-Riley 24 dB/oct.
//
// Bands are taken off one after another (low band first). Each LR4 low/high
// pair is in phase with the 2nd-order all-pass that equals their sum, so every
// band gets the all-passes of the splits it did not go through. The bands then
// add back to the input through one fixed all-pass: flat magnitude, no comb.
// Double precision throughout (low crossovers at high sample rates need it).

#include <algorithm>
#include <array>
#include <cmath>

namespace fx
{
    class Crossover
    {
    public:
        static constexpr int maxBands = 4;
        static constexpr int maxChannels = 2;

        void prepare (double sampleRate) noexcept
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            setBands (numBands, freqs[0], freqs[1], freqs[2], true);
            reset();
        }

        void reset() noexcept
        {
            for (auto& ch : state)
                for (auto& s : ch)
                    s = {};
        }

        // `bands` 1..4; the first bands-1 frequencies are used, kept ascending
        // and at least a third of an octave apart.
        void setBands (int bands, float f1, float f2, float f3, bool force = false) noexcept
        {
            bands = std::clamp (bands, 1, maxBands);
            std::array<float, 3> f { f1, f2, f3 };
            const double nyq = sr * 0.45;
            for (int k = 0; k < 3; ++k)
            {
                double v = std::clamp ((double) f[(size_t) k], 20.0, nyq);
                if (k > 0) v = std::max (v, (double) f[(size_t) k - 1] * 1.26);
                f[(size_t) k] = (float) std::min (v, nyq);
            }
            if (! force && bands == numBands && f == freqs)
                return;
            numBands = bands;
            freqs = f;
            for (int k = 0; k < 3; ++k)
            {
                lp[(size_t) k] = design (0, f[(size_t) k]);
                hp[(size_t) k] = design (1, f[(size_t) k]);
                ap[(size_t) k] = design (2, f[(size_t) k]);
            }
        }

        int bands() const noexcept { return numBands; }
        float frequency (int k) const noexcept { return freqs[(size_t) std::clamp (k, 0, 2)]; }

        // Splits one sample of channel `ch` into out[0..bands-1].
        void split (int ch, float x, float* out) noexcept
        {
            auto& st = state[(size_t) std::clamp (ch, 0, maxChannels - 1)];
            const int splits = std::clamp (numBands - 1, 0, maxBands - 1);
            double rest = x;
            for (int k = 0; k < splits; ++k)
            {
                double low = run (lp[(size_t) k], st[(size_t) (k * 5 + 0)], rest);
                low = run (lp[(size_t) k], st[(size_t) (k * 5 + 1)], low);
                double high = run (hp[(size_t) k], st[(size_t) (k * 5 + 2)], rest);
                high = run (hp[(size_t) k], st[(size_t) (k * 5 + 3)], high);
                // Compensate this band for the later splits it skips.
                for (int j = k + 1; j < splits; ++j)
                    low = run (ap[(size_t) j], st[(size_t) (15 + k * 3 + j)], low);
                out[k] = (float) low;
                rest = high;
            }
            out[splits] = (float) rest;
        }

    private:
        struct Coef { double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }; };
        struct State { double z1 { 0 }, z2 { 0 }; };

        static double run (const Coef& c, State& s, double x) noexcept
        {
            const double y = c.b0 * x + s.z1;
            s.z1 = c.b1 * x - c.a1 * y + s.z2;
            s.z2 = c.b2 * x - c.a2 * y;
            return y;
        }

        // kind 0 = Butterworth low-pass, 1 = Butterworth high-pass, 2 = all-pass (Q 1/sqrt2).
        Coef design (int kind, double f) const noexcept
        {
            constexpr double pi = 3.14159265358979323846, q = 0.70710678118654752;
            const double w = 2.0 * pi * f / sr, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
            Coef c;
            if (kind == 0)      { c.b0 = (1 - cw) * 0.5; c.b1 = 1 - cw;    c.b2 = (1 - cw) * 0.5; }
            else if (kind == 1) { c.b0 = (1 + cw) * 0.5; c.b1 = -(1 + cw); c.b2 = (1 + cw) * 0.5; }
            else                { c.b0 = 1 - alpha;      c.b1 = -2 * cw;   c.b2 = 1 + alpha; }
            c.b0 /= a0; c.b1 /= a0; c.b2 /= a0;
            c.a1 = -2 * cw / a0; c.a2 = (1 - alpha) / a0;
            return c;
        }

        double sr { 48000.0 };
        int numBands { 1 };
        std::array<float, 3> freqs { 120.0f, 1000.0f, 6000.0f };
        std::array<Coef, 3> lp, hp, ap;
        // Per channel: 3 splits x (2 LP + 2 HP + 1 spare) states, then 3x3 all-pass states.
        std::array<std::array<State, 24>, maxChannels> state {};
    };
}
