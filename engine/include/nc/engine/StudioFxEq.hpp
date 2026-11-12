#pragma once

#include <nc/engine/StudioFxDsp.hpp>
#include <nc/engine/StudioFxSlot.hpp>

#include <memory>

namespace nc::engine::studiofx
{
/** One insert module. Mirrors the `StudioModule` interface: modules are built
    once per chain shape and reconfigured in place, so filter and compressor
    state survives edits and never clicks. */
class StudioFxModule
{
public:
    virtual ~StudioFxModule() = default;

    virtual const char* kind() const noexcept = 0;
    virtual void configure (const FxSlot& slot) = 0;
    virtual void processSampleInto (float inL, float inR, float* out) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void setKey (float, float) noexcept {}
    virtual void setTempoBpm (float) noexcept {}
    virtual float gainReductionDb() const noexcept { return 0.0f; }

    bool enabled = true;
};

/** Trim / auto gain staging. Feeds analogue-modelled inserts at -18 dBFS. */
class TrimModule final : public StudioFxModule
{
public:
    explicit TrimModule (float sampleRate) noexcept
        : fs (sampleRate)
        , rmsCoeff (static_cast<float> (std::exp (-1.0 / (0.3 * static_cast<double> (sampleRate)))))
        , targetRms (dbToLinear (-18.0f))
    {
    }

    const char* kind() const noexcept override { return "TRIM"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        manualGain = dbToLinear (slotValue (slot, "gainDb"));
        autoStage = slotValue (slot, "autoStage") >= 0.5f;
        if (! autoStage)
            rms = 0.0f;
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        if (autoStage)
        {
            const float mono = (inL + inR) * 0.5f;
            rms = rms + (1.0f - rmsCoeff) * (mono * mono - rms);
            const float level = std::sqrt (std::max (rms, 0.0f));
            const float desired = level > 1e-5f ? std::min (std::max (targetRms / level, 0.25f), 4.0f) : 1.0f;
            gain = gain + 0.001f * (desired - gain);
        }
        else
        {
            gain = 1.0f;
        }
        const float total = manualGain * gain;
        out[0] = inL * total;
        out[1] = inR * total;
    }

    void reset() noexcept override
    {
        rms = 0.0f;
        gain = 1.0f;
    }

private:
    float fs;
    float gain = 1.0f, manualGain = 1.0f;
    bool autoStage = false;
    float rms = 0.0f;
    float rmsCoeff, targetRms;
};

/** Utility 12 dB/oct high and low pass. A disabled band is bypassed. */
class FilterModule final : public StudioFxModule
{
public:
    explicit FilterModule (float sampleRate)
        : hpL (sampleRate, 1)
        , hpR (sampleRate, 1)
        , lpL (sampleRate, 1)
        , lpR (sampleRate, 1)
    {
    }

    const char* kind() const noexcept override { return "FILTER"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        const float hp = slotValue (slot, "hpHz");
        const float lp = slotValue (slot, "lpHz");
        if (hp > 15.0f)
        {
            hpL.setSecondOrderHighPass (hp);
            hpR.setSecondOrderHighPass (hp);
        }
        else
        {
            hpL.bypass();
            hpR.bypass();
        }
        if (lp > 0.0f)
        {
            lpL.setSecondOrderLowPass (lp);
            lpR.setSecondOrderLowPass (lp);
        }
        else
        {
            lpL.bypass();
            lpR.bypass();
        }
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        out[0] = lpL.process (hpL.process (inL));
        out[1] = lpR.process (hpR.process (inR));
    }

    void reset() noexcept override
    {
        hpL.reset();
        hpR.reset();
        lpL.reset();
        lpR.reset();
    }

private:
    BiquadCascade hpL, hpR, lpL, lpR;
};

/** Neve 1073 preamp + fixed-frequency EQ with a Class-A style drive stage. */
class Neve1073Module final : public StudioFxModule
{
public:
    explicit Neve1073Module (float sampleRate)
        : fs (sampleRate)
        , hpL (sampleRate, 2)
        , hpR (sampleRate, 2)
        , lfL (sampleRate)
        , lfR (sampleRate)
        , midL (sampleRate)
        , midR (sampleRate)
        , hfL (sampleRate)
        , hfR (sampleRate)
        , outSmoother (1.0f, sampleRate, 20.0f)
        , driveSmoother (0.0f, sampleRate, 25.0f)
        , transformerL (0.0f, sampleRate, 1.1f)
        , transformerR (0.0f, sampleRate, 1.1f)
    {
    }

