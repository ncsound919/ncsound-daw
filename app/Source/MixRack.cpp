#include "MixRack.h"

#include "fx/Biquad.h"
#include "fx/Compressor.h"
#include "fx/Loudness.h"
#include "fx/MasterLimiter.h"
#include "fx/Saturator.h"
#include "mpm/SaturationStage.h"

#include "MasterDsp.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nc
{
namespace
{
float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

float readFloat (const juce::var& object, const char* key, float fallback)
{
    if (! object.isObject())
        return fallback;

    const auto value = object.getProperty (key, juce::var());

    if (value.isVoid() || value.isUndefined())
        return fallback;

    if (value.isBool())
        return value == juce::var (true) ? 1.0f : 0.0f;

    return static_cast<float> (static_cast<double> (value));
}

bool readBool (const juce::var& object, const char* key, bool fallback)
{
    return readFloat (object, key, fallback ? 1.0f : 0.0f) > 0.5f;
}

float clampGain (float value) { return std::clamp (value, 0.0f, 4.0f); }
} // namespace

//==============================================================================
struct MixRackChannel::Impl
{
    fx::StereoBand hpf, lpf, low, lmf, hmf, high;
    fx::Compressor comp;
    fx::Saturator sat;

    mpm::SaturationStage micLeft { mpm::SaturationStage::Params { 0.0, true, 0, 80.0 } };
    mpm::SaturationStage micRight { mpm::SaturationStage::Params { 0.0, true, 0, 80.0 } };
    std::vector<double> micLeftScratch, micRightScratch;

    double sampleRate = 48000.0;
    int maxBlock = 0;
    bool prepared = false;

    float cachedHpfOn = -1.0f, cachedHpfFreq = -1.0f;
    float cachedLpfOn = -1.0f, cachedLpfFreq = -1.0f;
    float cachedLowFreq = -1.0f, cachedLowGain = -999.0f;
    float cachedLmfFreq = -1.0f, cachedLmfGain = -999.0f, cachedLmfQ = -1.0f;
    float cachedHmfFreq = -1.0f, cachedHmfGain = -999.0f, cachedHmfQ = -1.0f;
    float cachedHighFreq = -1.0f, cachedHighGain = -999.0f;
    float cachedCompOn = -1.0f, cachedCompThresh = -999.0f, cachedCompRatio = -1.0f,
          cachedCompAttack = -1.0f, cachedCompRelease = -1.0f, cachedCompMakeup = -999.0f;
    float cachedSatOn = -1.0f, cachedSatDrive = -999.0f, cachedSatMix = -1.0f;
};

MixRackChannel::MixRackChannel() : impl (std::make_unique<Impl>()) {}
MixRackChannel::~MixRackChannel() = default;

void MixRackChannel::prepare (double sampleRate, int maxBlockSize)
{
    auto& d = *impl;
    d.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    d.maxBlock = std::max (1, maxBlockSize);

    d.hpf.reset();
    d.lpf.reset();
    d.low.reset();
    d.lmf.reset();
    d.hmf.reset();
    d.high.reset();
    d.comp.prepare (d.sampleRate, 2);
    d.comp.reset();
    d.sat.prepare (d.sampleRate, d.maxBlock, 2);
    d.sat.setOversample (false); // zero-latency on a track insert
    d.sat.reset();

    const mpm::ProcessSpec spec { d.sampleRate, d.maxBlock };
    d.micLeft.prepare (spec);
    d.micRight.prepare (spec);
    d.micLeft.reset();
    d.micRight.reset();
    d.micLeftScratch.assign (static_cast<size_t> (d.maxBlock), 0.0);
    d.micRightScratch.assign (static_cast<size_t> (d.maxBlock), 0.0);

    d.cachedHpfOn = d.cachedHpfFreq = -1.0f;
    d.cachedLpfOn = d.cachedLpfFreq = -1.0f;
    d.cachedLowFreq = d.cachedLowGain = -1.0f;
    d.cachedLmfFreq = d.cachedLmfGain = d.cachedLmfQ = -1.0f;
    d.cachedHmfFreq = d.cachedHmfGain = d.cachedHmfQ = -1.0f;
    d.cachedHighFreq = d.cachedHighGain = -1.0f;
    d.cachedCompOn = d.cachedCompThresh = d.cachedCompRatio = d.cachedCompAttack = -1.0f;
    d.cachedCompRelease = d.cachedCompMakeup = -1.0f;
    d.cachedSatOn = d.cachedSatDrive = d.cachedSatMix = -1.0f;
    d.prepared = true;
}

void MixRackChannel::reset()
{
    auto& d = *impl;
    d.hpf.reset();
    d.lpf.reset();
    d.low.reset();
    d.lmf.reset();
    d.hmf.reset();
    d.high.reset();
    d.comp.reset();
    d.sat.reset();
    d.micLeft.reset();
    d.micRight.reset();
}

void MixRackChannel::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto& d = *impl;

    if (! d.prepared || numSamples <= 0 || buffer.getNumChannels() < 2
        || numSamples > buffer.getNumSamples())
        return;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
        processSample (left[i], right[i]);
}

