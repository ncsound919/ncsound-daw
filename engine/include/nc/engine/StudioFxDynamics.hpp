#pragma once

#include <nc/engine/StudioFxDsp.hpp>
#include <nc/engine/StudioFxEq.hpp>
#include <nc/engine/StudioFxSlot.hpp>

#include <memory>
#include <vector>

namespace nc::engine::studiofx
{
/** UREI 1176 FET compressor with program-dependent release. */
class Fet1176Module final : public StudioFxModule
{
public:
    explicit Fet1176Module (float sampleRate) noexcept
        : fs (sampleRate)
        , attackCoeff (dynCoeff (0.2f, sampleRate))
        , releaseFastCoeff (dynCoeff (120.0f, sampleRate))
        , releaseSlowCoeff (dynCoeff (500.0f, sampleRate))
        , detector (sampleRate)
        , sustainCoeff (dynCoeff (400.0f, sampleRate))
        , mixSmoother (1.0f, sampleRate, 12.0f)
    {
    }

    const char* kind() const noexcept override { return "FET_1176"; }
    float gainReductionDb() const noexcept override { return grDb; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        drive = dbToLinear (slotValue (slot, "inputDb"));
        output = dbToLinear (slotValue (slot, "outputDb"));
        ratio = slotValue (slot, "ratio");
        allButtons = slotValue (slot, "allButtons") >= 0.5f;
        attackCoeff = dynCoeff (slotValue (slot, "attackUs") / 1000.0f, fs);
        const float release = slotValue (slot, "releaseMs");
        const float releaseScale = allButtons ? 1.8f : 1.0f;
        releaseFastCoeff = dynCoeff (release * releaseScale, fs);
        releaseSlowCoeff = dynCoeff (release * releaseScale * 3.5f, fs);
        mixSmoother.set (slotValue (slot, "mix"));
        const int factor = allButtons ? 4 : 2;
        if (oversampler == nullptr || oversampler->factor() != factor)
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
        const float dl = inL * drive;
        const float dr = inR * drive;
        const float peak = detector.update (std::max (std::fabs (dl), std::fabs (dr)));
        const float level = levelToDb (peak);
        const float effectiveRatio = allButtons ? std::max (ratio, 12.0f) : ratio;
        const float target = computeGainReductionDb (level, thresholdDb, effectiveRatio, 0.0f);
        sustain = sustain + smoothingStep (sustainCoeff) * (target - sustain);
        const float blend = std::min (std::max (sustain / 12.0f, 0.0f), 1.0f);
        const float coeff = target > grDb ? attackCoeff
                                          : releaseFastCoeff + (releaseSlowCoeff - releaseFastCoeff) * blend;
        grDb = target + coeff * (grDb - target);
        const float gain = dbToLinear (-grDb);
        float l = dl * gain;
        float r = dr * gain;
        if (oversampler != nullptr)
        {
            const float edge = std::min (std::max (grDb / 18.0f, 0.0f), 1.0f) * (allButtons ? 1.0f : 0.45f);
            scratch[0] = l;
            scratch[1] = r;
            oversampler->upsample (scratch[0], scratch[1]);
            for (int i = 0; i < oversampler->factor(); ++i)
            {
                oversampler->bufferL[static_cast<std::size_t> (i)] = fetClamp (oversampler->bufferL[static_cast<std::size_t> (i)], edge);
                oversampler->bufferR[static_cast<std::size_t> (i)] = fetClamp (oversampler->bufferR[static_cast<std::size_t> (i)], edge);
            }
            oversampler->downsample (scratch);
            l = scratch[0];
            r = scratch[1];
        }
        l = l * (output / drive);
        r = r * (output / drive);
        const float mix = mixSmoother.next();
        out[0] = inL + (l - inL) * mix;
        out[1] = inR + (r - inR) * mix;
        outputMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        grDb = 0.0f;
        sustain = 0.0f;
        detector.reset();
        if (oversampler != nullptr)
            oversampler->reset();
        inputMeter.reset();
        outputMeter.reset();
    }

private:
    static float fetClamp (float x, float amount) noexcept
    {
        if (amount <= 0.001f)
            return x;
        const float driven = x * (1.0f + amount * 3.0f);
        return static_cast<float> (std::tanh (driven)) * (1.0f - amount * 0.15f) + x * (amount * 0.15f);
    }

