#pragma once

// MixRack v3 DSP.
//
// The baked Overlay Channel Strip and Overlay Master used to expose a small
// subset of the vendored fx_core: five fixed filters, one VCA compressor and
// one tape shaper. This header adds the parts a modern channel strip and
// master chain actually need:
//
//   ProEq          - eight parametric bands with bell / shelf / cut / notch /
//                    band-pass / all-pass shapes, 12-48 dB/oct slopes,
//                    per-band dynamic control and stereo / mid / side
//                    placement. Dynamic range is signed: negative cuts and
//                    positive boosts while the band is above its threshold.
//   SaturnSat      - eight saturation characters with drive, mix, bias,
//                    bass/mid/treble/presence tone, gate/compress dynamics,
//                    feedback and first-order ADAA anti-aliasing (HQ).
//   MultibandSat   - the same saturator across 1-4 phase-coherent bands, used
//                    by the master exciter.
//   MixCompressor  - one wrapper over fx::Compressor that maps VCA/FET/Opto/
//                    VariMu plus Bus, Punch, Transparent and Limiter
//                    characters onto distinct envelope maths.
//
// All state is preallocated in prepare(); process() never allocates, locks or
// calls into the message thread. These are original designs, not emulations of
// any commercial product.

#include "fx/Biquad.h"
#include "fx/Compressor.h"
#include "fx/Crossover.h"
#include "fx/DynamicEq.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace nc::mixrack
{
//==============================================================================
// Shared EQ shape ids. These are the values the UI and the parameter surface
// use; they are stable and independent of the fx_core enum.
enum class EqShapeId
{
    bell = 0,
    lowShelf,
    highShelf,
    lowCut,
    highCut,
    notch,
    bandPass,
    allPass
};

inline fx::EqShape toFxShape (int type) noexcept
{
    switch (type)
    {
        case 1:  return fx::EqShape::lowShelf;
        case 2:  return fx::EqShape::highShelf;
        case 3:  return fx::EqShape::lowCut;
        case 4:  return fx::EqShape::highCut;
        case 5:  return fx::EqShape::notch;
        case 6:  return fx::EqShape::bandPass;
        case 7:  return fx::EqShape::allPass;
        default: return fx::EqShape::bell;
    }
}

inline bool shapeHasGain (int type) noexcept
{
    return type == 0 || type == 1 || type == 2;
}

inline int orderFromSlope (int slope) noexcept
{
    return 1 + std::clamp (slope, 0, 3);
}

struct EqBandSettings
{
    bool on { true };
    int type { 0 };
    float freq { 1000.0f };
    float gainDb { 0.0f };
    float q { 1.0f };
    int slope { 0 };
    int channel { 0 };       // 0 stereo, 1 mid, 2 side
    bool dynOn { false };
    float dynThreshDb { -20.0f };
    float dynRangeDb { 0.0f }; // negative cuts while loud, positive boosts
    float dynAttackMs { 10.0f };
    float dynReleaseMs { 120.0f };
};

//==============================================================================
/**
    One parametric band with an optional dynamic stage.

    The static filter is built at the *reference* gain (static + dynamic range)
    and the wet/dry blend is scaled so the band lands exactly on the requested
    gain at every envelope position. This avoids recomputing biquad coefficients
    per sample and makes the dynamic range behave like Pro-Q's ring: a negative
    range cuts while the band is above threshold, a positive range boosts.
*/
class EqBandV2
{
public:
    void prepare (double sampleRate, int maxSections = 4)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        maxStages = std::clamp (maxSections, 1, 4);
        sections.assign (static_cast<size_t> (maxStages), fx::StereoBand {});
        reset();
    }

    void reset() noexcept
    {
        for (auto& section : sections)
            section.reset();
        detL.reset();
        detR.reset();
        env = 0.0f;
        dynGainDb = 0.0f;
        scaleValid = false;
    }

    void configure (const EqBandSettings& s) noexcept
    {
        spec = s;
        enabled = s.on;

        const bool gainShape = shapeHasGain (s.type);
        dynamicActive = s.dynOn && gainShape && std::abs (s.dynRangeDb) >= 0.01f;

        // Reference gain: build the static filter here so the blend is a
        // simple, well-conditioned ratio.
        const auto range = dynamicActive ? s.dynRangeDb : 0.0f;
        refGainDb = gainShape ? s.gainDb + range : 0.0;

        if (gainShape && std::abs (refGainDb) < 0.01f && std::abs (s.gainDb) < 0.01f)
            refGainDb = 0.0;

        stages = (s.type == 0 || s.type == 5 || s.type == 6 || s.type == 7)
                     ? 1
                     : orderFromSlope (s.slope);

        const auto perStage = gainShape ? refGainDb / (double) stages : 0.0;

        for (int i = 0; i < maxStages; ++i)
        {
            if (i >= stages)
            {
                sections[static_cast<size_t> (i)].set (fx::FilterType::peak, sr, s.freq, 0.707, 0.0);
                continue;
            }

            const auto q = fx::shapeUsesButterworth (toFxShape (s.type))
                               ? fx::butterworthQ (stages, i)
                               : std::max (0.05, (double) s.q);

            sections[static_cast<size_t> (i)].set (fx::filterTypeFor (toFxShape (s.type)),
                                                   sr, s.freq, q, perStage);
        }

        detL.setCoeffs (fx::BiquadCoeffs::from (fx::FilterType::bandPass, sr,
                                                s.freq, std::clamp ((double) s.q, 0.1, 10.0), 0.0));
        detR.setCoeffs (fx::BiquadCoeffs::from (fx::FilterType::bandPass, sr,
                                                s.freq, std::clamp ((double) s.q, 0.1, 10.0), 0.0));

        attackCoeff = (float) std::exp (-1.0 / (0.001 * std::max (0.1, (double) s.dynAttackMs) * sr));
        releaseCoeff = (float) std::exp (-1.0 / (0.001 * std::max (1.0, (double) s.dynReleaseMs) * sr));

        scaleValid = false;
        dynGainDb = 0.0f;
        env = 0.0f;
    }

    void setEnabled (bool e) noexcept { enabled = e; }
    bool isEnabled() const noexcept { return enabled; }
    int channel() const noexcept { return spec.channel; }
    float dynamicGainDb() const noexcept { return dynGainDb; }
    float debugScale() const noexcept { return cachedScale; }
    float debugRefGainDb() const noexcept { return (float) refGainDb; }

    void process (float& l, float& r) noexcept
    {
        if (! enabled)
            return;

        const auto inL = l;
        const auto inR = r;

        for (auto& section : sections)
            section.process (l, r);

        if (! dynamicActive)
            return;

        const auto dL = detL.process (inL);
        const auto dR = detR.process (inR);
        const auto peak = std::max (std::abs (dL), std::abs (dR));
        const auto peakDb = peak > 1.0e-9f ? 20.0f * std::log10 (peak) : -120.0f;
        updateEnvelope (peakDb);

        const auto s = blendScale();
        l = inL + (l - inL) * s;
        r = inR + (r - inR) * s;
    }

    void processMono (float& x) noexcept
    {
        if (! enabled)
            return;

        const auto in = x;

        for (auto& section : sections)
            x = section.processMono (x);

        if (! dynamicActive)
            return;

        const auto d = detL.process (in);
        const auto peakDb = std::abs (d) > 1.0e-9f ? 20.0f * std::log10 (std::abs (d)) : -120.0f;
        updateEnvelope (peakDb);

        x = in + (x - in) * blendScale();
    }

    /** Magnitude of the reference filter actually running, in dB. */
    double referenceMagnitudeDb (double freq) const noexcept
    {
        double m = 1.0;
        for (int i = 0; i < stages; ++i)
            m *= fx::biquadMagnitude (sections[static_cast<size_t> (i)].coefficients(), sr, freq);
        return 20.0 * std::log10 (std::max (1.0e-9, m));
    }

    /** Static magnitude of the band at `freq` in dB (the static gain, not the
        current dynamic position). */
    double staticMagnitude (double freq) const noexcept
    {
        if (! enabled)
            return 1.0;

        const bool gainShape = shapeHasGain (spec.type);

        if (! gainShape)
        {
            double m = 1.0;
            for (int i = 0; i < stages; ++i)
                m *= fx::biquadMagnitude (sections[static_cast<size_t> (i)].coefficients(), sr, freq);
            return m;
        }

        // Build the response at the static gain, not the dynamic reference.
        double m = 1.0;
        const auto perStage = spec.gainDb / (double) stages;

        for (int i = 0; i < stages; ++i)
        {
            const auto q = fx::shapeUsesButterworth (toFxShape (spec.type))
                               ? fx::butterworthQ (stages, i)
                               : std::max (0.05, (double) spec.q);
            const auto c = fx::BiquadCoeffs::from (fx::filterTypeFor (toFxShape (spec.type)),
                                                  sr, spec.freq, q, perStage);
            m *= fx::biquadMagnitude (c, sr, freq);
        }

        return m;
    }

private:
    void updateEnvelope (float peakDb) noexcept
    {
        const auto target = std::clamp ((peakDb - spec.dynThreshDb) / 12.0f, 0.0f, 1.0f);
        const auto coeff = target > env ? attackCoeff : releaseCoeff;
        env = target + (env - target) * coeff;
        dynGainDb = spec.dynRangeDb * env;
    }

    float blendScale() noexcept
    {
        const auto totalGain = spec.gainDb + dynGainDb;

        if (! scaleValid || std::abs (totalGain - cachedGainDb) > 0.01f)
        {
            const auto denom = std::pow (10.0, refGainDb / 20.0) - 1.0;
            const auto numer = std::pow (10.0, (double) totalGain / 20.0) - 1.0;
            cachedScale = std::abs (denom) > 1.0e-9 ? (float) (numer / denom) : 1.0f;
            cachedGainDb = totalGain;
            scaleValid = true;
        }

        return cachedScale;
    }

    double sr { 48000.0 };
    int maxStages { 4 };
    int stages { 1 };
    EqBandSettings spec {};
    bool enabled { true };
    bool dynamicActive { false };
    double refGainDb { 0.0 };

    std::vector<fx::StereoBand> sections;
    fx::Biquad detL, detR;

    float env { 0.0f }, dynGainDb { 0.0f };
    float attackCoeff { 0.99f }, releaseCoeff { 0.999f };
    float cachedGainDb { 1.0e9f }, cachedScale { 1.0f };
    bool scaleValid { false };
};

