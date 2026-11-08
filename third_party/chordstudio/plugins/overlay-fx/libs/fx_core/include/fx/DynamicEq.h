#pragma once

#include "Biquad.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace fx
{
    // Flexible EQ band shapes. This is an independent implementation of the
    // filter taxonomy shared by most modern parametric EQs (bell / shelf /
    // cut / notch / band-pass / all-pass); it is not an emulation of any
    // commercial product.
    enum class EqShape { bell, lowShelf, highShelf, lowCut, highCut, notch, bandPass, allPass };

    inline FilterType filterTypeFor (EqShape s) noexcept
    {
        switch (s)
        {
            case EqShape::bell:      return FilterType::peak;
            case EqShape::lowShelf:  return FilterType::lowShelf;
            case EqShape::highShelf: return FilterType::highShelf;
            case EqShape::lowCut:    return FilterType::highPass;
            case EqShape::highCut:   return FilterType::lowPass;
            case EqShape::notch:     return FilterType::notch;
            case EqShape::bandPass:  return FilterType::bandPass;
            case EqShape::allPass:   return FilterType::allPass;
        }

        return FilterType::peak;
    }

    inline bool shapeHasGain (EqShape s) noexcept
    {
        return s == EqShape::bell || s == EqShape::lowShelf || s == EqShape::highShelf;
    }

    // Cuts and shelves are cascaded with Butterworth Q values so that a higher
    // order gives a genuinely steeper skirt without peaking. Bell/notch/
    // band-pass/all-pass keep the user's Q per section.
    inline bool shapeUsesButterworth (EqShape s) noexcept
    {
        return s == EqShape::lowCut || s == EqShape::highCut
            || s == EqShape::lowShelf || s == EqShape::highShelf;
    }

    // Q of section k (0-based) for an order-`stages` Butterworth cascade.
    inline double butterworthQ (int stages, int k) noexcept
    {
        const auto n = std::max (1, stages);
        return 1.0 / (2.0 * std::sin ((2.0 * k + 1.0) * 3.14159265358979 / (4.0 * n)));
    }

    struct EqBandSpec
    {
        EqShape shape { EqShape::bell };
        double  freq { 1000.0 };
        double  gainDb { 0.0 };
        double  q { 1.0 };
        // Cascaded 2nd-order sections. Slope = order * 12 dB/oct approx.
        // For gain shapes each section carries gainDb / order so the plateau or
        // peak gain is preserved as the slope steepens.
        int     order { 1 };
    };

    // Magnitude (dB) of a cascade at a frequency. Used for UI read-outs/tests.
    inline double eqCascadeMagnitudeDb (const std::vector<BiquadCoeffs>& sections,
                                        double sampleRate, double freq) noexcept
    {
        double mag = 1.0;

        for (const auto& c : sections)
            mag *= biquadMagnitude (c, sampleRate, freq);

        return 20.0 * std::log10 (std::max (1.0e-9, mag));
    }

    // One EQ band: static filter (optionally multi-section) plus an optional
    // dynamic stage. The dynamic stage detects the level inside the band and
    // scales the band's EQ delta:
    //   out = in + (filtered - in) * gain
    // so a positive range ducks the band toward flat when the band is loud, and
    // a negative range boosts it. This is a documented, independent design, not
    // a clone of a specific dynamic-EQ algorithm.
    class DynamicEqBand
    {
    public:
        void prepare (double sampleRate, int numChannels, int maxOrder = 4)
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            channels = std::max (1, numChannels);
            maxStages = std::clamp (maxOrder, 1, 4);

            sections.assign ((size_t) maxStages, StereoBand {});

            const auto q = std::max (0.05, spec.q);
            detL.setCoeffs (BiquadCoeffs::from (FilterType::bandPass, sr, spec.freq, q, 0.0));
            detR.setCoeffs (BiquadCoeffs::from (FilterType::bandPass, sr, spec.freq, q, 0.0));

            updateCoeffs();
        }

        void reset() noexcept
        {
            for (auto& s : sections)
                s.reset();

            detL.reset();
            detR.reset();
            dynGrDb = 0.0f;
        }

        void setEnabled (bool e) noexcept { enabled = e; }

        void setSpec (const EqBandSpec& s) noexcept
        {
            spec = s;
            updateCoeffs();
            detL.setCoeffs (BiquadCoeffs::from (FilterType::bandPass, sr, spec.freq, std::max (0.05, spec.q), 0.0));
            detR.setCoeffs (BiquadCoeffs::from (FilterType::bandPass, sr, spec.freq, std::max (0.05, spec.q), 0.0));
        }

        void setDynamic (bool on, double thresholdDb, double rangeDb,
                         double attackMs, double releaseMs) noexcept
        {
            dynOn = on;
            dynThresholdDb = thresholdDb;
            dynRangeDb = rangeDb;
            dynAttackCoeff  = (float) std::exp (-1.0 / (0.001 * std::max (0.1, attackMs) * sr));
            dynReleaseCoeff = (float) std::exp (-1.0 / (0.001 * std::max (1.0, releaseMs) * sr));
            updateCoeffs();
        }

        // Current dynamic gain applied to the band delta, in dB (always <= 0 for
        // a ducking range, >= 0 for a boosting range). 0 when not dynamic.
        float dynamicGainDb() const noexcept { return dynGrDb; }

        void process (float& l, float& r) noexcept
        {
            if (! enabled)
                return;

            const auto inL = l, inR = r;

            for (auto& s : sections)
                s.process (l, r);

            if (! dynOn)
            {
                dynGrDb = 0.0f;
                return;
            }

            const auto dL = detL.process (inL);
            const auto dR = detR.process (channels > 1 ? inR : inL);
            const auto peak = std::max (std::abs (dL), std::abs (dR));
            const auto peakDb = peak > 1.0e-9f ? 20.0f * std::log10 (peak) : -120.0f;
            const auto over = peakDb - dynThresholdDb;

            float target = 0.0f;

            if (over > 0.0f)
            {
                const auto amount = std::min ((double) std::abs (dynRangeDb), (double) over);
                target = -(float) std::copysign (amount, dynRangeDb);
            }

            // Differentiate attack/release by direction of travel. Because the
            // amount is bounded by |range|, the applied gain can never over- or
            // under-shoot the requested range.
            const auto coeff = std::abs (target) > std::abs (dynGrDb) ? dynAttackCoeff : dynReleaseCoeff;
            dynGrDb = target + (dynGrDb - target) * coeff;

            // The static sections are built at `refGainDb`; re-scale the filter
            // delta so the effective gain at the band centre is exactly
            // (static + dynamic). This also lets a band run purely dynamically
            // (static gain 0) with a non-zero reference. For non-gain shapes the
            // dynamic value simply scales the cut/notch depth.
            //
            // The scale only changes with the (slow) envelope, so it is recomputed
            // when the envelope moves and cached otherwise to avoid a per-sample
            // pow() across every band.
            if (! scaleValid || std::abs (dynGrDb - cachedGrDb) > 0.02f)
            {
                const auto totalGain = shapeHasGain (spec.shape) ? spec.gainDb + (double) dynGrDb
                                                                 : (double) dynGrDb;

                if (shapeHasGain (spec.shape))
                {
                    const auto denom = std::pow (10.0, refGainDb / 20.0) - 1.0;
                    const auto numer = std::pow (10.0, totalGain / 20.0) - 1.0;
                    cachedScale = (float) (std::abs (denom) > 1.0e-6 ? numer / denom : 1.0);
                }
                else
                {
                    cachedScale = (float) std::pow (10.0, totalGain / 20.0);
                }

                cachedGrDb = dynGrDb;
                scaleValid = true;
            }

            const auto s = cachedScale;
            l = inL + (l - inL) * s;
            r = inR + (r - inR) * s;
        }

        // One channel (mid or side of an M/S pair). Uses the left filter and
        // detector state only; same maths as process().
        void processMono (float& x) noexcept
        {
            if (! enabled)
                return;

            const auto in = x;

            for (auto& s : sections)
                x = s.processMono (x);

            if (! dynOn)
            {
                dynGrDb = 0.0f;
                return;
            }

            const auto d = detL.process (in);
            const auto peak = std::abs (d);
            updateDynamic (peak > 1.0e-9f ? 20.0f * std::log10 (peak) : -120.0f);
            x = in + (x - in) * cachedScale;
        }

        // Linear magnitude of the static band (all sections) at `freq`.
        double staticMagnitude (double freq) const noexcept
        {
            double m = 1.0;
            const auto stages = std::clamp (spec.order, 1, maxStages);
            for (int i = 0; i < stages; ++i)
                m *= biquadMagnitude (sections[(size_t) i].coefficients(), sr, freq);
            return m;
        }

        const EqBandSpec& currentSpec() const noexcept { return spec; }

    private:
        void updateDynamic (float peakDb) noexcept
        {
            const auto over = peakDb - dynThresholdDb;
            float target = 0.0f;

            if (over > 0.0f)
            {
                const auto amount = std::min ((double) std::abs (dynRangeDb), (double) over);
                target = -(float) std::copysign (amount, dynRangeDb);
            }

            const auto coeff = std::abs (target) > std::abs (dynGrDb) ? dynAttackCoeff : dynReleaseCoeff;
            dynGrDb = target + (dynGrDb - target) * coeff;

            if (! scaleValid || std::abs (dynGrDb - cachedGrDb) > 0.02f)
            {
                const auto totalGain = shapeHasGain (spec.shape) ? spec.gainDb + (double) dynGrDb
                                                                 : (double) dynGrDb;
                if (shapeHasGain (spec.shape))
                {
                    const auto denom = std::pow (10.0, refGainDb / 20.0) - 1.0;
                    const auto numer = std::pow (10.0, totalGain / 20.0) - 1.0;
                    cachedScale = (float) (std::abs (denom) > 1.0e-6 ? numer / denom : 1.0);
                }
                else
                {
                    cachedScale = (float) std::pow (10.0, totalGain / 20.0);
                }

                cachedGrDb = dynGrDb;
                scaleValid = true;
            }
        }

        void updateCoeffs() noexcept
        {
            const auto stages = std::clamp (spec.order, 1, maxStages);

            // Reference gain used to build the static sections. When the static
            // gain is ~0 and a dynamic range is set, use a +/-12 dB reference so
            // the dynamic scaling has a well-conditioned delta to work from.
            if (shapeHasGain (spec.shape))
            {
                if (std::abs (spec.gainDb) >= 0.05)
                    refGainDb = spec.gainDb;
                else if (dynOn && std::abs (dynRangeDb) >= 0.05)
                    refGainDb = std::copysign (12.0, dynRangeDb);
                else
                    refGainDb = spec.gainDb;
            }
            else
            {
                refGainDb = 0.0;
            }

            const auto perStageGain = shapeHasGain (spec.shape) ? refGainDb / (double) stages : 0.0;

            for (int i = 0; i < maxStages; ++i)
            {
                if (i >= stages)
                {
                    // Identity: unity peak, no gain.
                    sections[(size_t) i].set (FilterType::peak, sr, spec.freq, 0.707, 0.0);
                    continue;
                }

                const auto q = shapeUsesButterworth (spec.shape) ? butterworthQ (stages, i) : spec.q;
                sections[(size_t) i].set (filterTypeFor (spec.shape), sr, spec.freq, q, perStageGain);
            }

            scaleValid = false;
        }

        double sr { 48000.0 };
        int channels { 2 };
        int maxStages { 4 };

        EqBandSpec spec {};
        bool enabled { true };
        bool dynOn { false };
        double dynThresholdDb { -20.0 };
        double dynRangeDb { 4.0 };
        double refGainDb { 0.0 };

        std::vector<StereoBand> sections;
        Biquad detL, detR;

        float dynGrDb { 0.0f };
        float dynAttackCoeff { 0.99f };
        float dynReleaseCoeff { 0.999f };
        float cachedGrDb { 1.0e9f };
        float cachedScale { 1.0f };
        bool  scaleValid { false };
    };
}
