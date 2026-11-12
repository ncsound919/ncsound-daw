#pragma once

#include <nc/engine/StudioFxDsp.hpp>
#include <nc/engine/StudioFxEq.hpp>
#include <nc/engine/StudioFxSlot.hpp>

#include <array>
#include <cstdint>

namespace nc::engine::studiofx
{
/** Resonant 4-pole ladder: Moog / Juno / Prophet families. */
class LadderFilterModule final : public StudioFxModule
{
public:
    explicit LadderFilterModule (float sampleRate) noexcept
        : fs (sampleRate)
    {
    }

    const char* kind() const noexcept override { return "LADDER_FILTER"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        cutoffHz = slotValue (slot, "cutoffHz");
        resonance = slotValue (slot, "resonance");
        drive = slotValue (slot, "drive");
        mode = std::min (2, std::max (0, static_cast<int> (slotValue (slot, "mode"))));
        const float sr = fs > 0.0f ? fs : 44100.0f;
        const float fc = std::min (std::max (cutoffHz, 1.0f), sr * 0.45f);
        const float w0 = 6.2831855f * fc / sr;
        g = std::min (static_cast<float> (std::tan (w0 * 0.5f)), 0.95f);
        const float bigG = g / (1.0f + g);
        g4 = bigG * bigG * bigG * bigG;
        const float ceiling = mode == 1 ? 3.9f : mode == 2 ? 3.99f : 3.95f;
        k = std::min (resonance * (ceiling / 20.0f), ceiling);
        driveScale = mode == 1 ? 1.2f : mode == 2 ? 1.6f : 2.0f;
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        inMeter.update (inL, inR);
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        const float l = render (inL, stateL);
        const float r = render (inR, stateR);
        outMeter.update (l, r);
        out[0] = l;
        out[1] = r;
    }

    void reset() noexcept override
    {
        stateL.fill (0.0f);
        stateR.fill (0.0f);
        inMeter.reset();
        outMeter.reset();
    }

private:
    float render (float x, std::array<float, 4>& state) noexcept
    {
        const float bigG = g / (1.0f + g);
        const float bigG2 = bigG * bigG;
        const float driven = static_cast<float> (std::tanh (x * (1.0f + (drive / 10.0f) * driveScale)));
        const float oneMinus = 1.0f - bigG;
        const float s0Term = state[0] * oneMinus * bigG2 * bigG;
        const float s1Term = state[1] * oneMinus * bigG2;
        const float s2Term = state[2] * oneMinus * bigG;
        const float s3Term = state[3] * oneMinus;
        const float sum = (s0Term + s1Term) + (s2Term + s3Term);
        const float u = (driven - k * sum) / (1.0f + k * g4);
        const float v0 = (u - state[0]) * bigG;
        const float y0 = v0 + state[0];
        state[0] = y0 + v0;
        const float v1 = (y0 - state[1]) * bigG;
        const float y1 = v1 + state[1];
        state[1] = y1 + v1;
        const float v2 = (y1 - state[2]) * bigG;
        const float y2 = v2 + state[2];
        state[2] = y2 + v2;
        const float v3 = (y2 - state[3]) * bigG;
        const float y3 = v3 + state[3];
        state[3] = y3 + v3;
        if (! std::isfinite (y3))
        {
            state.fill (0.0f);
            return 0.0f;
        }
        return y3;
    }

    float fs;
    float cutoffHz = 1200.0f, resonance = 4.0f, drive = 0.0f;
    int mode = 0;
    float g = 0.0f, g4 = 0.0f, k = 0.0f, driveScale = 1.0f;
    std::array<float, 4> stateL = {}, stateR = {};
    PeakMeter inMeter, outMeter;
};

/** Deterministic self-noise, identical to the TS `makeNoiseSource`. */
class LadderNoiseSource
{
public:
    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        const uint32_t u = (state >> 8) & 0xffffffu;
        return static_cast<float> (static_cast<double> (u) / 16777216.0 - 0.5);
    }

    void reset() noexcept { state = 0x12345678u; }

private:
    uint32_t state = 0x12345678u;
};

/** PHONE_NOISE_REDUCTION: broadband gate plus wind/handling detectors. */
class PhoneNoiseReductionModule final : public StudioFxModule
{
public:
    explicit PhoneNoiseReductionModule (float sampleRate)
        : fs (sampleRate)
        , windLpL (sampleRate)
        , windLpR (sampleRate)
    {
        windLpL.setLowPass (150.0f, 0.7071f);
        windLpR.setLowPass (150.0f, 0.7071f);
    }