//==============================================================================
/** Eight-band parametric EQ with per-band dynamics and M/S placement. */
class ProEq
{
public:
    static constexpr int maxBands = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;

        for (auto& band : bands)
            band.prepare (sr, 4);

        hpf.prepare (sr, 4);
        lpf.prepare (sr, 4);

        for (auto& spec : specs)
            spec.freq = -1.0f;
        for (auto& a : active)
            a = false;

        reset();
    }

    void reset() noexcept
    {
        for (auto& band : bands)
            band.reset();
        hpf.reset();
        lpf.reset();
    }

    void setBand (int index, const EqBandSettings& s) noexcept
    {
        if (index < 0 || index >= maxBands)
            return;

        auto& spec = specs[static_cast<size_t> (index)];

        const bool same = spec.on == s.on && spec.type == s.type && spec.freq == s.freq
                       && spec.gainDb == s.gainDb && spec.q == s.q && spec.slope == s.slope
                       && spec.channel == s.channel && spec.dynOn == s.dynOn
                       && spec.dynThreshDb == s.dynThreshDb && spec.dynRangeDb == s.dynRangeDb
                       && spec.dynAttackMs == s.dynAttackMs && spec.dynReleaseMs == s.dynReleaseMs;

        if (same)
            return;

        spec = s;

        const bool gainShape = shapeHasGain (s.type);
        const bool audible = s.on && (! gainShape
                                      || std::abs (s.gainDb) >= 0.01f
                                      || (s.dynOn && std::abs (s.dynRangeDb) >= 0.01f));

        active[static_cast<size_t> (index)] = audible;
        auto& band = bands[static_cast<size_t> (index)];
        band.setEnabled (audible);
        band.configure (s);
    }

    /** HPF / LPF are their own two bands with independent slopes. */
    void setFilters (bool hpfOn, float hpfHz, int hpfSlope,
                     bool lpfOn, float lpfHz, int lpfSlope) noexcept
    {
        if (hpfSpec != hpfOn || hpfFreq != hpfHz || hpfOrder != orderFromSlope (hpfSlope))
        {
            hpfSpec = hpfOn;
            hpfFreq = hpfHz;
            hpfOrder = orderFromSlope (hpfSlope);

            EqBandSettings s;
            s.type = 3;
            s.freq = std::clamp (hpfHz, 10.0f, (float) (sr * 0.45));
            s.gainDb = 0.0f;
            s.q = 0.707f;
            s.slope = hpfSlope;
            s.on = hpfOn;
            hpf.setEnabled (hpfOn);
            hpf.configure (s);
        }

        if (lpfSpec != lpfOn || lpfFreq != lpfHz || lpfOrder != orderFromSlope (lpfSlope))
        {
            lpfSpec = lpfOn;
            lpfFreq = lpfHz;
            lpfOrder = orderFromSlope (lpfSlope);

            EqBandSettings s;
            s.type = 4;
            s.freq = std::clamp (lpfHz, 10.0f, (float) (sr * 0.45));
            s.gainDb = 0.0f;
            s.q = 0.707f;
            s.slope = lpfSlope;
            s.on = lpfOn;
            lpf.setEnabled (lpfOn);
            lpf.configure (s);
        }
    }

    void process (float& l, float& r) noexcept
    {
        hpf.process (l, r);

        bool needsMs = false;
        for (int i = 0; i < maxBands; ++i)
            needsMs = needsMs || (specs[static_cast<size_t> (i)].channel != 0
                                  && active[static_cast<size_t> (i)]);

        if (needsMs)
        {
            float m = 0.5f * (l + r);
            float s = 0.5f * (l - r);

            for (int i = 0; i < maxBands; ++i)
            {
                if (! active[static_cast<size_t> (i)] || specs[static_cast<size_t> (i)].channel == 0)
                    continue;

                if (specs[static_cast<size_t> (i)].channel == 1)
                    bands[static_cast<size_t> (i)].processMono (m);
                else
                    bands[static_cast<size_t> (i)].processMono (s);
            }

            l = m + s;
            r = m - s;
        }

        for (int i = 0; i < maxBands; ++i)
        {
            if (! active[static_cast<size_t> (i)] || specs[static_cast<size_t> (i)].channel != 0)
                continue;
            bands[static_cast<size_t> (i)].process (l, r);
        }

        lpf.process (l, r);
    }

    float dynamicGainDb (int index) const noexcept
    {
        return index >= 0 && index < maxBands
                   ? bands[static_cast<size_t> (index)].dynamicGainDb()
                   : 0.0f;
    }

    float bandDebugScale (int index) const noexcept
    {
        return index >= 0 && index < maxBands ? bands[static_cast<size_t> (index)].debugScale() : 1.0f;
    }

    float bandDebugRefGain (int index) const noexcept
    {
        return index >= 0 && index < maxBands ? bands[static_cast<size_t> (index)].debugRefGainDb() : 0.0f;
    }

    double bandStaticDb (int index, double freq) const noexcept
    {
        if (index < 0 || index >= maxBands)
            return 0.0;
        return 20.0 * std::log10 (std::max (1.0e-9, bands[static_cast<size_t> (index)].staticMagnitude (freq)));
    }

    double bandReferenceDb (int index, double freq) const noexcept
    {
        return index >= 0 && index < maxBands
                   ? bands[static_cast<size_t> (index)].referenceMagnitudeDb (freq)
                   : 0.0;
    }

    /** Static magnitude response in dB at `freq` (mid path for M/S). */
    double responseDb (double freq) const noexcept
    {
        double mag = 1.0;
        mag *= hpfSpec ? hpf.staticMagnitude (freq) : 1.0;
        mag *= lpfSpec ? lpf.staticMagnitude (freq) : 1.0;

        for (int i = 0; i < maxBands; ++i)
            if (active[static_cast<size_t> (i)])
                mag *= bands[static_cast<size_t> (i)].staticMagnitude (freq);

        return 20.0 * std::log10 (std::max (1.0e-9, mag));
    }

