#pragma once

#include "Biquad.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace fx
{
    // Feed-forward stereo-linked compressor with soft knee, peak/RMS detector
    // blend, makeup and parallel mix. Detector can be high-passed (sidechain
    // HPF) and shaped by a sidechain bell EQ.
    //
    // Four real topology flavours, selected with Type:
    //   vca    - clean, predictable, peak detector, user ratio/knee/attack/release.
    //   fet    - fast and aggressive: harder knee, boosted ratio, very fast attack.
    //   opto   - slow, soft knee, RMS detector, program-dependent release (release
    //            stretches as gain reduction deepens).
    //   varimu - variable-mu: ratio softens as it clamps harder, RMS detector,
    //            soft knee, slowest envelopes, program-dependent release.
    // The differences are applied to the actual detector / curve / envelope maths,
    // not just labels.
    //
    // Modern controls (all opt-in, neutral by default so existing behaviour is
    // unchanged): lookahead, hold, a hard range cap on gain reduction, a scaling
    // amount (0-200% of the applied gain change), auto release, auto gain and a
    // sidechain bell EQ.
    class Compressor
    {
    public:
        enum class Type { vca, fet, opto, varimu };

        void prepare (double sampleRate, int numChannels)
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            channels = std::max (1, numChannels);
            scState.assign ((size_t) channels, 0.0f);
            detState.assign ((size_t) channels, 0.0f);
            envDb = 0.0f;
            grDb  = 0.0f;
            grAvgDb = 0.0f;
            holdCounter = 0;

            // 10 ms RMS window for the opto/varimu detector.
            rmsCoeff = std::exp (-1.0f / (0.010f * (float) sr));

            // Lookahead buffer holds up to 20 ms.
            maxLookahead = (int) std::ceil (0.020 * sr) + 2;
            delayLine.assign ((size_t) channels, std::vector<float> ((size_t) maxLookahead, 0.0f));
            writePos = 0;

            autoGainCoeff = std::exp (-1.0f / (0.500f * (float) sr));

            recomputeAttackRelease();
            updateScEq();
        }

        void reset() noexcept
        {
            std::fill (scState.begin(), scState.end(), 0.0f);
            std::fill (detState.begin(), detState.end(), 0.0f);

            for (auto& d : delayLine)
                std::fill (d.begin(), d.end(), 0.0f);

            envDb = 0.0f;
            grDb  = 0.0f;
            grAvgDb = 0.0f;
            holdCounter = 0;
            writePos = 0;
            scEqL.reset();
            scEqR.reset();
        }

        struct Params
        {
            float thresholdDb { -18.0f };
            float ratio { 4.0f };
            float attackMs { 10.0f };
            float releaseMs { 120.0f };
            float kneeDb { 6.0f };
            float makeupDb { 0.0f };
            float mix { 1.0f };
            float sidechainHpfHz { 20.0f };
            bool  rms { false };
            Type  type { Type::vca };

            // Modern controls.
            float lookaheadMs { 0.0f };   // 0..20
            float holdMs { 0.0f };        // 0..500
            float rangeDb { 60.0f };      // max gain reduction (dB)
            float amount { 1.0f };        // 0..2 scale of applied gain change
            float scEqFreqHz { 0.0f };    // 0 = sidechain EQ off
            float scEqGainDb { 0.0f };
            float scEqQ { 1.0f };
            bool  autoRelease { false };
            bool  autoGain { false };
        };

        void setParams (const Params& p) noexcept
        {
            const bool coeffsChanged = p.attackMs != params.attackMs
                                    || p.releaseMs != params.releaseMs
                                    || p.type != params.type;

            const bool scEqChanged = p.scEqFreqHz != params.scEqFreqHz
                                  || p.scEqGainDb != params.scEqGainDb
                                  || p.scEqQ != params.scEqQ
                                  || p.sidechainHpfHz != params.sidechainHpfHz
                                  || p.type != params.type;

            params = p;
            lookaheadSamples = (int) std::lround (std::clamp (params.lookaheadMs, 0.0f, 20.0f) * 0.001f * (float) sr);
            lookaheadSamples = std::clamp (lookaheadSamples, 0, maxLookahead - 1);
            holdSamples = (int) std::lround (std::clamp (params.holdMs, 0.0f, 500.0f) * 0.001f * (float) sr);

            if (coeffsChanged)
                recomputeAttackRelease();

            if (scEqChanged)
                updateScEq();
        }

        float gainReductionDb() const noexcept { return grDb; }

        void process (float& l, float& r) noexcept
        {
            const bool active = params.ratio > 1.001f || params.mix < 0.999f
                             || lookaheadSamples > 0;

            if (! active)
            {
                grDb = 0.0f;
                return;
            }

            // ---- detector (on the *undelayed* input so lookahead anticipates) ----
            const auto scCoeff = std::exp (-2.0f * 3.14159265f * params.sidechainHpfHz / (float) sr);
            float scL = 0.0f, scR = 0.0f;
            {
                const auto inL = l, inR = r;
                scState[0] = inL * (1.0f - scCoeff) + scState[0] * scCoeff;
                scL = inL - scState[0];
                if (channels > 1)
                {
                    scState[1] = inR * (1.0f - scCoeff) + scState[1] * scCoeff;
                    scR = inR - scState[1];
                }
                else
                {
                    scR = scL;
                }
            }

            // Optional sidechain bell EQ on the detector only.
            if (params.scEqFreqHz > 1.0f && std::abs (params.scEqGainDb) > 0.01f)
            {
                scL = scEqL.process (scL);
                scR = channels > 1 ? scEqR.process (scR) : scL;
            }

            const auto peak = std::max (std::abs (scL), std::abs (scR));
            float det = peak;

            if (params.type == Type::opto || params.type == Type::varimu)
            {
                const auto sq = std::max (scL * scL, scR * scR);
                detState[0] = sq + rmsCoeff * (detState[0] - sq);
                det = std::sqrt (std::max (0.0f, detState[0]));
            }

            const auto detDb = det > 1.0e-9f ? 20.0f * std::log10 (det) : -120.0f;
            const auto inDb = detDb;

            const auto knee = std::max (0.1f, params.kneeDb * kneeScale (params.type));

            float effRatio = std::max (1.001f, params.ratio * ratioScale (params.type));

            if (params.type == Type::varimu)
                effRatio = 1.0f + (effRatio - 1.0f) / (1.0f + std::max (0.0f, grDb) * 0.18f);

            const auto over = inDb - params.thresholdDb;
            float targetGr = 0.0f;

            if (over >= knee * 0.5f)
                targetGr = over - over / effRatio;
            else if (over > -knee * 0.5f)
            {
                const auto x = over + knee * 0.5f;
                targetGr = (x * x) / (2.0f * knee) * (1.0f - 1.0f / effRatio);
            }

            // Hard range cap.
            targetGr = std::min (targetGr, std::max (0.0f, params.rangeDb));

            float aCoeff = attackCoeff;
            float rCoeff = releaseCoeff;

            // Program-dependent release for opto/varimu (and optional auto)
            // stretches as gain reduction deepens.
            const auto programDep = params.type == Type::opto || params.type == Type::varimu;
            if (programDep || params.autoRelease)
            {
                auto relMs = params.releaseMs * releaseScale (params.type);
                if (programDep)
                    relMs *= 1.0f + 0.05f * std::max (0.0f, grDb);
                if (params.autoRelease)
                    relMs *= 1.0f + 0.50f * std::max (0.0f, grDb);
                rCoeff = std::exp (-1.0f / (0.001f * std::max (1.0f, relMs) * (float) sr));
            }

            if (targetGr > grDb)
            {
                // Attack (or more reduction): reset the hold window.
                grDb = targetGr + (grDb - targetGr) * aCoeff;
                holdCounter = holdSamples;
            }
            else
            {
                // Release, gated by the hold window.
                if (holdCounter > 0)
                    --holdCounter;
                else
                    grDb = targetGr + (grDb - targetGr) * rCoeff;
            }

            // ---- apply to (optionally delayed) audio ----
            const int size = (maxLookahead > 0) ? maxLookahead : 1;
            auto& dL = delayLine[0];
            auto& dR = delayLine[(size_t) std::min (1, channels - 1)];

            dL[(size_t) writePos] = l;
            if (channels > 1) dR[(size_t) writePos] = r;

            int readPos = writePos - lookaheadSamples;
            if (readPos < 0) readPos += size;

            const auto dryL = dL[(size_t) readPos];
            const auto dryR = channels > 1 ? dR[(size_t) readPos] : dryL;

            writePos = (writePos + 1) % size;

            // Slow average of the applied reduction drives auto gain.
            grAvgDb = grDb + (grAvgDb - grDb) * autoGainCoeff;

            const auto appliedGr = grDb * std::max (0.0f, params.amount);
            const auto gain = std::pow (10.0f, -appliedGr / 20.0f);
            const auto autoMakeup = params.autoGain ? grAvgDb : 0.0f;
            const auto makeup = std::pow (10.0f, (params.makeupDb + autoMakeup) / 20.0f);
            const auto wet = gain * makeup;

            l = dryL * (1.0f - params.mix) + dryL * wet * params.mix;
            r = dryR * (1.0f - params.mix) + dryR * wet * params.mix;
        }

    private:
        static float attackScale (Type t) noexcept
        {
            switch (t)
            {
                case Type::fet:    return 0.5f;
                case Type::opto:   return 2.5f;
                case Type::varimu: return 3.0f;
                case Type::vca:
                default:           return 1.0f;
            }
        }

        static float releaseScale (Type t) noexcept
        {
            switch (t)
            {
                case Type::fet:    return 0.7f;
                case Type::opto:   return 3.0f;
                case Type::varimu: return 4.0f;
                case Type::vca:
                default:           return 1.0f;
            }
        }

        static float kneeScale (Type t) noexcept
        {
            switch (t)
            {
                case Type::fet:    return 0.12f;
                case Type::opto:   return 1.6f;
                case Type::varimu: return 1.3f;
                case Type::vca:
                default:           return 1.0f;
            }
        }

        static float ratioScale (Type t) noexcept
        {
            switch (t)
            {
                case Type::fet:    return 1.5f;
                case Type::opto:
                case Type::varimu:
                case Type::vca:
                default:           return 1.0f;
            }
        }

        void updateScEq() noexcept
        {
            if (params.scEqFreqHz > 1.0f)
            {
                const auto c = BiquadCoeffs::from (FilterType::peak, sr,
                                                   (double) params.scEqFreqHz,
                                                   std::max (0.05f, params.scEqQ),
                                                   (double) params.scEqGainDb);
                scEqL.setCoeffs (c);
                scEqR.setCoeffs (c);
            }
        }

        void recomputeAttackRelease()
        {
            attackCoeff  = std::exp (-1.0f / (0.001f * std::max (0.05f, params.attackMs * attackScale (params.type))
                                              * (float) sr));
            releaseCoeff = std::exp (-1.0f / (0.001f * std::max (1.0f, params.releaseMs * releaseScale (params.type))
                                              * (float) sr));
        }

        Params params {};
        double sr { 48000.0 };
        int channels { 2 };
        std::vector<float> scState;
        std::vector<float> detState;
        std::vector<std::vector<float>> delayLine;
        int maxLookahead { 1 };
        int lookaheadSamples { 0 };
        int holdSamples { 0 };
        int holdCounter { 0 };
        int writePos { 0 };
        Biquad scEqL, scEqR;
        float envDb { 0.0f }, grDb { 0.0f }, grAvgDb { 0.0f };
        float attackCoeff { 0.99f }, releaseCoeff { 0.999f };
        float rmsCoeff { 0.999f };
        float autoGainCoeff { 0.9999f };
    };
}