    const char* kind() const noexcept override { return "NEVE_1073"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        const float hpf = std::max (slotValue (slot, "hpfHz"), 20.0f);
        hpL.setThirdOrderHighPass (hpf);
        hpR.setThirdOrderHighPass (hpf);
        const float lfHz = slotValue (slot, "lfHz"), lfDb = slotValue (slot, "lfDb");
        lfL.setLowShelf (lfHz, lfDb);
        lfR.setLowShelf (lfHz, lfDb);
        const float midHz = slotValue (slot, "midHz");
        const float q = std::min (std::max (0.6f + (midHz / 7200.0f) * 1.1f, 0.6f), 1.8f);
        midL.setPeaking (midHz, slotValue (slot, "midDb"), q);
        midR.setPeaking (midHz, slotValue (slot, "midDb"), q);
        hfL.setHighShelf (12000.0f, slotValue (slot, "hfDb"));
        hfR.setHighShelf (12000.0f, slotValue (slot, "hfDb"));
        outSmoother.set (dbToLinear (slotValue (slot, "outDb")));
        driveSmoother.set (slotValue (slot, "driveDb"));
        if (oversampler == nullptr || oversampler->factor() != 2)
            oversampler = std::make_unique<Oversampler> (2, fs);
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        float l, r;
        if (oversampler != nullptr)
        {
            oversampler->upsample (inL, inR);
            for (int i = 0; i < oversampler->factor(); ++i)
            {
                oversampler->bufferL[static_cast<std::size_t> (i)] = shape (oversampler->bufferL[static_cast<std::size_t> (i)]);
                oversampler->bufferR[static_cast<std::size_t> (i)] = shape (oversampler->bufferR[static_cast<std::size_t> (i)]);
            }
            float scratch[2];
            oversampler->downsample (scratch);
            l = scratch[0];
            r = scratch[1];
        }
        else
        {
            l = shape (inL);
            r = shape (inR);
        }
        l = lfL.process (midL.process (hfL.process (l)));
        r = lfR.process (midR.process (hfR.process (r)));
        l = hpL.process (l);
        r = hpR.process (r);
        transformerL.set (l);
        transformerR.set (r);
        const float bloomL = transformerL.next();
        const float bloomR = transformerR.next();
        l = l + bloomL * 0.05f;
        r = r + bloomR * 0.05f;
        const float gain = outSmoother.next();
        out[0] = l * gain;
        out[1] = r * gain;
    }

    void reset() noexcept override
    {
        if (oversampler != nullptr)
            oversampler->reset();
        hpL.reset();
        hpR.reset();
        lfL.reset();
        lfR.reset();
        midL.reset();
        midR.reset();
        hfL.reset();
        hfR.reset();
    }

private:
    float shape (float x) noexcept
    {
        const float driveDb = driveSmoother.next();
        if (driveDb <= 0.01f)
            return x;
        const float drive = dbToLinear (driveDb);
        const float driven = x * drive;
        const float shaped = driven >= 0.0f ? driven / (1.0f + driven * 0.45f)
                                            : driven / (1.0f - driven * 0.32f);
        return shaped / (1.0f + (drive - 1.0f) * 0.06f);
    }

    float fs;
    BiquadCascade hpL, hpR;
    Biquad lfL, lfR, midL, midR, hfL, hfR;
    SmoothValue outSmoother, driveSmoother, transformerL, transformerR;
    std::unique_ptr<Oversampler> oversampler;
};

/** SSL 4000 E channel EQ: switchable shelves/bells and variable-Q mids. */
class SslChannelEqModule final : public StudioFxModule
{
public:
    explicit SslChannelEqModule (float sampleRate)
        : hpL (sampleRate, 2)
        , hpR (sampleRate, 2)
        , lfL (sampleRate)
        , lfR (sampleRate)
        , lmfL (sampleRate)
        , lmfR (sampleRate)
        , hmfL (sampleRate)
        , hmfR (sampleRate)
        , hfL (sampleRate)
        , hfR (sampleRate)
        , outSmoother (1.0f, sampleRate, 20.0f)
        , driveSmoother (0.0f, sampleRate, 30.0f)
    {
    }