private:
    double sr { 48000.0 };
    std::array<EqBandV2, maxBands> bands;
    std::array<EqBandSettings, maxBands> specs {};
    std::array<bool, maxBands> active {};
    EqBandV2 hpf, lpf;
    bool hpfSpec { false }, lpfSpec { false };
    float hpfFreq { -1.0f }, lpfFreq { -1.0f };
    int hpfOrder { 1 }, lpfOrder { 1 };
};

//==============================================================================
/** Saturn-style saturation settings. */
struct SaturnSettings
{
    float driveDb { 0.0f };
    float mix { 1.0f };
    float bias { 0.0f };
    float toneBass { 0.0f };
    float toneMid { 0.0f };
    float toneTreble { 0.0f };
    float tonePresence { 0.0f };
    float dynamics { 0.0f };   // -1 gate/expand .. +1 compress
    float feedback { 0.0f };   // 0..1
    float feedbackHz { 220.0f };
    float outputDb { 0.0f };
    int style { 0 };
    bool hq { true };          // first-order ADAA
};

class SaturnSat
{
public:
    enum Style
    {
        tape = 0,
        tube,
        diode,
        soft,
        warm,
        fold,
        fuzz,
        crunch
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;

        dcCoeff = (float) std::exp (-2.0 * 3.14159265358979 * 20.0 / sr);
        envAttack = (float) std::exp (-1.0 / (0.010 * sr));
        envRelease = (float) std::exp (-1.0 / (0.120 * sr));

