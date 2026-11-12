#pragma once

#include <nc/engine/StudioFxDsp.hpp>
#include <nc/engine/StudioFxEq.hpp>
#include <nc/engine/StudioFxSlot.hpp>

#include <memory>
#include <vector>

namespace nc::engine::studiofx
{
/** Tape / tube / transformer / console harmonic colors, oversampled. */
class SaturationModule final : public StudioFxModule
{
public:
    explicit SaturationModule (float sampleRate) noexcept
        : fs (sampleRate)
        , mixSmoother (1.0f, sampleRate, 12.0f)
    {
    }

    const char* kind() const noexcept override { return "SATURATION"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        type = std::min (std::max (static_cast<int> (slotValue (slot, "type")), 0), 3);
        drive = dbToLinear (slotValue (slot, "driveDb"));
        warmth = slotValue (slot, "warmth");
        mixSmoother.set (slotValue (slot, "mix"));
        const int factor = std::min (std::max (static_cast<int> (slotValue (slot, "oversample")), 1), 4);
        if (factor <= 1)
            oversampler.reset();
        else if (oversampler == nullptr || oversampler->factor() != factor)
            oversampler = std::make_unique<Oversampler> (factor, fs);
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        inputMeter.update (inL, inR);
        float l, r;
        if (oversampler == nullptr)
        {
            l = shapeFor (inL * drive, 0) / trimForDrive();
            r = shapeFor (inR * drive, 1) / trimForDrive();
        }
        else
        {
            oversampler->upsample (inL * drive, inR * drive);
            for (int i = 0; i < oversampler->factor(); ++i)
            {
                oversampler->bufferL[static_cast<std::size_t> (i)] = shapeFor (oversampler->bufferL[static_cast<std::size_t> (i)], 0);
                oversampler->bufferR[static_cast<std::size_t> (i)] = shapeFor (oversampler->bufferR[static_cast<std::size_t> (i)], 1);
            }
            float scratch[2];
            oversampler->downsample (scratch);
            const float trim = trimForDrive();
            l = scratch[0] / trim;
            r = scratch[1] / trim;
        }
        const float mix = mixSmoother.next();
        out[0] = inL + (l - inL) * mix;
        out[1] = inR + (r - inR) * mix;
        outputMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        lpL = lpR = 0.0f;
        if (oversampler != nullptr)
            oversampler->reset();
        inputMeter.reset();
        outputMeter.reset();
    }

private:
    float shapeFor (float x, int channel) noexcept
    {
        switch (type)
        {
            case 0:
            {
                const float driven = static_cast<float> (std::tanh (x * (1.0f + warmth * 0.4f)));
                float smoothed;
                if (channel == 0)
                {
                    lpL = lpL + 0.3f * (driven - lpL);
                    smoothed = lpL;
                }
                else
                {
                    lpR = lpR + 0.3f * (driven - lpR);
                    smoothed = lpR;
                }
                return driven * (1.0f - warmth * 0.2f) + smoothed * (warmth * 0.2f);
            }
            case 1:
            {
                const float bias = 1.0f + warmth * 0.5f;
                const float driven = x * (1.0f + warmth * 0.3f);
                return driven >= 0.0f ? static_cast<float> (std::tanh (driven * bias))
                                      : static_cast<float> (std::tanh (driven / bias));
            }
            case 2:
            {
                const float driven = x / (1.0f + std::fabs (x) * (0.8f - warmth * 0.4f));
                float weighted;
                if (channel == 0)
                {
                    lpL = lpL + 0.055f * (driven - lpL);
                    weighted = lpL;
                }
                else
                {
                    lpR = lpR + 0.055f * (driven - lpR);
                    weighted = lpR;
                }
                return driven + weighted * warmth * 0.5f;
            }
            default:
            {
                const float driven = static_cast<float> (std::tanh (x * 1.35f));
                return driven * (1.0f - warmth * 0.1f) + driven * driven * driven * (warmth * 0.1f);
            }
        }
    }