    float fs;
    float drive = 1.0f, output = 1.0f, ratio = 4.0f;
    bool allButtons = false;
    float thresholdDb = -18.0f;
    float attackCoeff, releaseFastCoeff, releaseSlowCoeff;
    float grDb = 0.0f, sustain = 0.0f;
    PeakEnvelope detector;
    float sustainCoeff;
    SmoothValue mixSmoother;
    std::unique_ptr<Oversampler> oversampler;
    float scratch[2] = { 0.0f, 0.0f };
    PeakMeter inputMeter, outputMeter;
};

/** Teletronix LA-2A optical leveler: ~3:1, soft knee, two-stage release. */
class OptoLa2aModule final : public StudioFxModule
{
public:
    explicit OptoLa2aModule (float sampleRate) noexcept
        : fs (sampleRate)
        , attackCoeff (dynCoeff (10.0f, sampleRate))
        , fastRelease (dynCoeff (60.0f, sampleRate))
        , slowRelease (dynCoeff (1500.0f, sampleRate))
        , detector (sampleRate)
        , slowDecayCoeff (dynCoeff (4000.0f, sampleRate))
        , mixSmoother (1.0f, sampleRate, 12.0f)
    {
    }

    const char* kind() const noexcept override { return "OPTO_LA2A"; }
    float gainReductionDb() const noexcept override { return grDb; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        drive = dbToLinear (-6.0f + slotValue (slot, "peakReduction") * 1.6f);
        makeup = dbToLinear (slotValue (slot, "gainDb"));
        limitMode = slotValue (slot, "limitMode") >= 0.5f;
        thresholdDb = limitMode ? -12.0f : -20.0f;
        slowRelease = dynCoeff (1500.0f + peakGr * 400.0f, fs);
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
        const float dl = inL * drive;
        const float dr = inR * drive;
        const float peak = detector.update (std::max (std::fabs (dl), std::fabs (dr)));
        const float level = levelToDb (peak);
        const float ratio = limitMode ? 20.0f : 3.0f;
        const float knee = limitMode ? 4.0f : 8.0f;
        const float target = computeGainReductionDb (level, thresholdDb, ratio, knee);
        const float coeff = target > grDb ? attackCoeff : grDb > peakGr * 0.5f ? fastRelease : slowRelease;
        grDb = target + coeff * (grDb - target);
        peakGr = std::max (peakGr, grDb);
        peakGr = peakGr * slowDecayCoeff;
        const float gain = dbToLinear (-grDb) * makeup;
        float l = dl * gain;
        float r = dr * gain;
        l = l - (0.03f * l) * l;
        r = r - (0.03f * r) * r;
        const float mix = mixSmoother.next();
        out[0] = inL + (l - inL) * mix;
        out[1] = inR + (r - inR) * mix;
        outputMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        grDb = 0.0f;
        peakGr = 0.0f;
        detector.reset();
        inputMeter.reset();
        outputMeter.reset();
    }

private:
    float fs;
    float drive = 1.0f, makeup = 1.0f;
    bool limitMode = false;
    float thresholdDb = -18.0f;
    float attackCoeff, fastRelease, slowRelease;
    float grDb = 0.0f, peakGr = 0.0f;
    PeakEnvelope detector;
    float slowDecayCoeff;
    SmoothValue mixSmoother;
    PeakMeter inputMeter, outputMeter;
};

/** SSL G bus compressor with feedback detection and multi-stage Auto release. */
class VcaBusCompModule final : public StudioFxModule
{
public:
    explicit VcaBusCompModule (float sampleRate) noexcept
        : fs (sampleRate)
        , attackCoeff (dynCoeff (10.0f, sampleRate))
        , release100 (dynCoeff (100.0f, sampleRate))
        , release600 (dynCoeff (600.0f, sampleRate))
        , release1200 (dynCoeff (1200.0f, sampleRate))
        , fixedReleaseCoeff (dynCoeff (300.0f, sampleRate))
        , detector (sampleRate, 20.0f)
        , mixSmoother (1.0f, sampleRate, 12.0f)
    {
    }