        maxFeedback = (int) std::ceil (sr / 20.0) + 2; // 20 Hz floor
        for (auto& delay : feedbackDelay)
            delay.assign ((size_t) maxFeedback, 0.0f);

        toneBassFilter.reset();
        toneMidFilter.reset();
        toneTrebleFilter.reset();
        tonePresenceFilter.reset();
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            dcState[static_cast<size_t> (ch)] = 0.0f;
            env[static_cast<size_t> (ch)] = 0.0f;
            prevX[static_cast<size_t> (ch)] = 0.0f;
            prevF[static_cast<size_t> (ch)] = -1.0e9f;
            feedbackPos[static_cast<size_t> (ch)] = 0;
            std::fill (feedbackDelay[static_cast<size_t> (ch)].begin(),
                       feedbackDelay[static_cast<size_t> (ch)].end(), 0.0f);
        }

        toneBassFilter.reset();
        toneMidFilter.reset();
        toneTrebleFilter.reset();
        tonePresenceFilter.reset();
    }

    void setSettings (const SaturnSettings& s) noexcept
    {
        settings = s;
        enabled = s.mix > 0.0f && s.driveDb > 0.001f;

        const auto bassDb = std::clamp (s.toneBass, -1.0f, 1.0f) * 6.0f;
        const auto midDb = std::clamp (s.toneMid, -1.0f, 1.0f) * 6.0f;
        const auto trebleDb = std::clamp (s.toneTreble, -1.0f, 1.0f) * 6.0f;
        const auto presenceDb = std::clamp (s.tonePresence, -1.0f, 1.0f) * 6.0f;

        toneBassFilter.set (fx::FilterType::lowShelf, sr, 120.0, 0.707, bassDb);
        toneMidFilter.set (fx::FilterType::peak, sr, 1000.0, 0.8, midDb);
        toneTrebleFilter.set (fx::FilterType::highShelf, sr, 8000.0, 0.707, trebleDb);
        tonePresenceFilter.set (fx::FilterType::peak, sr, 3500.0, 1.0, presenceDb);

        const auto freq = std::clamp (s.feedbackHz, 20.0f, (float) (sr * 0.45));
        feedbackSamples = std::clamp ((int) std::lround (sr / freq), 4, maxFeedback - 1);
        outputGain = std::pow (10.0f, std::clamp (s.outputDb, -24.0f, 24.0f) / 20.0f);
    }

    void process (float& l, float& r) noexcept
    {
        if (! enabled)
            return;

        const auto dryL = l;
        const auto dryR = r;

        auto wetL = processChannel (0, l);
        auto wetR = processChannel (1, r);

        toneBassFilter.process (wetL, wetR);
        toneMidFilter.process (wetL, wetR);
        toneTrebleFilter.process (wetL, wetR);
        tonePresenceFilter.process (wetL, wetR);

        const auto mix = std::clamp (settings.mix, 0.0f, 1.0f);
        l = dryL * (1.0f - mix) + wetL * mix;
        r = dryR * (1.0f - mix) + wetR * mix;

        blockDc (l, 0);
        blockDc (r, 1);
    }

    float currentDriveDb (int channel) const noexcept
    {
        const auto ch = static_cast<size_t> (std::clamp (channel, 0, 1));
        return settings.driveDb + std::max (0.0f, settings.dynamics) * 18.0f * env[ch];
    }