    float trimForDrive() const noexcept { return 1.0f + (drive - 1.0f) * 0.18f; }

    float fs;
    int type = 0;
    float drive = 1.0f, warmth = 0.4f;
    SmoothValue mixSmoother;
    std::unique_ptr<Oversampler> oversampler;
    float lpL = 0.0f, lpR = 0.0f;
    PeakMeter inputMeter, outputMeter;
};

/** Tape machine: record saturation, head bump, HF loss, wow/flutter and noise. */
class TapeMachineModule final : public StudioFxModule
{
public:
    explicit TapeMachineModule (float sampleRate)
        : fs (sampleRate)
        , headBumpL (sampleRate)
        , headBumpR (sampleRate)
        , hfLossL (sampleRate)
        , hfLossR (sampleRate)
        , subsonicL (sampleRate)
        , subsonicR (sampleRate)
        , delaySize (static_cast<int> (sampleRate * 0.02f) + 4)
        , delayL (static_cast<std::size_t> (delaySize), 0.0f)
        , delayR (static_cast<std::size_t> (delaySize), 0.0f)
    {
    }

    const char* kind() const noexcept override { return "TAPE_MACHINE"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        drive = dbToLinear (slotValue (slot, "driveDb"));
        speedIps = slotValue (slot, "speedIps");
        bias = slotValue (slot, "bias");
        wowFlutter = slotValue (slot, "wowFlutter");
        noiseOn = slotValue (slot, "noise") >= 0.5f;
        outGain = dbToLinear (slotValue (slot, "outDb"));
        const int factor = std::fabs (bias) > 0.1f || drive > 2.0f ? 4 : 2;
        if (oversampler == nullptr || oversampler->factor() != factor)
            oversampler = std::make_unique<Oversampler> (factor, fs);
        const float bumpHz = speedIps <= 7.5f ? 40.0f : speedIps <= 15.0f ? 60.0f : 95.0f;
        const float bumpDb = speedIps <= 7.5f ? 3.5f : speedIps <= 15.0f ? 2.5f : 1.5f;
        headBumpL.setLowShelf (bumpHz, bumpDb, 0.8f);
        headBumpR.setLowShelf (bumpHz, bumpDb, 0.8f);
        const float hfBase = speedIps <= 7.5f ? 12000.0f : speedIps <= 15.0f ? 16000.0f : 20000.0f;
        const float hf = hfBase * (1.0f - std::max (bias, 0.0f) * 0.25f);
        hfLossL.setLowPass (std::max (hf, 6000.0f), 0.707f);
        hfLossR.setLowPass (std::max (hf, 6000.0f), 0.707f);
        subsonicL.setHighPass (25.0f, 0.707f);
        subsonicR.setHighPass (25.0f, 0.707f);
        wowRate = 1.2f;
        flutterRate = 7.5f;
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        inputMeter.update (inL, inR);
        float l, r;
        if (oversampler == nullptr)
        {
            l = recordShape (inL * drive);
            r = recordShape (inR * drive);
        }
        else
        {
            oversampler->upsample (inL * drive, inR * drive);
            for (int i = 0; i < oversampler->factor(); ++i)
            {
                oversampler->bufferL[static_cast<std::size_t> (i)] = recordShape (oversampler->bufferL[static_cast<std::size_t> (i)]);
                oversampler->bufferR[static_cast<std::size_t> (i)] = recordShape (oversampler->bufferR[static_cast<std::size_t> (i)]);
            }
            float scratch[2];
            oversampler->downsample (scratch);
            l = scratch[0];
            r = scratch[1];
        }

        wowPhase = wowPhase + (kTau * wowRate) / fs;
        if (wowPhase >= kTau)
            wowPhase = wowPhase - kTau;
        flutterPhase = flutterPhase + (kTau * flutterRate) / fs;
        if (flutterPhase >= kTau)
            flutterPhase = flutterPhase - kTau;
        const float baseDelayFrames = fs * 0.004f;
        const float modFrames = wowFlutter * fs * 0.00006f
                              * (0.7f * static_cast<float> (std::sin (wowPhase)) + 0.3f * static_cast<float> (std::sin (flutterPhase)));
        const float readL = wrap (writeIndex - baseDelayFrames - modFrames);
        const float readR = wrap (writeIndex - (baseDelayFrames + modFrames * 1.02f));
        const int iL = static_cast<int> (readL);
        const float fL = readL - iL;
        const int iR = static_cast<int> (readR);
        const float fR = readR - iR;
        const float outDelayedL = delayL[static_cast<std::size_t> (iL)] * (1.0f - fL)
                                + delayL[static_cast<std::size_t> ((iL + 1) % delaySize)] * fL;
        const float outDelayedR = delayR[static_cast<std::size_t> (iR)] * (1.0f - fR)
                                + delayR[static_cast<std::size_t> ((iR + 1) % delaySize)] * fR;
        delayL[static_cast<std::size_t> (writeIndex)] = l;
        delayR[static_cast<std::size_t> (writeIndex)] = r;
        writeIndex = (writeIndex + 1) % delaySize;
        l = outDelayedL;
        r = outDelayedR;

        l = headBumpL.process (subsonicL.process (l));
        r = headBumpR.process (subsonicR.process (r));
        l = hfLossL.process (l);
        r = hfLossR.process (r);
        if (noiseOn)
        {
            l = l + nextNoise() * 0.0006f;
            r = r + nextNoise() * 0.0006f;
        }
        out[0] = l * outGain;
        out[1] = r * outGain;
        outputMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        writeIndex = 0;
        wowPhase = flutterPhase = 0.0f;
        headBumpL.reset();
        headBumpR.reset();
        hfLossL.reset();
        hfLossR.reset();
        subsonicL.reset();
        subsonicR.reset();
        if (oversampler != nullptr)
            oversampler->reset();
        inputMeter.reset();
        outputMeter.reset();
    }

private:
    static constexpr float kTau = 6.2831855f;