void MixRackChannel::processSample (float& left, float& right)
{
    auto& d = *impl;

    if (! d.prepared || ! params.stripOn.load())
        return;

    // Open Mic sits in front of the strip on vocal tracks only. The stage runs
    // at 1x with ADAA, so a vocal track gains no latency.
    if (params.vocal.load() && params.openMicOn.load())
    {
        const auto mix = std::clamp (params.openMicMix.load(), 0.0f, 1.0f);

        if (mix > 0.0f)
        {
            const auto drive = dbToGain (params.openMicDriveDb.load());
            const auto trim = dbToGain (params.openMicTrimDb.load());
            const auto inverse = 1.0f / std::max (1.0e-6f, drive);
            auto micL = static_cast<double> (left) * drive;
            auto micR = static_cast<double> (right) * drive;

            d.micLeft.process (&micL, 1);
            d.micRight.process (&micR, 1);

            const auto wetL = static_cast<float> (micL) * inverse * trim;
            const auto wetR = static_cast<float> (micR) * inverse * trim;
            left += mix * (wetL - left);
            right += mix * (wetR - right);
        }
    }

    const auto eqOn = params.eqOn.load();

    if (eqOn && params.hpfOn.load())
    {
        const auto freq = params.hpfFreq.load();

        if (d.cachedHpfOn != 1.0f || d.cachedHpfFreq != freq)
        {
            d.hpf.set (fx::FilterType::highPass, d.sampleRate, std::max (20.0f, freq), 0.707, 0.0);
            d.cachedHpfOn = 1.0f;
            d.cachedHpfFreq = freq;
        }

        d.hpf.process (left, right);
    }

    if (eqOn && params.lpfOn.load())
    {
        const auto freq = params.lpfFreq.load();

        if (d.cachedLpfOn != 1.0f || d.cachedLpfFreq != freq)
        {
            d.lpf.set (fx::FilterType::lowPass, d.sampleRate,
                       std::clamp (freq, 1000.0f, 20000.0f), 0.707, 0.0);
            d.cachedLpfOn = 1.0f;
            d.cachedLpfFreq = freq;
        }

        d.lpf.process (left, right);
    }

    if (eqOn)
    {
        const auto lowFreq = params.lowFreq.load();
        const auto lowGain = params.lowGainDb.load();

        if (d.cachedLowFreq != lowFreq || d.cachedLowGain != lowGain)
        {
            d.low.set (fx::FilterType::lowShelf, d.sampleRate, lowFreq, 0.707, lowGain);
            d.cachedLowFreq = lowFreq;
            d.cachedLowGain = lowGain;
        }

        const auto lmfFreq = params.lmfFreq.load();
        const auto lmfGain = params.lmfGainDb.load();
        const auto lmfQ = params.lmfQ.load();

        if (d.cachedLmfFreq != lmfFreq || d.cachedLmfGain != lmfGain || d.cachedLmfQ != lmfQ)
        {
            d.lmf.set (fx::FilterType::peak, d.sampleRate, lmfFreq, std::max (0.1f, lmfQ), lmfGain);
            d.cachedLmfFreq = lmfFreq;
            d.cachedLmfGain = lmfGain;
            d.cachedLmfQ = lmfQ;
        }

        const auto hmfFreq = params.hmfFreq.load();
        const auto hmfGain = params.hmfGainDb.load();
        const auto hmfQ = params.hmfQ.load();

        if (d.cachedHmfFreq != hmfFreq || d.cachedHmfGain != hmfGain || d.cachedHmfQ != hmfQ)
        {
            d.hmf.set (fx::FilterType::peak, d.sampleRate, hmfFreq, std::max (0.1f, hmfQ), hmfGain);
            d.cachedHmfFreq = hmfFreq;
            d.cachedHmfGain = hmfGain;
            d.cachedHmfQ = hmfQ;
        }

        const auto highFreq = params.highFreq.load();
        const auto highGain = params.highGainDb.load();

        if (d.cachedHighFreq != highFreq || d.cachedHighGain != highGain)
        {
            d.high.set (fx::FilterType::highShelf, d.sampleRate, highFreq, 0.707, highGain);
            d.cachedHighFreq = highFreq;
            d.cachedHighGain = highGain;
        }

        d.low.process (left, right);
        d.lmf.process (left, right);
        d.hmf.process (left, right);
        d.high.process (left, right);
    }

    const auto compOn = params.compOn.load();

    if (compOn)
    {
        const auto thresh = params.compThreshDb.load();
        const auto ratio = std::max (1.0f, params.compRatio.load());
        const auto attack = params.compAttackMs.load();
        const auto release = params.compReleaseMs.load();
        const auto makeup = params.compMakeupDb.load();

        if (d.cachedCompOn != 1.0f || d.cachedCompThresh != thresh || d.cachedCompRatio != ratio
            || d.cachedCompAttack != attack || d.cachedCompRelease != release || d.cachedCompMakeup != makeup)
        {
            fx::Compressor::Params compParams;
            compParams.thresholdDb = thresh;
            compParams.ratio = ratio;
            compParams.attackMs = attack;
            compParams.releaseMs = release;
            compParams.kneeDb = 6.0f;
            compParams.makeupDb = makeup;
            compParams.mix = 1.0f;
            compParams.sidechainHpfHz = 20.0f;
            compParams.type = fx::Compressor::Type::vca;
            d.comp.setParams (compParams);
            d.cachedCompOn = 1.0f;
            d.cachedCompThresh = thresh;
            d.cachedCompRatio = ratio;
            d.cachedCompAttack = attack;
            d.cachedCompRelease = release;
            d.cachedCompMakeup = makeup;
        }

        d.comp.process (left, right);
    }
    else
    {
        d.cachedCompOn = 0.0f;
    }

    if (params.satOn.load())
    {
        const auto drive = params.satDriveDb.load();
        const auto mix = std::clamp (params.satMix.load(), 0.0f, 1.0f);

        if (d.cachedSatOn != 1.0f || d.cachedSatDrive != drive || d.cachedSatMix != mix)
        {
            d.sat.setMode (fx::Saturator::Mode::tape);
            d.sat.setDriveDb (drive);
            d.sat.setMix (mix);
            d.sat.setOversample (false);
            d.cachedSatOn = 1.0f;
            d.cachedSatDrive = drive;
            d.cachedSatMix = mix;
        }

        d.sat.process (left, right);
    }
    else
    {
        d.cachedSatOn = 0.0f;
    }

    const auto output = dbToGain (params.outputDb.load());
    left *= output;
    right *= output;
}