    const char* kind() const noexcept override { return "PHONE_NOISE_REDUCTION"; }
    float gainReductionDb() const noexcept override { return -levelToDb (gain); }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        reductionDb = slotValue (slot, "reductionDb");
        thresholdDb = slotValue (slot, "thresholdDb");
        smoothingMs = slotValue (slot, "smoothingMs");
        windDetection = slotValue (slot, "windDetection") >= 0.5f;
        handlingDetection = slotValue (slot, "handlingDetection") >= 0.5f;
        minGain = dbToLinear (-reductionDb);
        thresholdLinear = dbToLinear (thresholdDb);
        windDepth = std::min (std::max (1.0f - minGain, 0.0f), 0.9f);
        duckDepth = std::min (std::max (1.0f - static_cast<float> (std::sqrt (minGain)), 0.0f), 0.9f);
        envRelease = dynCoeff (smoothingMs, fs);
        gainSmooth = dynCoeff (smoothingMs * 0.25f, fs);
        refHold = dynCoeff (smoothingMs, fs);
        duckRelease = dynCoeff (std::min (smoothingMs, 80.0f), fs);
        windSmooth = dynCoeff (smoothingMs, fs);
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        const float peak = std::max (std::fabs (inL), std::fabs (inR));
        inMeter.update (peak, peak);
        env = std::max (peak, env * envRelease);
        const float ratio = std::min (std::max (env / thresholdLinear, 0.0f), 1.0f);
        const float closing = std::min (std::max ((1.0f - ratio) / (1.0f - 0.25f), 0.0f), 1.0f);
        const float targetGain = 1.0f - closing * (1.0f - minGain);
        gain = targetGain + gainSmooth * (gain - targetGain);
        duck = duck * duckRelease;
        if (handlingDetection && handlingRef > 1e-4f && peak > handlingRef * 3.0f)
            duck = duckDepth;
        handlingRef = std::max (peak, handlingRef * refHold);
        const float lpL = windLpL.process (inL);
        const float lpR = windLpR.process (inR);
        const float lpMono = std::fabs ((lpL + lpR) * 0.5f);
        const float fullMono = std::fabs ((inL + inR) * 0.5f);
        lfEnv = std::max (lpMono, lfEnv * windSmooth);
        fullEnv = std::max (fullMono, fullEnv * windSmooth);
        const float dominance = windDetection ? lfEnv / std::max (fullEnv, 1e-6f) : 0.0f;
        const float windTarget = std::min (std::max ((dominance - 0.7f) / (1.0f - 0.7f), 0.0f), 1.0f);
        windAmount = windTarget + windSmooth * (windAmount - windTarget);
        const float applied = gain * (1.0f - duck);
        const float cut = windAmount * windDepth;
        out[0] = (inL - cut * lpL) * applied;
        out[1] = (inR - cut * lpR) * applied;
        outMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        env = 0.0f;
        gain = 1.0f;
        lfEnv = fullEnv = windAmount = 0.0f;
        handlingRef = duck = 0.0f;
        windLpL.reset();
        windLpR.reset();
        inMeter.reset();
        outMeter.reset();
    }

private:
    float fs;
    float reductionDb = 12.0f, thresholdDb = -35.0f, smoothingMs = 100.0f;
    bool windDetection = true, handlingDetection = true;
    float env = 0.0f, gain = 1.0f;
    Biquad windLpL, windLpR;
    float lfEnv = 0.0f, fullEnv = 0.0f, windAmount = 0.0f;
    float handlingRef = 0.0f, duck = 0.0f;
    float envRelease = 1.0f, gainSmooth = 1.0f, refHold = 1.0f;
    float duckRelease = 1.0f, windSmooth = 1.0f;
    float minGain = 1.0f, thresholdLinear = 0.0f, windDepth = 0.0f, duckDepth = 0.0f;
    PeakMeter inMeter, outMeter;
};

/** PHONE_MIC_EMULATION: EQ voicing plus proximity, noise and expansion. */
class PhoneMicEmulationModule final : public StudioFxModule
{
public:
    explicit PhoneMicEmulationModule (float sampleRate)
        : lowShelfL (sampleRate)
        , lowShelfR (sampleRate)
        , peak2k5L (sampleRate)
        , peak2k5R (sampleRate)
        , highShelfL (sampleRate)
        , highShelfR (sampleRate)
        , highPassL (sampleRate)
        , highPassR (sampleRate)
    {
        configureFilters();
    }