    float recordShape (float x) noexcept
    {
        const float biasDrive = 1.0f + 0.35f * (1.0f - std::max (bias, 0.0f));
        const float driven = x * biasDrive;
        const float shape = static_cast<float> (std::tanh (driven));
        return shape - (0.06f * shape) * (shape * shape);
    }

    float nextNoise() noexcept
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return static_cast<float> (static_cast<double> ((noiseState >> 8) & 0xffffu) / 32768.0 - 1.0);
    }

    float wrap (float value) noexcept
    {
        float wrapped = std::fmod (value, static_cast<float> (delaySize));
        if (wrapped < 0.0f)
            wrapped += delaySize;
        return wrapped;
    }

    float fs;
    float drive = 1.0f, speedIps = 15.0f, bias = 0.0f, wowFlutter = 0.35f;
    bool noiseOn = false;
    float outGain = 1.0f;
    std::unique_ptr<Oversampler> oversampler;
    Biquad headBumpL, headBumpR, hfLossL, hfLossR, subsonicL, subsonicR;
    int delaySize;
    std::vector<float> delayL, delayR;
    int writeIndex = 0;
    float wowPhase = 0.0f, flutterPhase = 0.0f;
    float wowRate = 1.2f, flutterRate = 7.5f;
    uint32_t noiseState = 0x12345678u;
    PeakMeter inputMeter, outputMeter;
};