void MixRackChannel::setParam (const juce::String& id, float value)
{
    if (id == "stripOn") params.stripOn.store (value > 0.5f);
    else if (id == "vocal") params.vocal.store (value > 0.5f);
    else if (id == "openMicOn")
    {
        params.openMicOn.store (value > 0.5f);
        params.openMicUserSet.store (true);
    }
    else if (id == "openMicDrive") params.openMicDriveDb.store (std::clamp (value, -12.0f, 24.0f));
    else if (id == "openMicMix") params.openMicMix.store (std::clamp (value, 0.0f, 1.0f));
    else if (id == "openMicTrim") params.openMicTrimDb.store (std::clamp (value, -24.0f, 24.0f));
    else if (id == "eqOn") params.eqOn.store (value > 0.5f);
    else if (id == "hpfOn") params.hpfOn.store (value > 0.5f);
    else if (id == "hpfFreq") params.hpfFreq.store (std::clamp (value, 20.0f, 500.0f));
    else if (id == "lpfOn") params.lpfOn.store (value > 0.5f);
    else if (id == "lpfFreq") params.lpfFreq.store (std::clamp (value, 1000.0f, 20000.0f));
    else if (id == "lowFreq") params.lowFreq.store (std::clamp (value, 20.0f, 400.0f));
    else if (id == "lowGain") params.lowGainDb.store (std::clamp (value, -18.0f, 18.0f));
    else if (id == "lmfFreq") params.lmfFreq.store (std::clamp (value, 150.0f, 3000.0f));
    else if (id == "lmfGain") params.lmfGainDb.store (std::clamp (value, -18.0f, 18.0f));
    else if (id == "lmfQ") params.lmfQ.store (std::clamp (value, 0.2f, 10.0f));
    else if (id == "hmfFreq") params.hmfFreq.store (std::clamp (value, 1000.0f, 12000.0f));
    else if (id == "hmfGain") params.hmfGainDb.store (std::clamp (value, -18.0f, 18.0f));
    else if (id == "hmfQ") params.hmfQ.store (std::clamp (value, 0.2f, 10.0f));
    else if (id == "highFreq") params.highFreq.store (std::clamp (value, 5000.0f, 20000.0f));
    else if (id == "highGain") params.highGainDb.store (std::clamp (value, -18.0f, 18.0f));
    else if (id == "compOn") params.compOn.store (value > 0.5f);
    else if (id == "compThresh") params.compThreshDb.store (std::clamp (value, -60.0f, 0.0f));
    else if (id == "compRatio") params.compRatio.store (std::clamp (value, 1.0f, 20.0f));
    else if (id == "compAttack") params.compAttackMs.store (std::clamp (value, 0.1f, 200.0f));
    else if (id == "compRelease") params.compReleaseMs.store (std::clamp (value, 10.0f, 1000.0f));
    else if (id == "compMakeup") params.compMakeupDb.store (std::clamp (value, -12.0f, 24.0f));
    else if (id == "satOn") params.satOn.store (value > 0.5f);
    else if (id == "satDrive") params.satDriveDb.store (std::clamp (value, 0.0f, 24.0f));
    else if (id == "satMix") params.satMix.store (std::clamp (value, 0.0f, 1.0f));
    else if (id == "outputDb") params.outputDb.store (std::clamp (value, -24.0f, 12.0f));
}