    const char* kind() const noexcept override { return "VCA_BUS_COMP"; }
    float gainReductionDb() const noexcept override { return grDb; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        thresholdDb = slotValue (slot, "thresholdDb");
        ratio = slotValue (slot, "ratio");
        attackCoeff = dynCoeff (slotValue (slot, "attackMs"), fs);
        autoRelease = slotValue (slot, "autoRelease") >= 0.5f;
        fixedReleaseCoeff = dynCoeff (slotValue (slot, "releaseMs"), fs);
        makeup = dbToLinear (slotValue (slot, "makeupDb"));
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
        const float peak = detector.update (std::max (std::fabs (lastOutL), std::fabs (lastOutR)));
        const float level = levelToDb (peak);
        const float target = computeGainReductionDb (level, thresholdDb, ratio, 2.0f);
        const float releaseCoeff = autoRelease ? autoReleaseCoeff (grDb) : fixedReleaseCoeff;
        const float coeff = target > grDb ? attackCoeff : releaseCoeff;
        grDb = target + coeff * (grDb - target);
        const float gain = dbToLinear (-grDb) * makeup;
        const float wetL = inL * gain;
        const float wetR = inR * gain;
        lastOutL = wetL;
        lastOutR = wetR;
        const float mix = mixSmoother.next();
        out[0] = inL + (wetL - inL) * mix;
        out[1] = inR + (wetR - inR) * mix;
        outputMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        grDb = 0.0f;
        stage = 0;
        stageTimer = 0;
        lastOutL = lastOutR = 0.0f;
        detector.reset();
        inputMeter.reset();
        outputMeter.reset();
    }

private:
    float autoReleaseCoeff (float currentGrDb) noexcept
    {
        if (currentGrDb > 1.5f)
        {
            stageTimer = 0;
            stage = currentGrDb > 6.0f ? 2 : currentGrDb > 3.0f ? 1 : 0;
        }
        else
        {
            stageTimer += 1;
            if (stageTimer > (fs * 3.0f) / 2.0f && stage > 0)
                stage -= 1;
        }
        return stage == 0 ? release100 : stage == 1 ? release600 : release1200;
    }

    float fs;
    float thresholdDb = -10.0f, ratio = 2.0f;
    float attackCoeff, release100, release600, release1200, fixedReleaseCoeff;
    bool autoRelease = true;
    float makeup = 1.0f, grDb = 0.0f;
    int stage = 0;
    float stageTimer = 0.0f;
    float lastOutL = 0.0f, lastOutR = 0.0f;
    PeakEnvelope detector;
    SmoothValue mixSmoother;
    PeakMeter inputMeter, outputMeter;
};

/** Vari-mu tube compressor: ratio stiffens with gain reduction. */
class VariMuCompModule final : public StudioFxModule
{
public:
    explicit VariMuCompModule (float sampleRate) noexcept
        : fs (sampleRate)
        , attackCoeff (dynCoeff (0.4f, sampleRate))
        , releaseCoeff (dynCoeff (2000.0f, sampleRate))
        , programLongCoeff (dynCoeff (10000.0f, sampleRate))
        , detector (sampleRate)
        , historyCoeff (dynCoeff (2500.0f, sampleRate))
        , mixSmoother (1.0f, sampleRate, 12.0f)
    {
    }