/** Four-line feedback delay network reverb (plate / hall / room). */
class AlgorithmicReverbModule final : public StudioFxModule
{
public:
    AlgorithmicReverbModule (float sampleRate, const char* kindName, int mode)
        : fs (sampleRate)
        , kindName (kindName)
        , reverbMode (mode)
        , lineLen{ delayMs (29.7f, sampleRate), delayMs (37.1f, sampleRate), delayMs (41.1f, sampleRate), delayMs (43.7f, sampleRate) }
        , lineBuf{ std::vector<float> (static_cast<std::size_t> (lineLen[0] + 1), 0.0f), std::vector<float> (static_cast<std::size_t> (lineLen[1] + 1), 0.0f), std::vector<float> (static_cast<std::size_t> (lineLen[2] + 1), 0.0f), std::vector<float> (static_cast<std::size_t> (lineLen[3] + 1), 0.0f) }
        , apLen{ static_cast<int> (sampleRate * 0.0047f), static_cast<int> (sampleRate * 0.0037f), static_cast<int> (sampleRate * 0.0021f), static_cast<int> (sampleRate * 0.0013f) }
        , preDelaySize (static_cast<int> (sampleRate * 0.15f) + 4)
        , preDelayBuf (static_cast<std::size_t> (preDelaySize), 0.0f)
        , lowCutL (sampleRate)
        , lowCutR (sampleRate)
        , highCutL (sampleRate)
        , highCutR (sampleRate)
        , tiltL (sampleRate)
        , tiltR (sampleRate)
        , mixSmoother (0.25f, sampleRate, 20.0f)
    {
        for (int i = 0; i < 4; ++i)
        {
            dampingFilters[i] = Biquad (sampleRate);
            dampingFilters[i].setBypass();
            apBuf[i] = std::vector<float> (static_cast<std::size_t> (apLen[i] + 1), 0.0f);
        }
        outputGain = 1.0f / static_cast<float> (std::sqrt (2.0));
    }