float MixRackChannel::getParam (const juce::String& id) const
{
    if (id == "stripOn") return params.stripOn.load() ? 1.0f : 0.0f;
    if (id == "vocal") return params.vocal.load() ? 1.0f : 0.0f;
    if (id == "openMicOn") return params.openMicOn.load() ? 1.0f : 0.0f;
    if (id == "openMicDrive") return params.openMicDriveDb.load();
    if (id == "openMicMix") return params.openMicMix.load();
    if (id == "openMicTrim") return params.openMicTrimDb.load();
    if (id == "eqOn") return params.eqOn.load() ? 1.0f : 0.0f;
    if (id == "hpfOn") return params.hpfOn.load() ? 1.0f : 0.0f;
    if (id == "hpfFreq") return params.hpfFreq.load();
    if (id == "lpfOn") return params.lpfOn.load() ? 1.0f : 0.0f;
    if (id == "lpfFreq") return params.lpfFreq.load();
    if (id == "lowFreq") return params.lowFreq.load();
    if (id == "lowGain") return params.lowGainDb.load();
    if (id == "lmfFreq") return params.lmfFreq.load();
    if (id == "lmfGain") return params.lmfGainDb.load();
    if (id == "lmfQ") return params.lmfQ.load();
    if (id == "hmfFreq") return params.hmfFreq.load();
    if (id == "hmfGain") return params.hmfGainDb.load();
    if (id == "hmfQ") return params.hmfQ.load();
    if (id == "highFreq") return params.highFreq.load();
    if (id == "highGain") return params.highGainDb.load();
    if (id == "compOn") return params.compOn.load() ? 1.0f : 0.0f;
    if (id == "compThresh") return params.compThreshDb.load();
    if (id == "compRatio") return params.compRatio.load();
    if (id == "compAttack") return params.compAttackMs.load();
    if (id == "compRelease") return params.compReleaseMs.load();
    if (id == "compMakeup") return params.compMakeupDb.load();
    if (id == "satOn") return params.satOn.load() ? 1.0f : 0.0f;
    if (id == "satDrive") return params.satDriveDb.load();
    if (id == "satMix") return params.satMix.load();
    if (id == "outputDb") return params.outputDb.load();
    return 0.0f;
}

juce::var MixRackChannel::toVar() const
{
    auto* object = new juce::DynamicObject();

    object->setProperty ("stripOn", params.stripOn.load());
    object->setProperty ("vocal", params.vocal.load());
    object->setProperty ("openMicOn", params.openMicOn.load());
    object->setProperty ("openMicUserSet", params.openMicUserSet.load());
    object->setProperty ("openMicDrive", params.openMicDriveDb.load());
    object->setProperty ("openMicMix", params.openMicMix.load());
    object->setProperty ("openMicTrim", params.openMicTrimDb.load());
    object->setProperty ("eqOn", params.eqOn.load());
    object->setProperty ("hpfOn", params.hpfOn.load());
    object->setProperty ("hpfFreq", params.hpfFreq.load());
    object->setProperty ("lpfOn", params.lpfOn.load());
    object->setProperty ("lpfFreq", params.lpfFreq.load());
    object->setProperty ("lowFreq", params.lowFreq.load());
    object->setProperty ("lowGain", params.lowGainDb.load());
    object->setProperty ("lmfFreq", params.lmfFreq.load());
    object->setProperty ("lmfGain", params.lmfGainDb.load());
    object->setProperty ("lmfQ", params.lmfQ.load());
    object->setProperty ("hmfFreq", params.hmfFreq.load());
    object->setProperty ("hmfGain", params.hmfGainDb.load());
    object->setProperty ("hmfQ", params.hmfQ.load());
    object->setProperty ("highFreq", params.highFreq.load());
    object->setProperty ("highGain", params.highGainDb.load());
    object->setProperty ("compOn", params.compOn.load());
    object->setProperty ("compThresh", params.compThreshDb.load());
    object->setProperty ("compRatio", params.compRatio.load());
    object->setProperty ("compAttack", params.compAttackMs.load());
    object->setProperty ("compRelease", params.compReleaseMs.load());
    object->setProperty ("compMakeup", params.compMakeupDb.load());
    object->setProperty ("satOn", params.satOn.load());
    object->setProperty ("satDrive", params.satDriveDb.load());
    object->setProperty ("satMix", params.satMix.load());
    object->setProperty ("outputDb", params.outputDb.load());
    return juce::var (object);
}

