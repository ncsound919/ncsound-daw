#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fx
{
    enum class FilterType { peak, lowShelf, highShelf, lowPass, highPass, bandPass, notch, allPass };

    struct BiquadCoeffs
    {
        float b0 { 1.0f }, b1 { 0.0f }, b2 { 0.0f }, a1 { 0.0f }, a2 { 0.0f };

        static BiquadCoeffs from (FilterType type, double sampleRate, double freq, double q, double gainDb) noexcept
        {
            BiquadCoeffs c;

            const auto sr    = sampleRate > 0.0 ? sampleRate : 48000.0;
            const auto nyq   = sr * 0.5;
            const auto f     = std::clamp (freq, 10.0, nyq * 0.98);
            const auto w0    = 2.0 * 3.14159265358979 * f / sr;
            const auto cw    = std::cos (w0);
            const auto sw    = std::sin (w0);
            const auto Q     = std::max (0.05, q);
            const auto alpha = sw / (2.0 * Q);
            const auto A     = std::pow (10.0, gainDb / 40.0);
            const auto sqrtA = std::sqrt (A);

            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

            switch (type)
            {
                case FilterType::peak:
                    b0 = 1.0 + alpha * A;
                    b1 = -2.0 * cw;
                    b2 = 1.0 - alpha * A;
                    a0 = 1.0 + alpha / A;
                    a1 = -2.0 * cw;
                    a2 = 1.0 - alpha / A;
                    break;

                case FilterType::lowShelf:
                    b0 = A * ((A + 1.0) - (A - 1.0) * cw + 2.0 * sqrtA * alpha);
                    b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
                    b2 = A * ((A + 1.0) - (A - 1.0) * cw - 2.0 * sqrtA * alpha);
                    a0 = (A + 1.0) + (A - 1.0) * cw + 2.0 * sqrtA * alpha;
                    a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
                    a2 = (A + 1.0) + (A - 1.0) * cw - 2.0 * sqrtA * alpha;
                    break;

                case FilterType::highShelf:
                    b0 = A * ((A + 1.0) + (A - 1.0) * cw + 2.0 * sqrtA * alpha);
                    b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
                    b2 = A * ((A + 1.0) + (A - 1.0) * cw - 2.0 * sqrtA * alpha);
                    a0 = (A + 1.0) - (A - 1.0) * cw + 2.0 * sqrtA * alpha;
                    a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
                    a2 = (A + 1.0) - (A - 1.0) * cw - 2.0 * sqrtA * alpha;
                    break;

                case FilterType::lowPass:
                    b0 = (1.0 - cw) * 0.5;
                    b1 = 1.0 - cw;
                    b2 = b0;
                    a0 = 1.0 + alpha;
                    a1 = -2.0 * cw;
                    a2 = 1.0 - alpha;
                    break;

                case FilterType::highPass:
                    b0 = (1.0 + cw) * 0.5;
                    b1 = -(1.0 + cw);
                    b2 = b0;
                    a0 = 1.0 + alpha;
                    a1 = -2.0 * cw;
                    a2 = 1.0 - alpha;
                    break;

                case FilterType::bandPass:
                    b0 = alpha;
                    b1 = 0.0;
                    b2 = -alpha;
                    a0 = 1.0 + alpha;
                    a1 = -2.0 * cw;
                    a2 = 1.0 - alpha;
                    break;

                case FilterType::notch:
                    b0 = 1.0;
                    b1 = -2.0 * cw;
                    b2 = 1.0;
                    a0 = 1.0 + alpha;
                    a1 = -2.0 * cw;
                    a2 = 1.0 - alpha;
                    break;

                case FilterType::allPass:
                    b0 = 1.0 - alpha;
                    b1 = -2.0 * cw;
                    b2 = 1.0 + alpha;
                    a0 = 1.0 + alpha;
                    a1 = -2.0 * cw;
                    a2 = 1.0 - alpha;
                    break;
            }

            c.b0 = (float) (b0 / a0);
            c.b1 = (float) (b1 / a0);
            c.b2 = (float) (b2 / a0);
            c.a1 = (float) (a1 / a0);
            c.a2 = (float) (a2 / a0);
            return c;
        }
    };

    // Direct-form-I biquad with two state variables.
    class Biquad
    {
    public:
        void setCoeffs (const BiquadCoeffs& c) noexcept { coeffs = c; }

        void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

        float process (float x) noexcept
        {
            const auto y = coeffs.b0 * x + coeffs.b1 * x1 + coeffs.b2 * x2
                         - coeffs.a1 * y1 - coeffs.a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }

    private:
        BiquadCoeffs coeffs {};
        float x1 { 0.0f }, x2 { 0.0f }, y1 { 0.0f }, y2 { 0.0f };
    };

    // One EQ band: coefficient set shared by both channels, separate state.
    class StereoBand
    {
    public:
        void set (FilterType type, double sampleRate, double freq, double q, double gainDb) noexcept
        {
            const auto c = BiquadCoeffs::from (type, sampleRate, freq, q, gainDb);
            coeffs = c;
            left.setCoeffs (c);
            right.setCoeffs (c);
        }

        void reset() noexcept { left.reset(); right.reset(); }

        void process (float& l, float& r) noexcept
        {
            l = left.process (l);
            r = right.process (r);
        }

        // One channel only (the left filter state), for mid- or side-only bands.
        float processMono (float x) noexcept { return left.process (x); }

        const BiquadCoeffs& coefficients() const noexcept { return coeffs; }

    private:
        Biquad left, right;
        BiquadCoeffs coeffs {};
    };

    // Magnitude response (linear) of a biquad at a given frequency. Shared by
    // the slope/EQ tests and the native response read-outs.
    inline double biquadMagnitude (const BiquadCoeffs& c, double sampleRate, double freq) noexcept
    {
        const auto sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto w  = 2.0 * 3.14159265358979 * std::clamp (freq, 0.0, sr * 0.5) / sr;
        const auto z1r = std::cos (-w),               z1i = std::sin (-w);
        const auto z2r = std::cos (-2.0 * w),         z2i = std::sin (-2.0 * w);

        const auto numR = (double) c.b0 + (double) c.b1 * z1r + (double) c.b2 * z2r;
        const auto numI =               (double) c.b1 * z1i + (double) c.b2 * z2i;
        const auto denR = 1.0        + (double) c.a1 * z1r + (double) c.a2 * z2r;
        const auto denI =               (double) c.a1 * z1i + (double) c.a2 * z2i;

        const auto num = std::sqrt (numR * numR + numI * numI);
        const auto den = std::sqrt (denR * denR + denI * denI);
        return den > 1.0e-12 ? num / den : 0.0;
    }
}