    const char* kind() const noexcept override { return kindName; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        decaySec = slotValue (slot, "decaySec");
        preDelayFrames = std::min (std::max (static_cast<int> (slotValue (slot, "preDelayMs") / 1000.0f * fs), 0), preDelaySize - 4);
        const float hp = slotValue (slot, "hpHz");
        const float lp = slotValue (slot, "lpHz");
        lowCutL.setHighPass (hp, 0.707f);
        lowCutR.setHighPass (hp, 0.707f);
        highCutL.setLowPass (lp, 0.707f);
        highCutR.setLowPass (lp, 0.707f);
        width = slotValue (slot, "width");
        mixSmoother.set (slotValue (slot, "mix"));
        const int mode = reverbMode;
        const float dampFreq = mode == 0 ? 7500.0f : mode == 2 ? 6000.0f : 5000.0f;
        const float hfDecayRatio = mode == 0 ? 0.75f : mode == 2 ? 0.7f : 0.7f;
        const float avgLen = (lineLen[0] + lineLen[1] + lineLen[2] + lineLen[3]) / 4.0f;
        const float passesPerSecond = fs / avgLen;
        const float dialled = std::max (decaySec, 0.05f);
        const float extraLossPerSecondDb = 60.0f / (dialled * hfDecayRatio) - 60.0f / dialled;
        const float dampDb = -std::min (std::max (extraLossPerSecondDb / passesPerSecond, 0.0f), 4.0f);
        for (auto& filter : dampingFilters)
            filter.setHighShelf (dampFreq, dampDb);
        modulation = mode == 1 ? 1.0f : 0.35f;
        modRate = mode == 1 ? 0.45f : 0.9f;
        const float tilt = mode == 0 ? 1.5f : mode == 2 ? 0.0f : -1.5f;
        tiltL.setHighShelf (6000.0f, tilt);
        tiltR.setHighShelf (6000.0f, tilt);
        recomputeFeedback();
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        inputMeter.update (inL, inR);
        const float mono = (inL + inR) * 0.5f;
        preDelayBuf[static_cast<std::size_t> (preDelayWrite)] = mono;
        const int preRead = (preDelayWrite - preDelayFrames + preDelaySize) % preDelaySize;
        float diffused = preDelayBuf[static_cast<std::size_t> (preRead)];
        const int erOrigin = preDelayWrite - preDelayFrames;
        const float er1 = preDelayBuf[static_cast<std::size_t> (erTap (erOrigin, 0.007f))] * 0.32f;
        const float er2 = preDelayBuf[static_cast<std::size_t> (erTap (erOrigin, 0.013f))] * 0.24f;
        const float er3 = preDelayBuf[static_cast<std::size_t> (erTap (erOrigin, 0.021f))] * 0.16f;
        diffused = diffused + (er1 + er2) + er3;
        preDelayWrite = (preDelayWrite + 1) % preDelaySize;
        for (int i = 0; i < 4; ++i)
        {
            const int n = apLen[i] + 1;
            auto& buf = apBuf[i];
            const int idx = apWrite[i];
            const float stored = buf[static_cast<std::size_t> (idx)];
            const float v = diffused - apGain * stored;
            buf[static_cast<std::size_t> (idx)] = v;
            diffused = stored + apGain * v;
            apWrite[i] = (idx + 1) % n;
        }
        modPhase = modPhase + (kTau * modRate) / fs;
        if (modPhase >= kTau)
            modPhase = modPhase - kTau;
        const float modSamples = modulation * 1.6f * static_cast<float> (std::sin (modPhase));
        for (int i = 0; i < 4; ++i)
        {
            const int len = lineLen[i];
            const int n = len + 1;
            const float direction = i % 2 == 0 ? 1.0f : -1.0f;
            float readPos = lineWrite - len + modSamples * direction;
            readPos = std::fmod (readPos, static_cast<float> (n));
            if (readPos < 0.0f)
                readPos += n;
            const int i0 = static_cast<int> (readPos);
            const float frac = readPos - i0;
            const float sample = lineBuf[i][static_cast<std::size_t> (i0)];
            float delayed;
            if (frac <= 1e-6f)
            {
                interpInput[i] = sample;
                interpOutput[i] = sample;
                delayed = sample;
            }
            else
            {
                const float eta = (1.0f - frac) / (1.0f + frac);
                const float interpolated = eta * (sample - interpOutput[i]) + interpInput[i];
                interpInput[i] = sample;
                interpOutput[i] = interpolated;
                delayed = interpolated;
            }
            outs[i] = dampingFilters[i].process (delayed);
        }
        const float h0 = (outs[0] + outs[1] + (outs[2] + outs[3])) * 0.5f;
        const float h1 = (outs[0] - outs[1] + (outs[2] - outs[3])) * 0.5f;
        const float h2 = (outs[0] + outs[1] - (outs[2] + outs[3])) * 0.5f;
        const float h3 = (outs[0] - outs[1] - (outs[2] - outs[3])) * 0.5f;
        mixed[0] = h0;
        mixed[1] = h1;
        mixed[2] = h2;
        mixed[3] = h3;
        for (int i = 0; i < 4; ++i)
        {
            const int n = lineLen[i] + 1;
            const int idx = static_cast<int> (std::fmod (lineWrite, static_cast<float> (n)));
            lineBuf[i][static_cast<std::size_t> (idx)] = diffused * 0.5f + mixed[i] * lineFeedback[i];
        }
        lineWrite += 1.0f;
        float wetL = outs[0] - outs[2];
        float wetR = outs[1] - outs[3];
        wetL = highCutL.process (lowCutL.process (tiltL.process (wetL))) * outputGain;
        wetR = highCutR.process (lowCutR.process (tiltR.process (wetR))) * outputGain;
        const float mid = (wetL + wetR) * 0.5f;
        const float side = (wetL - wetR) * 0.5f * width;
        wetL = mid + side;
        wetR = mid - side;
        const float mix = mixSmoother.next();
        out[0] = inL + (wetL - inL) * mix;
        out[1] = inR + (wetR - inR) * mix;
        outputMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        for (auto& buf : lineBuf)
            std::fill (buf.begin(), buf.end(), 0.0f);
        for (auto& buf : apBuf)
            std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < 4; ++i)
            apWrite[i] = 0;
        std::fill (preDelayBuf.begin(), preDelayBuf.end(), 0.0f);
        for (auto& filter : dampingFilters)
            filter.reset();
        for (int i = 0; i < 4; ++i)
            interpInput[i] = interpOutput[i] = 0.0f;
        lineWrite = 0.0f;
        preDelayWrite = 0;
        modPhase = 0.0f;
        lowCutL.reset();
        lowCutR.reset();
        highCutL.reset();
        highCutR.reset();
        tiltL.reset();
        tiltR.reset();
        inputMeter.reset();
        outputMeter.reset();
    }