    const char* kind() const noexcept override { return "SSL_CHANNEL_EQ"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        const float hp = slotValue (slot, "hpHz");
        if (hp >= 15.0f)
        {
            hpL.setThirdOrderHighPass (hp);
            hpR.setThirdOrderHighPass (hp);
        }
        else
        {
            hpL.bypass();
            hpR.bypass();
        }
        const float lfHz = slotValue (slot, "lfHz"), lfDb = slotValue (slot, "lfDb");
        if (slotValue (slot, "lfBell") >= 0.5f)
        {
            lfL.setPeaking (lfHz, lfDb, 0.9f);
            lfR.setPeaking (lfHz, lfDb, 0.9f);
        }
        else
        {
            lfL.setLowShelf (lfHz, lfDb, 0.85f);
            lfR.setLowShelf (lfHz, lfDb, 0.85f);
        }
        lmfL.setPeaking (slotValue (slot, "lmfHz"), slotValue (slot, "lmfDb"), slotValue (slot, "lmfQ"));
        lmfR.setPeaking (slotValue (slot, "lmfHz"), slotValue (slot, "lmfDb"), slotValue (slot, "lmfQ"));
        hmfL.setPeaking (slotValue (slot, "hmfHz"), slotValue (slot, "hmfDb"), slotValue (slot, "hmfQ"));
        hmfR.setPeaking (slotValue (slot, "hmfHz"), slotValue (slot, "hmfDb"), slotValue (slot, "hmfQ"));
        const float hfHz = slotValue (slot, "hfHz"), hfDb = slotValue (slot, "hfDb");
        if (slotValue (slot, "hfBell") >= 0.5f)
        {
            hfL.setPeaking (hfHz, hfDb, 0.9f);
            hfR.setPeaking (hfHz, hfDb, 0.9f);
        }
        else
        {
            hfL.setHighShelf (hfHz, hfDb, 0.85f);
            hfR.setHighShelf (hfHz, hfDb, 0.85f);
        }
        outSmoother.set (dbToLinear (slotValue (slot, "outDb")));
        driveSmoother.set (std::max (slotValue (slot, "hmfDb"), 0.0f));
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        float l = hfL.process (hmfL.process (lmfL.process (lfL.process (hpL.process (inL)))));
        float r = hfR.process (hmfR.process (lmfR.process (lfR.process (hpR.process (inR)))));
        const float drive = driveSmoother.next();
        if (drive > 1.0f)
        {
            const float amt = std::min (std::max ((drive - 1.0f) / 18.0f, 0.0f), 1.0f) * 0.35f;
            l = l * (1.0f + amt * (l * l - 0.33f));
            r = r * (1.0f + amt * (r * r - 0.33f));
        }
        const float gain = outSmoother.next();
        out[0] = l * gain;
        out[1] = r * gain;
    }

    void reset() noexcept override
    {
        hpL.reset();
        hpR.reset();
        lfL.reset();
        lfR.reset();
        lmfL.reset();
        lmfR.reset();
        hmfL.reset();
        hmfR.reset();
        hfL.reset();
        hfR.reset();
    }

private:
    BiquadCascade hpL, hpR;
    Biquad lfL, lfR, lmfL, lmfR, hmfL, hmfR, hfL, hfR;
    SmoothValue outSmoother, driveSmoother;
};

/** Pultec EQP-1A: low boost+cut trick, broad high boost, passive make-up. */
class PultecEqp1aModule final : public StudioFxModule
{
public:
    explicit PultecEqp1aModule (float sampleRate)
        : fs (sampleRate)
        , lowBoostL (sampleRate)
        , lowBoostR (sampleRate)
        , lowAttenL (sampleRate)
        , lowAttenR (sampleRate)
        , highBoostL (sampleRate)
        , highBoostR (sampleRate)
        , highAttenL (sampleRate)
        , highAttenR (sampleRate)
        , outSmoother (1.0f, sampleRate, 20.0f)
    {
    }