    const char* kind() const noexcept override { return "VARI_MU_COMP"; }
    float gainReductionDb() const noexcept override { return grDb; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        drive = dbToLinear (slotValue (slot, "driveDb"));
        makeup = dbToLinear (slotValue (slot, "makeupDb"));
        dcThresholdDb = slotValue (slot, "thresholdDb");
        ratio = slotValue (slot, "ratio");
        const int tc = std::min (std::max (static_cast<int> (slotValue (slot, "timeConstant")), 0), 5);
        float attackMs = 0.2f, releaseMs = 300.0f;
        switch (tc)
        {
            case 0: attackMs = 0.2f; releaseMs = 300.0f; programDependent = false; break;
            case 1: attackMs = 0.2f; releaseMs = 800.0f; programDependent = false; break;
            case 2: attackMs = 0.4f; releaseMs = 2000.0f; programDependent = false; break;
            case 3: attackMs = 0.8f; releaseMs = 5000.0f; programDependent = false; break;
            case 4: attackMs = 0.2f; releaseMs = 2000.0f; programDependent = true; break;
            default: attackMs = 0.4f; releaseMs = 300.0f; programDependent = true; break;
        }
        attackCoeff = dynCoeff (attackMs, fs);
        releaseCoeff = dynCoeff (releaseMs, fs);
        programLongCoeff = dynCoeff (tc == 5 ? 25000.0f : 10000.0f, fs);
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
        const float dl = inL * drive;
        const float dr = inR * drive;
        const float peak = detector.update (std::max (std::fabs (dl), std::fabs (dr)));
        const float level = levelToDb (peak);
        const float effectiveRatio = ratio * (1.0f + grDb / 12.0f);
        const float knee = std::min (std::max (dcThresholdDb * -0.5f + 6.0f, 2.0f), 14.0f);
        const float target = computeGainReductionDb (level, dcThresholdDb, effectiveRatio, knee);
        const float release = programDependent && grHistory > 4.0f ? programLongCoeff : releaseCoeff;
        const float coeff = target > grDb ? attackCoeff : release;
        grDb = target + coeff * (grDb - target);
        grHistory = grHistory + smoothingStep (historyCoeff) * (grDb - grHistory);
        const float gain = dbToLinear (-grDb) * makeup;
        float l = dl * gain;
        float r = dr * gain;
        l = (l - (0.08f * l) * l) + (0.02f * l) * (l * l);
        r = (r - (0.08f * r) * r) + (0.02f * r) * (r * r);
        const float mix = mixSmoother.next();
        out[0] = inL + (l - inL) * mix;
        out[1] = inR + (r - inR) * mix;
        outputMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        grDb = 0.0f;
        grHistory = 0.0f;
        detector.reset();
        inputMeter.reset();
        outputMeter.reset();
    }

private:
    float fs;
    float drive = 1.0f, makeup = 1.0f;
    float dcThresholdDb = -12.0f, ratio = 2.5f;
    float attackCoeff, releaseCoeff, programLongCoeff;
    bool programDependent = false;
    float grDb = 0.0f, grHistory = 0.0f;
    PeakEnvelope detector;
    float historyCoeff;
    SmoothValue mixSmoother;
    PeakMeter inputMeter, outputMeter;
};

/** Parallel upward-style blend compressor. */
class ParallelCompModule final : public StudioFxModule
{
public:
    explicit ParallelCompModule (float sampleRate) noexcept
        : fs (sampleRate)
        , attackCoeff (dynCoeff (5.0f, sampleRate))
        , releaseCoeff (dynCoeff (120.0f, sampleRate))
        , blendSmoother (0.35f, sampleRate, 15.0f)
        , detector (sampleRate)
    {
    }

    const char* kind() const noexcept override { return "PARALLEL_COMP"; }
    float gainReductionDb() const noexcept override { return grDb; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        thresholdDb = slotValue (slot, "thresholdDb");
        ratio = slotValue (slot, "ratio");
        attackCoeff = dynCoeff (slotValue (slot, "attackMs"), fs);
        releaseCoeff = dynCoeff (slotValue (slot, "releaseMs"), fs);
        blendSmoother.set (slotValue (slot, "blend"));
        makeup = dbToLinear (slotValue (slot, "makeupDb"));
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
        const float peak = detector.update (std::max (std::fabs (inL), std::fabs (inR)));
        const float level = levelToDb (peak);
        const float target = computeGainReductionDb (level, thresholdDb, ratio, 0.0f);
        const float coeff = target > grDb ? attackCoeff : releaseCoeff;
        grDb = target + coeff * (grDb - target);
        const float gain = dbToLinear (-grDb) * makeup;
        const float blend = blendSmoother.next();
        out[0] = inL + ((inL * gain - inL) * blend);
        out[1] = inR + ((inR * gain - inR) * blend);
        outputMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        grDb = 0.0f;
        detector.reset();
        inputMeter.reset();
        outputMeter.reset();
    }

private:
    float fs;
    float thresholdDb = -22.0f, ratio = 8.0f;
    float attackCoeff, releaseCoeff;
    SmoothValue blendSmoother;
    float makeup = 1.0f, grDb = 0.0f;
    PeakEnvelope detector;
    PeakMeter inputMeter, outputMeter;
};

/** Split-band de-esser: only the sibilance band is attenuated. */
class DeEsserModule final : public StudioFxModule
{
public:
    explicit DeEsserModule (float sampleRate) noexcept
        : bandL (sampleRate)
        , bandR (sampleRate)
        , bandLowL (sampleRate)
        , bandLowR (sampleRate)
        , detector (sampleRate, 8.0f)
        , attackCoeff (dynCoeff (3.0f, sampleRate))
        , releaseCoeff (dynCoeff (45.0f, sampleRate))
    {
    }