private:
    static constexpr double pi = 3.14159265358979323846;

    float processChannel (int channel, float x) noexcept
    {
        const auto ch = static_cast<size_t> (std::clamp (channel, 0, 1));

        const auto peak = std::abs (x);
        const auto envCoeff = peak > env[ch] ? envAttack : envRelease;
        env[ch] = peak + (env[ch] - peak) * envCoeff;

        // Feedback: a short comb feeding the shaper input, the way Saturn's
        // feedback control rings the band.
        float feedbackSample = 0.0f;
        if (settings.feedback > 0.001f && feedbackSamples > 1)
        {
            const auto size = (int) feedbackDelay[ch].size();
            const auto read = (feedbackPos[ch] - feedbackSamples + size) % size;
            feedbackSample = feedbackDelay[ch][static_cast<size_t> (read)] * settings.feedback;
            x += feedbackSample;
        }

        const auto driveDb = currentDriveDb (channel);
        const auto pre = std::pow (10.0f, driveDb / 20.0f);
        const auto post = 1.0f / std::max (1.0f, std::sqrt (pre));

        auto y = x * pre + std::clamp (settings.bias, -1.0f, 1.0f);
        y = settings.hq ? adaaShape (ch, y) : shape (y);

        // Input-driven dynamics: positive compresses loud material, negative
        // expands/gates quiet material. Bounded so it stays musical.
        if (std::abs (settings.dynamics) > 0.001f)
        {
            const auto amount = std::clamp (settings.dynamics, -1.0f, 1.0f);

            if (amount > 0.0f)
            {
                const auto gain = 1.0f - amount * 0.55f * env[ch];
                y *= std::max (0.2f, gain);
            }
            else
            {
                const auto gain = 1.0f + (-amount) * 0.8f * (0.5f - env[ch]);
                y *= std::clamp (gain, 0.25f, 2.0f);
            }
        }

        y *= post * outputGain;

        if (settings.feedback > 0.001f && feedbackSamples > 1)
        {
            const auto size = (int) feedbackDelay[ch].size();
            feedbackDelay[ch][static_cast<size_t> (feedbackPos[ch])] = y;
            feedbackPos[ch] = (feedbackPos[ch] + 1) % size;
        }

        return y;
    }

    float shape (float x) const noexcept
    {
        switch (settings.style)
        {
            case tube:
            {
                const auto y = std::tanh (x * 0.85f + 0.14f * x * x);
                return y / 0.92f;
            }
            case diode:
                return (std::tanh (x) + 0.5f * std::tanh (0.5f * x)) / 1.5f;
            case soft:
                return x / (1.0f + std::abs (x));
            case warm:
                return std::tanh (x + 0.12f * x * x * x) / 1.02f;
            case fold:
                return std::sin (x);
            case fuzz:
                return std::copysign (1.0f - std::exp (-2.0f * std::abs (x)), x) / 0.86f;
            case crunch:
                return std::tanh (x * 1.25f + 0.22f * x * x) / 1.05f;
            case tape:
            default:
                return std::tanh (x);
        }
    }

    float antiderivative (float x) const noexcept
    {
        switch (settings.style)
        {
            case tape:
                return std::log (std::cosh (x));
            case diode:
                return (std::log (std::cosh (x)) + std::log (std::cosh (0.5f * x))) / 1.5f;
            case soft:
            {
                const auto a = std::abs (x);
                return a - std::log1p (a);
            }
            case warm:
            {
                const auto x2 = x * x;
                return 0.5f * x2 - (1.0f / 12.0f) * x2 * x2;
            }
            case fold:
                return -std::cos (x);
            case fuzz:
            {
                const auto a = std::abs (x);
                return (a - 0.5f * (1.0f - std::exp (-2.0f * a))) / 0.86f;
            }
            case tube:
            case crunch:
            default:
                return 0.5f * x * x; // unused
        }
    }

    float adaaShape (int channel, float x) noexcept
    {
        const bool usable = settings.style == tape || settings.style == diode
                          || settings.style == soft || settings.style == warm
                          || settings.style == fold;
        if (! usable)
            return shape (x);

        const auto ch = static_cast<size_t> (std::clamp (channel, 0, 1));
        const auto f1 = antiderivative (x);

        // First sample after reset: seed the antiderivative state instead of
        // dividing by a sentinel.
        if (prevF[ch] < -1.0e8f)
        {
            prevX[ch] = x;
            prevF[ch] = f1;
            return shape (x);
        }

        const auto x0 = prevX[ch];
        const auto f0 = prevF[ch];
        prevX[ch] = x;
        prevF[ch] = f1;

        const auto dx = x - x0;
        if (std::abs (dx) < 1.0e-5f)
            return shape (0.5f * (x + x0));

        return (f1 - f0) / dx;
    }

    void blockDc (float& v, int channel) noexcept
    {
        const auto ch = static_cast<size_t> (std::clamp (channel, 0, 1));
        auto& s = dcState[ch];
        s = v * (1.0f - dcCoeff) + s * dcCoeff;
        v -= s;
    }

    double sr { 48000.0 };
    SaturnSettings settings {};
    bool enabled { false };
    float outputGain { 1.0f };

    float dcCoeff { 0.999f }, envAttack { 0.99f }, envRelease { 0.999f };
    int maxFeedback { 2402 }, feedbackSamples { 1 };

    fx::StereoBand toneBassFilter, toneMidFilter, toneTrebleFilter, tonePresenceFilter;

    std::array<float, 2> dcState {}, env {}, prevX {}, prevF {};
    std::array<std::vector<float>, 2> feedbackDelay;
    std::array<int, 2> feedbackPos { 0, 0 };
};