void MixRackChannel::fromVar (const juce::var& state)
{
    if (! state.isObject())
        return;

    params.stripOn.store (readBool (state, "stripOn", params.stripOn.load()));
    params.vocal.store (readBool (state, "vocal", params.vocal.load()));
    params.openMicOn.store (readBool (state, "openMicOn", params.openMicOn.load()));
    // Sessions saved before the stage was baked in carry no flag, so their
    // vocal tracks adopt the new default instead of staying silent.
    params.openMicUserSet.store (readBool (state, "openMicUserSet", false));
    params.openMicDriveDb.store (readFloat (state, "openMicDrive", params.openMicDriveDb.load()));
    params.openMicMix.store (readFloat (state, "openMicMix", params.openMicMix.load()));
    params.openMicTrimDb.store (readFloat (state, "openMicTrim", params.openMicTrimDb.load()));
    params.eqOn.store (readBool (state, "eqOn", params.eqOn.load()));
    params.hpfOn.store (readBool (state, "hpfOn", params.hpfOn.load()));
    params.hpfFreq.store (readFloat (state, "hpfFreq", params.hpfFreq.load()));
    params.lpfOn.store (readBool (state, "lpfOn", params.lpfOn.load()));
    params.lpfFreq.store (readFloat (state, "lpfFreq", params.lpfFreq.load()));
    params.lowFreq.store (readFloat (state, "lowFreq", params.lowFreq.load()));
    params.lowGainDb.store (readFloat (state, "lowGain", params.lowGainDb.load()));
    params.lmfFreq.store (readFloat (state, "lmfFreq", params.lmfFreq.load()));
    params.lmfGainDb.store (readFloat (state, "lmfGain", params.lmfGainDb.load()));
    params.lmfQ.store (readFloat (state, "lmfQ", params.lmfQ.load()));
    params.hmfFreq.store (readFloat (state, "hmfFreq", params.hmfFreq.load()));
    params.hmfGainDb.store (readFloat (state, "hmfGain", params.hmfGainDb.load()));
    params.hmfQ.store (readFloat (state, "hmfQ", params.hmfQ.load()));
    params.highFreq.store (readFloat (state, "highFreq", params.highFreq.load()));
    params.highGainDb.store (readFloat (state, "highGain", params.highGainDb.load()));
    params.compOn.store (readBool (state, "compOn", params.compOn.load()));
    params.compThreshDb.store (readFloat (state, "compThresh", params.compThreshDb.load()));
    params.compRatio.store (readFloat (state, "compRatio", params.compRatio.load()));
    params.compAttackMs.store (readFloat (state, "compAttack", params.compAttackMs.load()));
    params.compReleaseMs.store (readFloat (state, "compRelease", params.compReleaseMs.load()));
    params.compMakeupDb.store (readFloat (state, "compMakeup", params.compMakeupDb.load()));
    params.satOn.store (readBool (state, "satOn", params.satOn.load()));
    params.satDriveDb.store (readFloat (state, "satDrive", params.satDriveDb.load()));
    params.satMix.store (readFloat (state, "satMix", params.satMix.load()));
    params.outputDb.store (readFloat (state, "outputDb", params.outputDb.load()));
}

//==============================================================================
struct MasterRack::Impl
{
    fx::StereoBand hpf, lpf;
    overlay::master::dsp::EqModule eq;
    overlay::master::dsp::DynamicsModule dyn;
    overlay::master::dsp::ImagerModule img;
    fx::LookaheadLimiter limiter;
    fx::LoudnessMeter loudness;

    double sampleRate = 48000.0;
    int maxBlock = 0;
    bool prepared = false;
    float autoGainDb = 0.0f;
    float cachedHpfOn = -1.0f, cachedHpfFreq = -1.0f;
    float cachedLpfOn = -1.0f, cachedLpfFreq = -1.0f;
};

MasterRack::MasterRack() : impl (std::make_unique<Impl>()) {}
MasterRack::~MasterRack() = default;

void MasterRack::prepare (double sampleRate, int maxBlockSize)
{
    auto& d = *impl;
    d.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    d.maxBlock = std::max (1, maxBlockSize);
    d.hpf.reset();
    d.lpf.reset();
    d.cachedHpfOn = d.cachedHpfFreq = -1.0f;
    d.cachedLpfOn = d.cachedLpfFreq = -1.0f;
    d.eq.prepare (d.sampleRate);
    d.dyn.prepare (d.sampleRate);
    d.img.prepare (d.sampleRate);
    d.limiter.prepare (d.sampleRate);
    d.loudness.prepare (d.sampleRate, 2);
    d.autoGainDb = 0.0f;
    d.prepared = true;
}