private:
    static constexpr float kTau = 6.2831855f;

    static int delayMs (float ms, float sampleRate) noexcept
    {
        return std::max (static_cast<int> (sampleRate * ms / 1000.0f), 8);
    }

    int erTap (int origin, float seconds) const noexcept
    {
        // Mirrors `(erOrigin - back + 2 * size) % size`: the dividend stays
        // above -2 * size (origin >= -preDelayFrames), so one conditional add
        // reproduces the JS remainder exactly.
        const int back = std::max (1, static_cast<int> (fs * seconds));
        int tap = (origin - back + 2 * preDelaySize) % preDelaySize;
        if (tap < 0)
            tap += preDelaySize;
        return tap;
    }

    void recomputeFeedback() noexcept
    {
        for (int i = 0; i < 4; ++i)
        {
            const float lenSec = lineLen[i] / fs;
            lineFeedback[i] = std::min (std::max (static_cast<float> (std::pow (10.0, (-3.0 * lenSec) / std::max (decaySec, 0.05f))), 0.02f), 0.9995f);
        }
    }

    float fs;
    const char* kindName;
    int reverbMode;
    int lineLen[4];
    std::vector<float> lineBuf[4];
    float lineWrite = 0.0f;
    Biquad dampingFilters[4];
    float lineFeedback[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float outs[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float mixed[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float interpInput[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float interpOutput[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float outputGain = 0.0f;
    int apLen[4];
    std::vector<float> apBuf[4];
    int apWrite[4] = { 0, 0, 0, 0 };
    static constexpr float apGain = 0.62f;
    int preDelaySize;
    std::vector<float> preDelayBuf;
    int preDelayWrite = 0, preDelayFrames = 0;
    Biquad lowCutL, lowCutR, highCutL, highCutR, tiltL, tiltR;
    SmoothValue mixSmoother;
    float width = 0.9f, decaySec = 1.5f;
    float modulation = 0.0f, modRate = 0.6f, modPhase = 0.0f;
    PeakMeter inputMeter, outputMeter;
};

/** Tempo-locked filtered ducking delay. */
class TempoDelayModule final : public StudioFxModule
{
public:
    explicit TempoDelayModule (float sampleRate)
        : fs (sampleRate)
        , maxDelay (static_cast<int> (sampleRate * 2.5f) + 8)
        , bufferL (static_cast<std::size_t> (maxDelay), 0.0f)
        , bufferR (static_cast<std::size_t> (maxDelay), 0.0f)
        , hpL (sampleRate)
        , hpR (sampleRate)
        , lpL (sampleRate)
        , lpR (sampleRate)
        , duckRelease (dynCoeff (180.0f, sampleRate))
        , mixSmoother (0.25f, sampleRate, 18.0f)
    {
    }

    const char* kind() const noexcept override { return "TEMPO_DELAY"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        division = slotValue (slot, "division");
        freeTimeMs = slotValue (slot, "timeMs");
        feedbackAmount = slotValue (slot, "feedback");
        pingPong = slotValue (slot, "pingPong") >= 0.5f;
        ducking = slotValue (slot, "ducking") >= 0.5f;
        mixSmoother.set (slotValue (slot, "mix"));
        const float hpHz = slotValue (slot, "hpHz"), lpHz = slotValue (slot, "lpHz");
        hpL.setHighPass (hpHz, 0.707f);
        hpR.setHighPass (hpHz, 0.707f);
        lpL.setLowPass (lpHz, 0.707f);
        lpR.setLowPass (lpHz, 0.707f);
        updateTimes();
    }

    void setTempoBpm (float bpm) noexcept override
    {
        tempoBpm = std::min (std::max (bpm, 20.0f), 300.0f);
        updateTimes();
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        inputMeter.update (inL, inR);
        const int readL = (writeIndex - delayFramesL + maxDelay) % maxDelay;
        const int readR = (writeIndex - delayFramesR + maxDelay) % maxDelay;
        const float delayedL = bufferL[static_cast<std::size_t> (readL)];
        const float delayedR = bufferR[static_cast<std::size_t> (readR)];
        const float fbL = lpL.process (hpL.process (delayedL));
        const float fbR = lpR.process (hpR.process (delayedR));
        if (pingPong)
        {
            bufferL[static_cast<std::size_t> (writeIndex)] = inL + fbR * feedbackAmount;
            bufferR[static_cast<std::size_t> (writeIndex)] = inR + fbL * feedbackAmount;
        }
        else
        {
            bufferL[static_cast<std::size_t> (writeIndex)] = inL + fbL * feedbackAmount;
            bufferR[static_cast<std::size_t> (writeIndex)] = inR + fbR * feedbackAmount;
        }
        writeIndex = (writeIndex + 1) % maxDelay;
        if (ducking)
        {
            const float dry = std::max (std::fabs (inL), std::fabs (inR));
            const float target = dry > dbToLinear (-30.0f) ? 0.35f : 1.0f;
            duckGain = target < duckGain ? target : target + duckRelease * (duckGain - target);
        }
        else
        {
            duckGain = 1.0f;
        }
        const float mix = mixSmoother.next();
        const float wetL = delayedL * duckGain;
        const float wetR = delayedR * duckGain;
        out[0] = inL * (1.0f - mix) + wetL * mix;
        out[1] = inR * (1.0f - mix) + wetR * mix;
        outputMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        std::fill (bufferL.begin(), bufferL.end(), 0.0f);
        std::fill (bufferR.begin(), bufferR.end(), 0.0f);
        writeIndex = 0;
        duckGain = 1.0f;
        hpL.reset();
        hpR.reset();
        lpL.reset();
        lpR.reset();
        inputMeter.reset();
        outputMeter.reset();
    }

private:
    void updateTimes() noexcept
    {
        const float beatMs = 60000.0f / tempoBpm;
        float ms;
        switch (static_cast<int> (division))
        {
            case 0: ms = beatMs / 4.0f; break;
            case 1: ms = beatMs / 2.0f; break;
            case 2: ms = beatMs * 0.75f; break;
            case 3: ms = beatMs; break;
            case 4: ms = beatMs * 1.5f; break;
            case 5: ms = beatMs * 2.0f; break;
            default: ms = freeTimeMs; break;
        }
        delayFramesL = std::min (std::max (static_cast<int> (ms / 1000.0f * fs), 1), maxDelay - 4);
        delayFramesR = pingPong ? delayFramesL
                                : std::min (std::max (static_cast<int> (ms * 1.02f / 1000.0f * fs), 1), maxDelay - 4);
    }

    float fs;
    int maxDelay;
    std::vector<float> bufferL, bufferR;
    int writeIndex = 0;
    int delayFramesL = 1, delayFramesR = 1;
    float feedbackAmount = 0.25f;
    Biquad hpL, hpR, lpL, lpR;
    bool pingPong = false, ducking = true;
    float duckGain = 1.0f;
    float duckRelease;
    SmoothValue mixSmoother;
    float tempoBpm = 120.0f, division = 1.0f, freeTimeMs = 110.0f;
    PeakMeter inputMeter, outputMeter;
};

/** Juno-style chorus I/II and a Haas/doubler width mode. */
class ChorusWidthModule final : public StudioFxModule
{
public:
    explicit ChorusWidthModule (float sampleRate)
        : fs (sampleRate)
        , maxDelay (static_cast<int> (sampleRate * 0.06f) + 8)
        , bufferL (static_cast<std::size_t> (maxDelay), 0.0f)
        , bufferR (static_cast<std::size_t> (maxDelay), 0.0f)
        , mixSmoother (0.35f, sampleRate, 18.0f)
    {
    }

    const char* kind() const noexcept override { return "CHORUS_WIDTH"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        mode = std::min (std::max (static_cast<int> (slotValue (slot, "mode")), 0), 2);
        rateHz = slotValue (slot, "rateHz");
        depthMs = slotValue (slot, "depthMs");
        width = slotValue (slot, "width");
        mixSmoother.set (slotValue (slot, "mix"));
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        inputMeter.update (inL, inR);
        phase = phase + (kTau * rateHz) / fs;
        if (phase >= kTau)
            phase = phase - kTau;
        const float depthScale = mode == 0 ? 0.45f : mode == 1 ? 1.0f : 1.4f;
        const float baseMs = mode == 0 ? 8.0f : mode == 1 ? 12.0f : 18.0f;
        const float depthFrames = depthMs * depthScale * fs / 1000.0f;
        const float baseFrames = baseMs * fs / 1000.0f;
        const double delayL = baseFrames + (0.5 + 0.5 * std::sin (phase)) * depthFrames;
        const double delayR = baseFrames + (0.5 + 0.5 * std::sin (phase + kPi * 0.8)) * depthFrames;
        const float wetL = read (delayL, bufferL);
        const float wetR = read (delayR, bufferR);
        bufferL[static_cast<std::size_t> (writeIndex)] = inL;
        bufferR[static_cast<std::size_t> (writeIndex)] = inR;
        writeIndex = (writeIndex + 1) % maxDelay;
        const float mix = mixSmoother.next();
        const float blendedL = inL + (wetL - inL) * mix;
        const float blendedR = inR + (wetR - inR) * mix;
        const float mid = (blendedL + blendedR) * 0.5f;
        const float side = (blendedL - blendedR) * 0.5f * (0.6f + width);
        out[0] = mid + side;
        out[1] = mid - side;
        outputMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        std::fill (bufferL.begin(), bufferL.end(), 0.0f);
        std::fill (bufferR.begin(), bufferR.end(), 0.0f);
        writeIndex = 0;
        phase = 0.0f;
        inputMeter.reset();
        outputMeter.reset();
    }

private:
    static constexpr float kTau = 6.2831855f;
    static constexpr double kPi = 3.141592653589793;

    float read (double delayFrames, const std::vector<float>& buffer) noexcept
    {
        double pos = std::fmod (writeIndex - delayFrames, maxDelay);
        if (pos < 0.0)
            pos += maxDelay;
        const int i0 = static_cast<int> (pos);
        const double frac = pos - i0;
        const int i1 = (i0 + 1) % maxDelay;
        return static_cast<float> (buffer[static_cast<std::size_t> (i0)] * (1.0 - frac))
             + static_cast<float> (buffer[static_cast<std::size_t> (i1)] * frac);
    }

    float fs;
    int maxDelay;
    std::vector<float> bufferL, bufferR;
    int writeIndex = 0;
    float phase = 0.0f;
    int mode = 1;
    float rateHz = 0.8f, depthMs = 3.5f, width = 0.6f;
    SmoothValue mixSmoother;
    PeakMeter inputMeter, outputMeter;
};

} // namespace nc::engine::studiofx