//==============================================================================
/** Multiband Saturn: 1-4 phase-coherent bands, each with its own saturator. */
class MultibandSaturn
{
public:
    static constexpr int maxBands = 4;

    struct Band
    {
        float driveDb { 6.0f };
        float mix { 0.0f };
        int style { 0 };
        float dynamics { 0.0f };
        float toneBass { 0.0f };
        float toneMid { 0.0f };
        float toneTreble { 0.0f };
        float tonePresence { 0.0f };
        float feedback { 0.0f };
        float levelDb { 0.0f };
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        xo.prepare (sr);
        for (auto& sat : bands)
            sat.prepare (sr);
        reset();
    }

    void reset() noexcept
    {
        xo.reset();
        for (auto& sat : bands)
            sat.reset();
    }

    void configure (int bandCount, float x1, float x2, float x3,
                    const std::array<Band, maxBands>& b) noexcept
    {
        xo.setBands (bandCount, x1, x2, x3);

        for (int k = 0; k < maxBands; ++k)
        {
            const auto& band = b[static_cast<size_t> (k)];
            SaturnSettings s;
            s.driveDb = band.driveDb;
            s.mix = band.mix;
            s.style = band.style;
            s.dynamics = band.dynamics;
            s.toneBass = band.toneBass;
            s.toneMid = band.toneMid;
            s.toneTreble = band.toneTreble;
            s.tonePresence = band.tonePresence;
            s.feedback = band.feedback;
            s.outputDb = 0.0f; // per-band level is applied once below
            s.hq = true;
            bands[static_cast<size_t> (k)].setSettings (s);
            level[static_cast<size_t> (k)] = std::pow (10.0f, band.levelDb / 20.0f);
        }
    }

    void process (float* l, float* r, int n) noexcept
    {
        const int nb = xo.bands();

        for (int i = 0; i < n; ++i)
        {
            std::array<float, maxBands> bl {}, br {};
            xo.split (0, l[i], bl.data());
            xo.split (1, r[i], br.data());

            float outL = 0.0f, outR = 0.0f;

            for (int k = 0; k < nb; ++k)
            {
                float a = bl[static_cast<size_t> (k)];
                float b = br[static_cast<size_t> (k)];
                bands[static_cast<size_t> (k)].process (a, b);
                outL += a * level[static_cast<size_t> (k)];
                outR += b * level[static_cast<size_t> (k)];
            }

            l[i] = outL;
            r[i] = outR;
        }
    }

private:
    double sr { 48000.0 };
    fx::Crossover xo;
    std::array<SaturnSat, maxBands> bands;
    std::array<float, maxBands> level { 1, 1, 1, 1 };
};

//==============================================================================
/** Classic compressor characters on top of the vendored detector. */
class MixCompressor
{
public:
    enum Type
    {
        vca = 0,
        fet,
        opto,
        variMu,
        bus,
        punch,
        transparent,
        limiter
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        comp.prepare (sr, 2);
        comp.reset();
    }