    const char* kind() const noexcept override { return "DE_ESSER"; }
    float gainReductionDb() const noexcept override { return grDb; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        thresholdDb = slotValue (slot, "thresholdDb");
        maxReductionDb = slotValue (slot, "amountDb");
        splitBand = slotValue (slot, "split") >= 0.5f;
        const float freq = slotValue (slot, "freqHz");
        bandL.setPeaking (freq, 12.0f, 0.9f);
        bandR.setPeaking (freq, 12.0f, 0.9f);
        bandLowL.setLowPass (freq * 0.55f, 0.707f);
        bandLowR.setLowPass (freq * 0.55f, 0.707f);
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
        const float band = detector.update (bandL.process (mono));
        const float level = levelToDb (band);
        const float over = std::max (level - thresholdDb, 0.0f);
        const float target = std::min (over, maxReductionDb * 2.0f);
        const float coeff = target > grDb ? attackCoeff : releaseCoeff;
        grDb = target + coeff * (grDb - target);
        const float reduction = std::min (grDb, maxReductionDb);
        const float bandGain = dbToLinear (-reduction);
        if (splitBand)
        {
            const float lowMonoL = bandLowL.process (inL);
            const float lowMonoR = bandLowR.process (inR);
            const float sibL = inL - lowMonoL;
            const float sibR = inR - lowMonoR;
            out[0] = lowMonoL + sibL * bandGain;
            out[1] = lowMonoR + sibR * bandGain;
        }
        else
        {
            const float gain = dbToLinear (-reduction * 0.6f);
            out[0] = inL * gain;
            out[1] = inR * gain;
        }
    }

    void reset() noexcept override
    {
        bandL.reset();
        bandR.reset();
        bandLowL.reset();
        bandLowR.reset();
        grDb = 0.0f;
        detector.reset();
        inputMeter.reset();
    }

private:
    Biquad bandL, bandR, bandLowL, bandLowR;
    float thresholdDb = -22.0f, maxReductionDb = 3.0f;
    bool splitBand = true;
    float grDb = 0.0f;
    PeakEnvelope detector;
    float attackCoeff, releaseCoeff;
    PeakMeter inputMeter;
};

/** Sidechain duck for kick/bass separation. */
class SidechainDuckModule final : public StudioFxModule
{
public:
    explicit SidechainDuckModule (float sampleRate) noexcept
        : fs (sampleRate)
        , attackCoeff (dynCoeff (5.0f, sampleRate))
        , releaseCoeff (dynCoeff (120.0f, sampleRate))
        , detector (sampleRate, 25.0f)
    {
    }

    const char* kind() const noexcept override { return "SIDECHAIN_DUCK"; }
    float gainReductionDb() const noexcept override { return grDb; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        thresholdDb = slotValue (slot, "thresholdDb");
        ratio = slotValue (slot, "ratio");
        attackCoeff = dynCoeff (slotValue (slot, "attackMs"), fs);
        releaseCoeff = dynCoeff (slotValue (slot, "releaseMs"), fs);
        floorDb = slotValue (slot, "floorDb");
    }

    void setKey (float l, float r) noexcept override
    {
        keyL = l;
        keyR = r;
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
        const float keyPeak = detector.update (std::max (std::fabs (keyL), std::fabs (keyR)));
        const float level = levelToDb (keyPeak);
        float target = computeGainReductionDb (level, thresholdDb, ratio, 3.0f);
        target = std::min (target, -floorDb);
        const float coeff = target > grDb ? attackCoeff : releaseCoeff;
        grDb = target + coeff * (grDb - target);
        const float gain = dbToLinear (-grDb);
        out[0] = inL * gain;
        out[1] = inR * gain;
    }

    void reset() noexcept override
    {
        grDb = 0.0f;
        keyL = keyR = 0.0f;
        detector.reset();
        inputMeter.reset();
    }

private:
    float fs;
    float thresholdDb = -20.0f, ratio = 6.0f;
    float attackCoeff, releaseCoeff;
    float floorDb = -12.0f, grDb = 0.0f;
    float keyL = 0.0f, keyR = 0.0f;
    PeakEnvelope detector;
    PeakMeter inputMeter;
};

/** True-peak limiter; detection and gain run oversampled with lookahead. */
class TruePeakLimiterModule final : public StudioFxModule
{
public:
    explicit TruePeakLimiterModule (float sampleRate) noexcept
        : fs (sampleRate)
        , releaseCoeff (dynCoeff (120.0f, sampleRate))
        , lookAhead (std::max (1, static_cast<int> (sampleRate * 0.0015f)))
        , delayL (static_cast<std::size_t> (lookAhead), 0.0f)
        , delayR (static_cast<std::size_t> (lookAhead), 0.0f)
    {
    }