void MasterRack::reset()
{
    auto& d = *impl;
    d.hpf.reset();
    d.lpf.reset();
    d.eq.reset();
    d.dyn.reset();
    d.img.reset();
    d.limiter.reset();
    d.loudness.reset();
    d.autoGainDb = 0.0f;
}

void MasterRack::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto& d = *impl;

    if (! d.prepared || numSamples <= 0 || buffer.getNumChannels() < 2
        || numSamples > buffer.getNumSamples())
        return;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    const float* meterChannels[2] { left, right };
    d.loudness.process (meterChannels, 2, numSamples);

    if (params.compare.load())
    {
        const auto trim = dbToGain (params.trimDb.load());

        if (std::abs (trim - 1.0f) > 1.0e-6f)
            buffer.applyGain (0, numSamples, trim);

        return;
    }

    const auto eqOn = params.eqOn.load();

    if (eqOn && params.hpfOn.load())
    {
        const auto freq = params.hpfFreq.load();

        if (d.cachedHpfOn != 1.0f || d.cachedHpfFreq != freq)
        {
            d.hpf.set (fx::FilterType::highPass, d.sampleRate,
                       std::clamp (freq, 20.0f, 500.0f), 0.707, 0.0);
            d.cachedHpfOn = 1.0f;
            d.cachedHpfFreq = freq;
        }

        for (int i = 0; i < numSamples; ++i)
            d.hpf.process (left[i], right[i]);
    }

    if (eqOn && params.lpfOn.load())
    {
        const auto freq = params.lpfFreq.load();

        if (d.cachedLpfOn != 1.0f || d.cachedLpfFreq != freq)
        {
            d.lpf.set (fx::FilterType::lowPass, d.sampleRate,
                       std::clamp (freq, 1000.0f, 20000.0f), 0.707, 0.0);
            d.cachedLpfOn = 1.0f;
            d.cachedLpfFreq = freq;
        }

        for (int i = 0; i < numSamples; ++i)
            d.lpf.process (left[i], right[i]);
    }

    if (eqOn)
    {
        std::array<overlay::master::dsp::EqBand, overlay::master::dsp::EqModule::numBands> bands {};

        bands[0] = overlay::master::dsp::EqBand { true, 1, params.lowFreq.load(), params.lowGainDb.load(), 0.707f, 0, 0 };
        bands[1] = overlay::master::dsp::EqBand { true, 0, params.midFreq.load(), params.midGainDb.load(), 1.0f, 0, 0 };
        bands[2] = overlay::master::dsp::EqBand { true, 2, params.highFreq.load(), params.highGainDb.load(), 0.707f, 0, 0 };
        d.eq.setBands (bands);
        d.eq.process (left, right, numSamples);
    }

    const auto dynOn = params.dynOn.load();

    if (dynOn)
    {
        std::array<overlay::master::dsp::DynBand, overlay::master::dsp::DynamicsModule::maxBands> bands {};
        bands[0] = overlay::master::dsp::DynBand { true, params.dynThreshDb.load(), params.dynRatio.load(), 10.0f, 120.0f, 0.0f };
        d.dyn.setParams (1, 120.0f, 1000.0f, 8000.0f, 6.0f, false, 1.0f, bands);
        d.dyn.process (left, right, numSamples);
    }

    if (params.imgOn.load())
    {
        std::array<float, overlay::master::dsp::ImagerModule::maxBands> widths { 1.0f, 1.0f, 1.0f, 1.0f };
        widths[0] = std::clamp (params.width.load(), 0.0f, 2.0f);
        d.img.setParams (1, 120.0f, 1000.0f, 8000.0f, widths, 0.0f);
        d.img.process (left, right, numSamples);
    }

    if (params.automixOn.load())
    {
        const auto measured = d.loudness.shortTerm();

        if (measured > -69.0f)
        {
            const auto target = params.targetLufs.load();
            const auto error = std::clamp (target - measured, -12.0f, 12.0f);
            d.autoGainDb += (error - d.autoGainDb) * 0.015f;

            const auto gain = dbToGain (d.autoGainDb);

            if (std::abs (gain - 1.0f) > 1.0e-6f)
                buffer.applyGain (0, numSamples, gain);
        }
    }
    else
    {
        d.autoGainDb *= 0.90f;
    }

    const auto trim = dbToGain (params.trimDb.load());

    if (std::abs (trim - 1.0f) > 1.0e-6f)
        buffer.applyGain (0, numSamples, trim);

    if (params.maxOn.load())
    {
        fx::LookaheadLimiter::Params limiterParams;
        limiterParams.thresholdDb = 0.0f;
        limiterParams.ceilingDb = params.ceilingDb.load();
        limiterParams.releaseMs = 120.0f;
        limiterParams.mode = fx::LookaheadLimiter::Mode::clean;
        limiterParams.link = 1.0f;
        limiterParams.transient = 0.0f;
        d.limiter.setParams (limiterParams);
        d.limiter.process (left, right, numSamples);
    }
}