    void configure (int typeIndex, float thresholdDb, float ratio, float attackMs,
                    float releaseMs, float kneeDb, float makeupDb, float mixAmount,
                    float sidechainHpf, float lookaheadMs, float holdMs,
                    float rangeDb, float amount, bool autoRelease, bool autoGain,
                    float scEqFreq, float scEqGain, float scEqQ) noexcept
    {
        const auto type = std::clamp (typeIndex, 0, 7);

        fx::Compressor::Params p;
        p.thresholdDb = thresholdDb;
        p.ratio = std::max (1.0f, ratio);
        p.attackMs = std::max (0.05f, attackMs);
        p.releaseMs = std::max (1.0f, releaseMs);
        p.kneeDb = std::max (0.0f, kneeDb);
        p.makeupDb = makeupDb;
        p.mix = std::clamp (mixAmount, 0.0f, 1.0f);
        p.sidechainHpfHz = std::clamp (sidechainHpf, 1.0f, 1000.0f);
        p.lookaheadMs = std::clamp (lookaheadMs, 0.0f, 20.0f);
        p.holdMs = std::clamp (holdMs, 0.0f, 500.0f);
        p.rangeDb = std::clamp (rangeDb, 0.0f, 60.0f);
        p.amount = std::clamp (amount, 0.0f, 2.0f);
        p.autoRelease = autoRelease;
        p.autoGain = autoGain;
        p.scEqFreqHz = scEqFreq;
        p.scEqGainDb = scEqGain;
        p.scEqQ = std::max (0.05f, scEqQ);

        switch (type)
        {
            case fet:
                p.type = fx::Compressor::Type::fet;
                break;
            case opto:
                p.type = fx::Compressor::Type::opto;
                break;
            case variMu:
                p.type = fx::Compressor::Type::varimu;
                break;
            case bus:
                p.type = fx::Compressor::Type::vca;
                p.kneeDb = std::max (p.kneeDb, 4.0f);
                p.ratio = std::max (1.0f, p.ratio * 0.85f);
                p.attackMs *= 2.0f;
                p.releaseMs *= 2.5f;
                p.autoRelease = true;
                break;
            case punch:
                p.type = fx::Compressor::Type::fet;
                p.attackMs *= 0.5f;
                p.ratio = std::max (1.0f, p.ratio * 1.25f);
                p.kneeDb *= 0.5f;
                break;
            case transparent:
                p.type = fx::Compressor::Type::vca;
                p.kneeDb = std::max (p.kneeDb, 10.0f);
                p.attackMs = std::max (p.attackMs, 20.0f);
                p.releaseMs = std::max (p.releaseMs, 250.0f);
                p.ratio = std::clamp (p.ratio, 1.0f, 3.0f);
                break;
            case limiter:
                p.type = fx::Compressor::Type::vca;
                p.kneeDb = 0.0f;
                p.ratio = std::max (20.0f, p.ratio);
                p.attackMs = 0.05f;
                p.releaseMs = std::min (p.releaseMs, 80.0f);
                break;
            case vca:
            default:
                p.type = fx::Compressor::Type::vca;
                break;
        }

        comp.setParams (p);
        activeType = type;
    }

    /** Output-stage colour, input drive, output trim and a bounded feedback
        path around the compressor. The character maths is type-specific so
        VCA stays clean, FET gets odd-order edge, Opto/VariMu get tube even
        harmonics and Bus gets a subtle second-order glue. */
    void setColour (float colourAmount, float driveDb, float outputDb, float feedbackAmount) noexcept
    {
        colour = std::clamp (colourAmount, 0.0f, 1.0f);
        driveGain = std::pow (10.0f, std::clamp (driveDb, -24.0f, 24.0f) / 20.0f);
        outputGain = std::pow (10.0f, std::clamp (outputDb, -24.0f, 24.0f) / 20.0f);
        feedback = std::clamp (feedbackAmount, 0.0f, 0.45f);
    }

    void process (float& l, float& r) noexcept
    {
        const auto inL = l + feedbackStateL * feedback;
        const auto inR = r + feedbackStateR * feedback;

        auto drivenL = inL * driveGain;
        auto drivenR = inR * driveGain;
        comp.process (drivenL, drivenR);

        drivenL *= outputGain;
        drivenR *= outputGain;

        if (colour > 1.0e-4f)
        {
            drivenL = colourise (drivenL, activeType, colour);
            drivenR = colourise (drivenR, activeType, colour);
        }

        feedbackStateL = drivenL;
        feedbackStateR = drivenR;
        l = drivenL;
        r = drivenR;
    }

    void reset() noexcept
    {
        comp.reset();
        feedbackStateL = feedbackStateR = 0.0f;
    }