    const char* kind() const noexcept override { return "PHONE_MIC_EMULATION"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        targetMic = std::min (2, std::max (0, static_cast<int> (slotValue (slot, "targetMic"))));
        distanceMeters = slotValue (slot, "distanceMeters");
        proximityAmount = slotValue (slot, "proximityAmount");
        selfNoiseAmount = slotValue (slot, "selfNoiseAmount");
        dynamicRangeExpansion = slotValue (slot, "dynamicRangeExpansion");
        configureFilters();
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        // Mirrors the TS port, which voices every sample including bypassed:
        // `enabled` is configured but not consulted here.
        inMeter.update (inL, inR);
        float l = highPassL.process (inL);
        float r = highPassR.process (inR);
        l = lowShelfL.process (l);
        r = lowShelfR.process (r);
        l = peak2k5L.process (l);
        r = peak2k5R.process (r);
        l = highShelfL.process (l);
        r = highShelfR.process (r);
        const float proximityBoost = distanceMeters > 0.0f ? proximityAmount * std::min (std::max (1.0f / distanceMeters, 0.0f), 6.0f) : 0.0f;
        const float proximityGain = dbToLinear (proximityBoost);
        l = l * proximityGain;
        r = r * proximityGain;
        const float noise = noiseSource.next() * 0.002f * selfNoiseAmount;
        l = l + noise;
        r = r + noise;
        const float threshold = dbToLinear (-30.0f);
        l = expand (l, threshold, dynamicRangeExpansion);
        r = expand (r, threshold, dynamicRangeExpansion);
        outMeter.update (l, r);
        out[0] = l;
        out[1] = r;
    }

    void reset() noexcept override
    {
        for (Biquad* filter : { &lowShelfL, &lowShelfR, &peak2k5L, &peak2k5R, &highShelfL, &highShelfR, &highPassL, &highPassR })
            filter->reset();
        inMeter.reset();
        outMeter.reset();
        noiseSource.reset();
    }

private:
    void configureFilters() noexcept
    {
        float shelfFreq, shelfGain, peakGain, airFreq, airGain, hp;
        if (targetMic == 1)      { shelfFreq = 80.0f;  shelfGain = 4.0f;  peakGain = -3.0f; airFreq = 6000.0f;  airGain = -4.0f; hp = 50.0f; }
        else if (targetMic == 2) { shelfFreq = 100.0f; shelfGain = 2.0f;  peakGain = -1.0f; airFreq = 10000.0f; airGain = -1.0f; hp = 80.0f; }
        else                     { shelfFreq = 100.0f; shelfGain = 3.0f;  peakGain = -2.0f; airFreq = 8000.0f;   airGain = -3.0f; hp = 80.0f; }
        lowShelfL.setLowShelf (shelfFreq, shelfGain, 0.7f);
        lowShelfR.setLowShelf (shelfFreq, shelfGain, 0.7f);
        peak2k5L.setPeaking (2500.0f, peakGain, 1.5f);
        peak2k5R.setPeaking (2500.0f, peakGain, 1.5f);
        highShelfL.setHighShelf (airFreq, airGain, 0.7f);
        highShelfR.setHighShelf (airFreq, airGain, 0.7f);
        highPassL.setHighPass (hp, 0.7071f);
        highPassR.setHighPass (hp, 0.7071f);
    }

    static float expand (float value, float threshold, float amount) noexcept
    {
        const float abs = std::fabs (value);
        if (abs >= threshold || amount <= 0.0f)
            return value;
        const float boost = (threshold - abs) * amount * 2.0f;
        return value * (1.0f + boost);
    }

    int targetMic = 0;
    float distanceMeters = 0.3f, proximityAmount = 1.0f;
    float selfNoiseAmount = 0.5f, dynamicRangeExpansion = 0.5f;
    Biquad lowShelfL, lowShelfR, peak2k5L, peak2k5R;
    Biquad highShelfL, highShelfR, highPassL, highPassR;
    PeakMeter inMeter, outMeter;
    LadderNoiseSource noiseSource;
};

} // namespace nc::engine::studiofx