void MasterRack::setParam (const juce::String& id, float value)
{
    if (id == "eqOn") params.eqOn.store (value > 0.5f);
    else if (id == "hpfOn") params.hpfOn.store (value > 0.5f);
    else if (id == "hpfFreq") params.hpfFreq.store (std::clamp (value, 20.0f, 500.0f));
    else if (id == "lpfOn") params.lpfOn.store (value > 0.5f);
    else if (id == "lpfFreq") params.lpfFreq.store (std::clamp (value, 1000.0f, 20000.0f));
    else if (id == "lowFreq") params.lowFreq.store (std::clamp (value, 20.0f, 400.0f));
    else if (id == "lowGain") params.lowGainDb.store (std::clamp (value, -12.0f, 12.0f));
    else if (id == "midFreq" || id == "lmfFreq" || id == "hmfFreq")
        params.midFreq.store (std::clamp (value, 150.0f, 8000.0f));
    else if (id == "midGain" || id == "lmfGain" || id == "hmfGain") params.midGainDb.store (std::clamp (value, -12.0f, 12.0f));
    else if (id == "highFreq") params.highFreq.store (std::clamp (value, 2000.0f, 20000.0f));
    else if (id == "highGain") params.highGainDb.store (std::clamp (value, -12.0f, 12.0f));
    else if (id == "dynOn" || id == "compOn") params.dynOn.store (value > 0.5f);
    else if (id == "dynThresh" || id == "compThresh") params.dynThreshDb.store (std::clamp (value, -60.0f, 0.0f));
    else if (id == "dynRatio" || id == "compRatio") params.dynRatio.store (std::clamp (value, 1.0f, 20.0f));
    else if (id == "imgOn") params.imgOn.store (value > 0.5f);
    else if (id == "width") params.width.store (std::clamp (value, 0.0f, 2.0f));
    else if (id == "maxOn") params.maxOn.store (value > 0.5f);
    else if (id == "ceiling") params.ceilingDb.store (std::clamp (value, -6.0f, 0.0f));
    else if (id == "automixOn") params.automixOn.store (value > 0.5f);
    else if (id == "targetLufs") params.targetLufs.store (std::clamp (value, -30.0f, -5.0f));
    else if (id == "automixProfile")
    {
        static constexpr float targets[] { -14.0f, -9.0f, -12.0f, -10.0f };
        const auto profile = juce::jlimit (0, 3, static_cast<int> (std::round (value)));
        params.automixProfile.store (profile);
        params.targetLufs.store (targets[profile]);
    }
    else if (id == "compare" || id == "bypass") params.compare.store (value > 0.5f);
    else if (id == "trimDb" || id == "outputTrim") params.trimDb.store (std::clamp (value, -24.0f, 12.0f));
}

float MasterRack::getParam (const juce::String& id) const
{
    if (id == "eqOn") return params.eqOn.load() ? 1.0f : 0.0f;
    if (id == "hpfOn") return params.hpfOn.load() ? 1.0f : 0.0f;
    if (id == "hpfFreq") return params.hpfFreq.load();
    if (id == "lpfOn") return params.lpfOn.load() ? 1.0f : 0.0f;
    if (id == "lpfFreq") return params.lpfFreq.load();
    if (id == "lowFreq") return params.lowFreq.load();
    if (id == "lowGain") return params.lowGainDb.load();
    if (id == "midFreq") return params.midFreq.load();
    if (id == "midGain") return params.midGainDb.load();
    if (id == "highFreq") return params.highFreq.load();
    if (id == "highGain") return params.highGainDb.load();
    if (id == "dynOn") return params.dynOn.load() ? 1.0f : 0.0f;
    if (id == "dynThresh") return params.dynThreshDb.load();
    if (id == "dynRatio") return params.dynRatio.load();
    if (id == "imgOn") return params.imgOn.load() ? 1.0f : 0.0f;
    if (id == "width") return params.width.load();
    if (id == "maxOn") return params.maxOn.load() ? 1.0f : 0.0f;
    if (id == "ceiling") return params.ceilingDb.load();
    if (id == "automixOn") return params.automixOn.load() ? 1.0f : 0.0f;
    if (id == "targetLufs") return params.targetLufs.load();
    if (id == "automixProfile") return static_cast<float> (params.automixProfile.load());
    if (id == "compare") return params.compare.load() ? 1.0f : 0.0f;
    if (id == "trimDb") return params.trimDb.load();
    return 0.0f;
}