    float gainReductionDb() const noexcept { return comp.gainReductionDb(); }

private:
    static float colourise (float x, int type, float amount) noexcept
    {
        const auto a = std::clamp (amount, 0.0f, 1.0f);
        if (a <= 1.0e-4f)
            return x;

        float shaped = x;
        switch (type)
        {
            case 1: // FET: odd-order edge
                shaped = std::tanh (x * 1.35f) / 1.35f;
                break;
            case 2: // Opto: tube even harmonics
                shaped = std::tanh (x + 0.18f * x * x) / 1.06f;
                break;
            case 3: // VariMu: tube + transformer bloom
                shaped = std::tanh (x + 0.12f * x * x + 0.04f * x * x * x) / 1.08f;
                break;
            case 4: // Bus: second-order glue
                shaped = x + 0.07f * x * x / (1.0f + std::abs (x));
                break;
            case 5: // Punch: hard FET edge
                shaped = std::tanh (x * 1.6f) / 1.6f;
                break;
            case 6: // Transparent
            case 7: // Limiter
                shaped = x;
                break;
            case 0: // VCA: clean transformer hint
            default:
                shaped = x + 0.04f * x * x / (1.0f + std::abs (x));
                break;
        }

        return x + a * (shaped - x);
    }

    double sr { 48000.0 };
    fx::Compressor comp;
    int activeType { 0 };
    float colour { 0.0f }, driveGain { 1.0f }, outputGain { 1.0f };
    float feedback { 0.0f }, feedbackStateL { 0.0f }, feedbackStateR { 0.0f };
};

//==============================================================================
/** Up to four phase-coherent bands, each with its own typed compressor. */
class MultibandCompressor
{
public:
    static constexpr int maxBands = 4;

    struct Band
    {
        bool on { true };
        float threshDb { 0.0f };
        float ratio { 2.0f };
        float attackMs { 20.0f };
        float releaseMs { 150.0f };
        float gainDb { 0.0f };
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        xo.prepare (sr);
        for (auto& c : comps)
            c.prepare (sr);
        reset();
    }

    void reset() noexcept
    {
        xo.reset();
        for (auto& c : comps)
            c.reset();
    }

    void configure (int bands, float x1, float x2, float x3, int type,
                    float kneeDb, float mixAmount, const std::array<Band, maxBands>& b) noexcept
    {
        xo.setBands (bands, x1, x2, x3);
        mix = std::clamp (mixAmount, 0.0f, 1.0f);

        for (int k = 0; k < maxBands; ++k)
        {
            const auto& band = b[static_cast<size_t> (k)];
            bandOn[static_cast<size_t> (k)] = band.on;
            makeup[static_cast<size_t> (k)] = std::pow (10.0f, band.gainDb / 20.0f);
            ratio1[static_cast<size_t> (k)] = std::max (1.0f, band.ratio) <= 1.001f;

            comps[static_cast<size_t> (k)].configure (
                type, band.threshDb, band.ratio, band.attackMs, band.releaseMs,
                kneeDb, band.gainDb, 1.0f, 1.0f, 0.0f, 0.0f, 60.0f, 1.0f,
                false, false, 1000.0f, 0.0f, 1.0f);
        }
    }

    void process (float* l, float* r, int n) noexcept
    {
        const int nb = xo.bands();
        std::array<float, maxBands> gr {};

        for (int i = 0; i < n; ++i)
        {
            std::array<float, maxBands> bl {}, br {};
            xo.split (0, l[i], bl.data());
            xo.split (1, r[i], br.data());

            float dryL = 0.0f, dryR = 0.0f, wetL = 0.0f, wetR = 0.0f;

            for (int k = 0; k < nb; ++k)
            {
                dryL += bl[static_cast<size_t> (k)];
                dryR += br[static_cast<size_t> (k)];

                if (! bandOn[static_cast<size_t> (k)])
                {
                    wetL += bl[static_cast<size_t> (k)];
                    wetR += br[static_cast<size_t> (k)];
                    continue;
                }

                float a = bl[static_cast<size_t> (k)];
                float b = br[static_cast<size_t> (k)];

                if (ratio1[static_cast<size_t> (k)])
                {
                    a *= makeup[static_cast<size_t> (k)];
                    b *= makeup[static_cast<size_t> (k)];
                }
                else
                {
                    comps[static_cast<size_t> (k)].process (a, b);
                }

                gr[static_cast<size_t> (k)] = std::max (gr[static_cast<size_t> (k)],
                                                        comps[static_cast<size_t> (k)].gainReductionDb());
                wetL += a;
                wetR += b;
            }

            l[i] = dryL + mix * (wetL - dryL);
            r[i] = dryR + mix * (wetR - dryR);
        }

        for (int k = 0; k < maxBands; ++k)
            bandGr[static_cast<size_t> (k)].store (bandOn[static_cast<size_t> (k)] ? gr[static_cast<size_t> (k)] : 0.0f,
                                                   std::memory_order_relaxed);
    }

    float gainReductionDb (int band) const noexcept
    {
        return bandGr[static_cast<size_t> (std::clamp (band, 0, maxBands - 1))].load (std::memory_order_relaxed);
    }

private:
    double sr { 48000.0 };
    fx::Crossover xo;
    std::array<MixCompressor, maxBands> comps;
    std::array<bool, maxBands> bandOn { true, true, true, true };
    std::array<float, maxBands> makeup { 1, 1, 1, 1 };
    std::array<bool, maxBands> ratio1 {};
    std::array<std::atomic<float>, maxBands> bandGr {};
    float mix { 1.0f };
};

} // namespace nc::mixrack