    const char* kind() const noexcept override { return "TRUE_PEAK_LIMITER"; }
    float gainReductionDb() const noexcept override { return grDb; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        ceiling = dbToLinear (slotValue (slot, "ceilingDb"));
        releaseCoeff = dynCoeff (slotValue (slot, "releaseMs"), fs);
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
        const float futurePeak = std::max (std::fabs (inL), std::fabs (inR));
        const float targetGain = futurePeak * gain > ceiling ? ceiling / std::max (futurePeak, 1e-6f) : 1.0f;
        gain = targetGain < gain ? targetGain : targetGain + releaseCoeff * (gain - targetGain);
        const float delayedL = delayL[static_cast<std::size_t> (delayPos)];
        const float delayedR = delayR[static_cast<std::size_t> (delayPos)];
        delayL[static_cast<std::size_t> (delayPos)] = inL;
        delayR[static_cast<std::size_t> (delayPos)] = inR;
        delayPos = (delayPos + 1) % lookAhead;
        if (count < lookAhead)
            ++count;
        if (count < lookAhead)
        {
            out[0] = out[1] = 0.0f;
            grDb = -levelToDb (gain);
            outputMeter.update (0.0f, 0.0f);
            return;
        }
        if (oversampler != nullptr)
        {
            oversampler->upsample (delayedL, delayedR);
            for (int i = 0; i < oversampler->factor(); ++i)
            {
                oversampler->bufferL[static_cast<std::size_t> (i)] =
                    limitToCeiling (oversampler->bufferL[static_cast<std::size_t> (i)] * gain);
                oversampler->bufferR[static_cast<std::size_t> (i)] =
                    limitToCeiling (oversampler->bufferR[static_cast<std::size_t> (i)] * gain);
            }
            float scratch[2];
            oversampler->downsample (scratch);
            out[0] = limitToCeiling (scratch[0]);
            out[1] = limitToCeiling (scratch[1]);
        }
        else
        {
            out[0] = limitToCeiling (delayedL * gain);
            out[1] = limitToCeiling (delayedR * gain);
        }
        grDb = -levelToDb (gain);
        outputMeter.update (out[0], out[1]);
    }

    void reset() noexcept override
    {
        gain = 1.0f;
        grDb = 0.0f;
        if (oversampler != nullptr)
            oversampler->reset();
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        delayPos = 0;
        count = 0;
        inputMeter.reset();
        outputMeter.reset();
    }

private:
    float limitToCeiling (float x) const noexcept
    {
        if (x > ceiling)
            return ceiling;
        if (x < -ceiling)
            return -ceiling;
        return x;
    }

    float fs;
    float ceiling = dbToLinear (-1.0f);
    float releaseCoeff;
    int lookAhead;
    std::vector<float> delayL, delayR;
    int delayPos = 0, count = 0;
    std::unique_ptr<Oversampler> oversampler;
    float gain = 1.0f, grDb = 0.0f;
    PeakMeter inputMeter, outputMeter;
};

/** Brickwall channel limiter. */
class ChannelLimiterModule final : public StudioFxModule
{
public:
    explicit ChannelLimiterModule (float sampleRate) noexcept
        : fs (sampleRate)
        , releaseCoeff (dynCoeff (30.0f, sampleRate))
    {
    }

    const char* kind() const noexcept override { return "CHANNEL_LIMITER"; }
    float gainReductionDb() const noexcept override { return grDb; }

    void configure (const FxSlot& slot) override
    {
        enabled = slot.enabled;
        ceiling = dbToLinear (slotValue (slot, "ceilingDb"));
        releaseCoeff = dynCoeff (slotValue (slot, "releaseMs"), fs);
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
        const float peak = std::max (std::fabs (inL), std::fabs (inR));
        const float targetGain = peak * gain > ceiling ? ceiling / std::max (peak, 1e-6f) : 1.0f;
        gain = targetGain < gain ? targetGain : targetGain + releaseCoeff * (gain - targetGain);
        out[0] = inL * gain;
        out[1] = inR * gain;
        grDb = -levelToDb (gain);
    }

    void reset() noexcept override
    {
        gain = 1.0f;
        grDb = 0.0f;
        inputMeter.reset();
    }

private:
    float fs;
    float ceiling = dbToLinear (-0.5f);
    float releaseCoeff;
    float gain = 1.0f, grDb = 0.0f;
    PeakMeter inputMeter;
};

} // namespace nc::engine::studiofx