juce::var MasterRack::toVar() const
{
    auto* object = new juce::DynamicObject();

    object->setProperty ("eqOn", params.eqOn.load());
    object->setProperty ("hpfOn", params.hpfOn.load());
    object->setProperty ("hpfFreq", params.hpfFreq.load());
    object->setProperty ("lpfOn", params.lpfOn.load());
    object->setProperty ("lpfFreq", params.lpfFreq.load());
    object->setProperty ("lowFreq", params.lowFreq.load());
    object->setProperty ("lowGain", params.lowGainDb.load());
    object->setProperty ("midFreq", params.midFreq.load());
    object->setProperty ("midGain", params.midGainDb.load());
    object->setProperty ("highFreq", params.highFreq.load());
    object->setProperty ("highGain", params.highGainDb.load());
    object->setProperty ("dynOn", params.dynOn.load());
    object->setProperty ("dynThresh", params.dynThreshDb.load());
    object->setProperty ("dynRatio", params.dynRatio.load());
    object->setProperty ("imgOn", params.imgOn.load());
    object->setProperty ("width", params.width.load());
    object->setProperty ("maxOn", params.maxOn.load());
    object->setProperty ("ceiling", params.ceilingDb.load());
    object->setProperty ("automixOn", params.automixOn.load());
    object->setProperty ("targetLufs", params.targetLufs.load());
    object->setProperty ("automixProfile", params.automixProfile.load());
    object->setProperty ("compare", params.compare.load());
    object->setProperty ("trimDb", params.trimDb.load());
    return juce::var (object);
}

void MasterRack::fromVar (const juce::var& state)
{
    if (! state.isObject())
        return;

    params.eqOn.store (readBool (state, "eqOn", params.eqOn.load()));
    params.hpfOn.store (readBool (state, "hpfOn", params.hpfOn.load()));
    params.hpfFreq.store (readFloat (state, "hpfFreq", params.hpfFreq.load()));
    params.lpfOn.store (readBool (state, "lpfOn", params.lpfOn.load()));
    params.lpfFreq.store (readFloat (state, "lpfFreq", params.lpfFreq.load()));
    params.lowFreq.store (readFloat (state, "lowFreq", params.lowFreq.load()));
    params.lowGainDb.store (readFloat (state, "lowGain", params.lowGainDb.load()));
    params.midFreq.store (readFloat (state, "midFreq", params.midFreq.load()));
    params.midGainDb.store (readFloat (state, "midGain", params.midGainDb.load()));
    params.highFreq.store (readFloat (state, "highFreq", params.highFreq.load()));
    params.highGainDb.store (readFloat (state, "highGain", params.highGainDb.load()));
    params.dynOn.store (readBool (state, "dynOn", params.dynOn.load()));
    params.dynThreshDb.store (readFloat (state, "dynThresh", params.dynThreshDb.load()));
    params.dynRatio.store (readFloat (state, "dynRatio", params.dynRatio.load()));
    params.imgOn.store (readBool (state, "imgOn", params.imgOn.load()));
    params.width.store (readFloat (state, "width", params.width.load()));
    params.maxOn.store (readBool (state, "maxOn", params.maxOn.load()));
    params.ceilingDb.store (readFloat (state, "ceiling", params.ceilingDb.load()));
    params.automixOn.store (readBool (state, "automixOn", params.automixOn.load()));
    params.targetLufs.store (readFloat (state, "targetLufs", params.targetLufs.load()));
    params.automixProfile.store (static_cast<int> (std::round (readFloat (state, "automixProfile", static_cast<float> (params.automixProfile.load())))));
    params.compare.store (readBool (state, "compare", params.compare.load()));
    params.trimDb.store (readFloat (state, "trimDb", params.trimDb.load()));
}

void MasterRack::captureAB (int slot)
{
    const auto index = juce::jlimit (0, 1, slot);
    ab[static_cast<size_t> (index)] = toVar();
    abValid[static_cast<size_t> (index)] = true;
    activeSlot.store (index);
}

void MasterRack::recallAB (int slot)
{
    const auto index = juce::jlimit (0, 1, slot);

    if (! abValid[static_cast<size_t> (index)])
        return;

    fromVar (ab[static_cast<size_t> (index)]);
    activeSlot.store (index);
}

bool MasterRack::hasAB (int slot) const
{
    return slot >= 0 && slot < 2 && abValid[static_cast<size_t> (slot)];
}

float MasterRack::shortTermLufs() const { return impl->loudness.shortTerm(); }
float MasterRack::integratedLufs() const { return impl->loudness.integrated(); }
float MasterRack::truePeakDb() const { return impl->loudness.truePeakMaxDbtp(); }
float MasterRack::gainReductionDb() const { return impl->limiter.gainReductionDb(); }
int MasterRack::latencySamples() const { return impl->limiter.latencySamples(); }
} // namespace nc