    const char* kind() const noexcept override { return "PULTEC_EQP1A"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        const float lowBoost = slotValue (slot, "lowBoostDb");
        const float lowAtten = slotValue (slot, "lowAttenDb");
        if (slotValue (slot, "lowBoostHz") > 0.0f)
        {
            lowBoostL.setLowShelf (slotValue (slot, "lowBoostHz"), lowBoost);
            lowBoostR.setLowShelf (slotValue (slot, "lowBoostHz"), lowBoost);
        }
        else
        {
            lowBoostL.setBypass();
            lowBoostR.setBypass();
        }
        if (lowAtten > 0.01f)
        {
            const float attenFreq = std::min (slotValue (slot, "lowAttenHz") * 1.5f, fs * 0.35f);
            lowAttenL.setPeaking (attenFreq, -lowAtten, 0.7f);
            lowAttenR.setPeaking (attenFreq, -lowAtten, 0.7f);
        }
        else
        {
            lowAttenL.setBypass();
            lowAttenR.setBypass();
        }
        if (slotValue (slot, "highBoostDb") > 0.01f)
        {
            highBoostL.setPeaking (slotValue (slot, "highBoostHz"), slotValue (slot, "highBoostDb"), 0.8f);
            highBoostR.setPeaking (slotValue (slot, "highBoostHz"), slotValue (slot, "highBoostDb"), 0.8f);
        }
        else
        {
            highBoostL.setBypass();
            highBoostR.setBypass();
        }
        if (slotValue (slot, "highAttenDb") > 0.01f)
        {
            highAttenL.setHighShelf (slotValue (slot, "highAttenHz"), -slotValue (slot, "highAttenDb"), 0.7f);
            highAttenR.setHighShelf (slotValue (slot, "highAttenHz"), -slotValue (slot, "highAttenDb"), 0.7f);
        }
        else
        {
            highAttenL.setBypass();
            highAttenR.setBypass();
        }
        outSmoother.set (dbToLinear (slotValue (slot, "outDb") + 20.0f));
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        float l = lowAttenL.process (lowBoostL.process (inL));
        float r = lowAttenR.process (lowBoostR.process (inR));
        l = highAttenL.process (highBoostL.process (l));
        r = highAttenR.process (highBoostR.process (r));
        l = l / (1.0f + std::fabs (l) * 0.06f);
        r = r / (1.0f + std::fabs (r) * 0.06f);
        const float gain = outSmoother.next();
        out[0] = l * gain;
        out[1] = r * gain;
    }

    void reset() noexcept override
    {
        lowBoostL.reset();
        lowBoostR.reset();
        lowAttenL.reset();
        lowAttenR.reset();
        highBoostL.reset();
        highBoostR.reset();
        highAttenL.reset();
        highAttenR.reset();
    }

private:
    float fs = 0.0f;
    Biquad lowBoostL, lowBoostR, lowAttenL, lowAttenR;
    Biquad highBoostL, highBoostR, highAttenL, highAttenR;
    SmoothValue outSmoother;
};

class AirShelfModule final : public StudioFxModule
{
public:
    explicit AirShelfModule (float sampleRate)
        : l (sampleRate)
        , r (sampleRate)
    {
    }

    const char* kind() const noexcept override { return "AIR_SHELF"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        const float freq = slotValue (slot, "freqHz");
        const float gain = slotValue (slot, "gainDb");
        if (gain == 0.0f)
        {
            l.setBypass();
            r.setBypass();
        }
        else
        {
            l.setHighShelf (freq, gain);
            r.setHighShelf (freq, gain);
        }
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        out[0] = l.process (inL);
        out[1] = r.process (inR);
    }

    void reset() noexcept override
    {
        l.reset();
        r.reset();
    }

private:
    Biquad l, r;
};

/** Mid-side EQ and width: mono lows, side air, and a width trim. */
class MidSideEqModule final : public StudioFxModule
{
public:
    explicit MidSideEqModule (float sampleRate)
        : sideHpL (sampleRate)
        , sideHpR (sampleRate)
        , sideAirL (sampleRate)
        , sideAirR (sampleRate)
    {
    }

    const char* kind() const noexcept override { return "MID_SIDE_EQ"; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        const float hp = slotValue (slot, "sideHpHz");
        if (hp > 15.0f)
        {
            sideHpL.setHighPass (hp, 0.707f);
            sideHpR.setHighPass (hp, 0.707f);
        }
        else
        {
            sideHpL.setBypass();
            sideHpR.setBypass();
        }
        const float air = slotValue (slot, "sideAirDb");
        if (air != 0.0f)
        {
            sideAirL.setHighShelf (9000.0f, air);
            sideAirR.setHighShelf (9000.0f, air);
        }
        else
        {
            sideAirL.setBypass();
            sideAirR.setBypass();
        }
        width = std::min (std::max (slotValue (slot, "width"), 0.25f), 2.0f);
    }

    void processSampleInto (float inL, float inR, float* out) noexcept override
    {
        if (! enabled)
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        const float mid = (inL + inR) * 0.5f;
        float side = (inL - inR) * 0.5f;
        side = sideAirL.process (sideHpL.process (side));
        side = side * width;
        out[0] = mid + side;
        out[1] = mid - side;
    }

    void reset() noexcept override
    {
        sideHpL.reset();
        sideHpR.reset();
        sideAirL.reset();
        sideAirR.reset();
    }

private:
    Biquad sideHpL, sideHpR, sideAirL, sideAirR;
    float width = 1.0f;
};

} // namespace nc::engine::studiofx
