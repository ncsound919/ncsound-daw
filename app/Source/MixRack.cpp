#include "MixRack.h"

#include "MixRackDsp.h"

#include "fx/Biquad.h"
#include "fx/Compressor.h"
#include "fx/Loudness.h"
#include "fx/MasterLimiter.h"
#include "fx/Saturator.h"
#include "mpm/SaturationStage.h"

#include "MasterDsp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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

float readFloat (const juce::var& object, const juce::String& key, float fallback)
{
    return readFloat (object, key.toRawUTF8(), fallback);
}

bool readBool (const juce::var& object, const juce::String& key, bool fallback)
{
    return readFloat (object, key, fallback ? 1.0f : 0.0f) > 0.5f;
}

bool hasProperty (const juce::var& object, const juce::String& key)
{
    if (! object.isObject())
        return false;
    const auto value = object.getProperty (key, juce::var());
    return ! value.isVoid() && ! value.isUndefined();
}

float clampGain (float value) { return std::clamp (value, 0.0f, 4.0f); }

// ---- band id helpers ------------------------------------------------------
bool splitBandId (const juce::String& id, int& oneBased, juce::String& suffix)
{
    if (! id.startsWithIgnoreCase ("band"))
        return false;

    int i = 4;
    int number = 0;

    while (i < id.length() && juce::CharacterFunctions::isDigit (id[i]))
    {
        number = number * 10 + (id[i] - '0');
        ++i;
    }

    if (number < 1 || number > 8 || i >= id.length())
        return false;

    oneBased = number;
    suffix = id.substring (i);
    return true;
}

int legacyBandIndex (const juce::String& id, juce::String& suffix)
{
    const auto take = [&] (const char* prefix, int index)
    {
        if (id.startsWithIgnoreCase (prefix))
        {
            suffix = id.substring ((int) std::strlen (prefix));
            return index;
        }
        return -1;
    };

    if (const auto i = take ("low", 0); i >= 0) return i;
    if (const auto i = take ("lmf", 1); i >= 0) return i;
    if (const auto i = take ("hmf", 2); i >= 0) return i;
    if (const auto i = take ("high", 3); i >= 0) return i;

    // Master legacy names.
    if (const auto i = take ("mid", 1); i >= 0) return i;

    return -1;
}

void setEqBandParam (MixEqBandParams& band, const juce::String& suffix, float value)
{
    if (suffix.equalsIgnoreCase ("On")) band.on.store (value > 0.5f);
    else if (suffix.equalsIgnoreCase ("Type")) band.type.store (juce::jlimit (0, 7, (int) std::lround (value)));
    else if (suffix.equalsIgnoreCase ("Freq")) band.freq.store (std::clamp (value, 10.0f, 20000.0f));
    else if (suffix.equalsIgnoreCase ("Gain")) band.gainDb.store (std::clamp (value, -26.0f, 26.0f));
    else if (suffix.equalsIgnoreCase ("Q")) band.q.store (std::clamp (value, 0.05f, 24.0f));
    else if (suffix.equalsIgnoreCase ("Slope")) band.slope.store (juce::jlimit (0, 3, (int) std::lround (value)));
    else if (suffix.equalsIgnoreCase ("Channel") || suffix.equalsIgnoreCase ("Ch"))
        band.channel.store (juce::jlimit (0, 2, (int) std::lround (value)));
    else if (suffix.equalsIgnoreCase ("Dyn")) band.dynOn.store (value > 0.5f);
    else if (suffix.equalsIgnoreCase ("DynThresh")) band.dynThreshDb.store (std::clamp (value, -60.0f, 0.0f));
    else if (suffix.equalsIgnoreCase ("DynRange")) band.dynRangeDb.store (std::clamp (value, -24.0f, 24.0f));
    else if (suffix.equalsIgnoreCase ("DynAttack")) band.dynAttackMs.store (std::clamp (value, 0.1f, 200.0f));
    else if (suffix.equalsIgnoreCase ("DynRelease")) band.dynReleaseMs.store (std::clamp (value, 1.0f, 2000.0f));
}

float getEqBandParam (const MixEqBandParams& band, const juce::String& suffix)
{
    if (suffix.equalsIgnoreCase ("On")) return band.on.load() ? 1.0f : 0.0f;
    if (suffix.equalsIgnoreCase ("Type")) return (float) band.type.load();
    if (suffix.equalsIgnoreCase ("Freq")) return band.freq.load();
    if (suffix.equalsIgnoreCase ("Gain")) return band.gainDb.load();
    if (suffix.equalsIgnoreCase ("Q")) return band.q.load();
    if (suffix.equalsIgnoreCase ("Slope")) return (float) band.slope.load();
    if (suffix.equalsIgnoreCase ("Channel") || suffix.equalsIgnoreCase ("Ch")) return (float) band.channel.load();
    if (suffix.equalsIgnoreCase ("Dyn")) return band.dynOn.load() ? 1.0f : 0.0f;
    if (suffix.equalsIgnoreCase ("DynThresh")) return band.dynThreshDb.load();
    if (suffix.equalsIgnoreCase ("DynRange")) return band.dynRangeDb.load();
    if (suffix.equalsIgnoreCase ("DynAttack")) return band.dynAttackMs.load();
    if (suffix.equalsIgnoreCase ("DynRelease")) return band.dynReleaseMs.load();
    return 0.0f;
}

void publishBand (juce::DynamicObject& object, int index, const MixEqBandParams& band)
{
    const auto n = juce::String (index + 1);
    object.setProperty ("band" + n + "On", band.on.load());
    object.setProperty ("band" + n + "Type", band.type.load());
    object.setProperty ("band" + n + "Freq", (double) band.freq.load());
    object.setProperty ("band" + n + "Gain", (double) band.gainDb.load());
    object.setProperty ("band" + n + "Q", (double) band.q.load());
    object.setProperty ("band" + n + "Slope", band.slope.load());
    object.setProperty ("band" + n + "Channel", band.channel.load());
    object.setProperty ("band" + n + "Dyn", band.dynOn.load());
    object.setProperty ("band" + n + "DynThresh", (double) band.dynThreshDb.load());
    object.setProperty ("band" + n + "DynRange", (double) band.dynRangeDb.load());
    object.setProperty ("band" + n + "DynAttack", (double) band.dynAttackMs.load());
    object.setProperty ("band" + n + "DynRelease", (double) band.dynReleaseMs.load());
}

void restoreBand (const juce::var& state, int index, MixEqBandParams& band)
{
    const auto n = juce::String (index + 1);
    const auto id = [&] (const char* suffix) { return "band" + n + suffix; };

    band.on.store (readBool (state, id ("On"), band.on.load()));
    band.type.store (juce::jlimit (0, 7, (int) std::lround (readFloat (state, id ("Type"), (float) band.type.load()))));
    band.freq.store (std::clamp (readFloat (state, id ("Freq"), band.freq.load()), 10.0f, 20000.0f));
    band.gainDb.store (std::clamp (readFloat (state, id ("Gain"), band.gainDb.load()), -26.0f, 26.0f));
    band.q.store (std::clamp (readFloat (state, id ("Q"), band.q.load()), 0.05f, 24.0f));
    band.slope.store (juce::jlimit (0, 3, (int) std::lround (readFloat (state, id ("Slope"), (float) band.slope.load()))));
    band.channel.store (juce::jlimit (0, 2, (int) std::lround (readFloat (state, id ("Channel"), (float) band.channel.load()))));
    band.dynOn.store (readBool (state, id ("Dyn"), band.dynOn.load()));
    band.dynThreshDb.store (std::clamp (readFloat (state, id ("DynThresh"), band.dynThreshDb.load()), -60.0f, 0.0f));
    band.dynRangeDb.store (std::clamp (readFloat (state, id ("DynRange"), band.dynRangeDb.load()), -24.0f, 24.0f));
    band.dynAttackMs.store (std::clamp (readFloat (state, id ("DynAttack"), band.dynAttackMs.load()), 0.1f, 200.0f));
    band.dynReleaseMs.store (std::clamp (readFloat (state, id ("DynRelease"), band.dynReleaseMs.load()), 1.0f, 2000.0f));
}

void publishLegacyBand (juce::DynamicObject& object, const char* prefix,
                        const MixEqBandParams& band, bool publishQ)
{
    object.setProperty (juce::String (prefix) + "Freq", (double) band.freq.load());
    object.setProperty (juce::String (prefix) + "Gain", (double) band.gainDb.load());
    if (publishQ)
        object.setProperty (juce::String (prefix) + "Q", (double) band.q.load());
}

} // namespace

//==============================================================================
struct MixRackChannel::Impl
{
    mixrack::ProEq eq;
    mixrack::MixCompressor comp;
    mixrack::SaturnSat sat;

    mpm::SaturationStage micLeft { mpm::SaturationStage::Params { 0.0, true, 0, 80.0 } };
    mpm::SaturationStage micRight { mpm::SaturationStage::Params { 0.0, true, 0, 80.0 } };
    std::vector<double> micLeftScratch, micRightScratch;

    double sampleRate = 48000.0;
    int maxBlock = 0;
    bool prepared = false;

    std::uint32_t cachedRevision = 0xffffffffu;
    float outputGain = 1.0f, width = 1.0f, inputTrim = 1.0f;
    bool mono = false, eqOn = true, compOn = false, satOn = false, phaseInvert = false;

    void configure (const Params& p)
    {
        eq.setFilters (p.hpfOn.load(), p.hpfFreq.load(), p.hpfSlope.load(),
                       p.lpfOn.load(), p.lpfFreq.load(), p.lpfSlope.load());

        for (int i = 0; i < MixRackChannel::numEqBands; ++i)
        {
            const auto& b = p.bands[static_cast<size_t> (i)];
            mixrack::EqBandSettings s;
            s.on = b.on.load();
            s.type = b.type.load();
            s.freq = b.freq.load();
            s.gainDb = b.gainDb.load();
            s.q = b.q.load();
            s.slope = b.slope.load();
            s.channel = b.channel.load();
            s.dynOn = b.dynOn.load();
            s.dynThreshDb = b.dynThreshDb.load();
            s.dynRangeDb = b.dynRangeDb.load();
            s.dynAttackMs = p.eqDynAttackMs.load();
            s.dynReleaseMs = p.eqDynReleaseMs.load();
            eq.setBand (i, s);
        }

        comp.configure (p.compType.load(), p.compThreshDb.load(), p.compRatio.load(),
                        p.compAttackMs.load(), p.compReleaseMs.load(), p.compKneeDb.load(),
                        p.compMakeupDb.load(), p.compMix.load(), p.scHpf.load(),
                        p.compLookaheadMs.load(), p.compHoldMs.load(), p.compRangeDb.load(),
                        p.compAmount.load(), p.compAutoRelease.load(), p.compAutoGain.load(),
                        p.scEqFreq.load(), p.scEqGain.load(), p.scEqQ.load());
        comp.setColour (p.compColor.load(), p.compDriveDb.load(),
                        p.compOutputDb.load(), p.compFeedback.load());

        mixrack::SaturnSettings ss;
        ss.driveDb = p.satDriveDb.load();
        ss.mix = p.satMix.load();
        ss.style = p.satMode.load();
        ss.bias = p.satBias.load();
        ss.toneBass = p.satToneBass.load();
        ss.toneMid = p.satToneMid.load();
        ss.toneTreble = p.satToneTreble.load() + p.satTone.load() * 0.5f;
        ss.tonePresence = p.satTonePresence.load() + p.satTone.load() * 0.5f;
        ss.dynamics = p.satDynamics.load();
        ss.feedback = p.satFeedback.load();
        ss.feedbackHz = p.satFeedbackHz.load();
        ss.outputDb = p.satOutputDb.load();
        ss.hq = p.satOversample.load();
        sat.setSettings (ss);

        outputGain = dbToGain (p.outputDb.load());
        width = std::clamp (p.width.load(), 0.0f, 2.0f);
        inputTrim = dbToGain (p.inputTrimDb.load());
        phaseInvert = p.phaseInvert.load();
        mono = p.mono.load();
        eqOn = p.eqOn.load();
        compOn = p.compOn.load();
        satOn = p.satOn.load();
    }
};

MixRackChannel::MixRackChannel() : impl (std::make_unique<Impl>())
{
    initialiseDefaultBands();
}

MixRackChannel::~MixRackChannel() = default;

void MixRackChannel::initialiseDefaultBands()
{
    struct Default { bool on; int type; float freq, gain, q; };
    static constexpr Default defaults[numEqBands] {
        { true,  1, 100.0f,   0.0f, 0.707f },  // low shelf
        { true,  0, 700.0f,   0.0f, 1.0f   },  // low-mid bell
        { true,  0, 3000.0f,  0.0f, 1.0f   },  // high-mid bell
        { true,  2, 10000.0f, 0.0f, 0.707f },  // high shelf
        { false, 0, 200.0f,   0.0f, 1.0f   },
        { false, 0, 500.0f,   0.0f, 1.0f   },
        { false, 0, 5000.0f,  0.0f, 1.0f   },
        { false, 0, 12000.0f, 0.0f, 1.0f   },
    };

    for (int i = 0; i < numEqBands; ++i)
    {
        auto& b = params.bands[static_cast<size_t> (i)];
        b.on.store (defaults[i].on);
        b.type.store (defaults[i].type);
        b.freq.store (defaults[i].freq);
        b.gainDb.store (defaults[i].gain);
        b.q.store (defaults[i].q);
        b.slope.store (0);
        b.channel.store (0);
        b.dynOn.store (false);
        b.dynThreshDb.store (-20.0f);
        b.dynRangeDb.store (0.0f);
        b.dynAttackMs.store (10.0f);
        b.dynReleaseMs.store (120.0f);
    }
}

void MixRackChannel::prepare (double sampleRate, int maxBlockSize)
{
    auto& d = *impl;
    d.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    d.maxBlock = std::max (1, maxBlockSize);

    d.eq.prepare (d.sampleRate);
    d.comp.prepare (d.sampleRate);
    d.sat.prepare (d.sampleRate);

    const mpm::ProcessSpec spec { d.sampleRate, d.maxBlock };
    d.micLeft.prepare (spec);
    d.micRight.prepare (spec);
    d.micLeft.reset();
    d.micRight.reset();
    d.micLeftScratch.assign (static_cast<size_t> (d.maxBlock), 0.0);
    d.micRightScratch.assign (static_cast<size_t> (d.maxBlock), 0.0);

    d.cachedRevision = 0xffffffffu;
    d.prepared = true;
    d.configure (params);
    d.cachedRevision = params.revision.load (std::memory_order_relaxed);
}

void MixRackChannel::reset()
{
    auto& d = *impl;
    d.eq.reset();
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

    beginBlock();

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
        processSample (left[i], right[i]);
}

void MixRackChannel::beginBlock()
{
    auto& d = *impl;

    if (! d.prepared)
        return;

    const auto revision = params.revision.load (std::memory_order_relaxed);

    if (revision != d.cachedRevision)
    {
        d.configure (params);
        d.cachedRevision = revision;
    }
}

void MixRackChannel::processSample (float& left, float& right)
{
    auto& d = *impl;

    if (! d.prepared || ! params.stripOn.load())
        return;

    // The engine calls beginBlock() once per block; direct processSample()
    // callers (tests, offline helpers) still get a revision check here. With
    // beginBlock() in front this is one relaxed load, not a reconfigure.
    const auto revision = params.revision.load (std::memory_order_relaxed);

    if (revision != d.cachedRevision)
    {
        d.configure (params);
        d.cachedRevision = revision;
    }

    if (d.phaseInvert)
    {
        left = -left;
        right = -right;
    }

    left *= d.inputTrim;
    right *= d.inputTrim;

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

    if (d.eqOn)
        d.eq.process (left, right);

    if (d.compOn)
        d.comp.process (left, right);

    if (d.satOn)
        d.sat.process (left, right);

    // Utility width / mono / output, always last.
    if (d.mono)
    {
        const auto m = 0.5f * (left + right);
        left = right = m;
    }
    else if (std::abs (d.width - 1.0f) > 1.0e-4f)
    {
        const auto m = 0.5f * (left + right);
        const auto s = 0.5f * (left - right) * d.width;
        left = m + s;
        right = m - s;
    }

    left *= d.outputGain;
    right *= d.outputGain;
}

void MixRackChannel::setParam (const juce::String& id, float value)
{
    int bandNumber = 0;
    juce::String suffix;

    if (splitBandId (id, bandNumber, suffix))
    {
        setEqBandParam (params.bands[static_cast<size_t> (bandNumber - 1)], suffix, value);
    }
    else if (const auto legacy = legacyBandIndex (id, suffix); legacy >= 0)
    {
        // Legacy strip ids keep working: low -> band 1, lmf -> 2, hmf -> 3,
        // high -> 4. `lowGain` and friends are the names Mixology still emits.
        auto& band = params.bands[static_cast<size_t> (legacy)];
        if (suffix.equalsIgnoreCase ("Gain"))
            band.gainDb.store (std::clamp (value, -18.0f, 18.0f));
        else if (suffix.equalsIgnoreCase ("Freq"))
            band.freq.store (std::clamp (value, 20.0f, 20000.0f));
        else if (suffix.equalsIgnoreCase ("Q"))
            band.q.store (std::clamp (value, 0.1f, 18.0f));
        else
            return;

        // v2 strips had no per-band on switch; a legacy move re-enables the
        // band so Mixology's low/lmf/hmf/high output always lands.
        band.on.store (true);
    }
    else if (id == "stripOn") params.stripOn.store (value > 0.5f);
    else if (id == "vocal") params.vocal.store (value > 0.5f);
    else if (id == "inputTrim" || id == "trim") params.inputTrimDb.store (std::clamp (value, -24.0f, 24.0f));
    else if (id == "phase") params.phaseInvert.store (value > 0.5f);
    else if (id == "eqDynAttack" || id == "dynAttack") params.eqDynAttackMs.store (std::clamp (value, 0.1f, 200.0f));
    else if (id == "eqDynRelease" || id == "dynRelease") params.eqDynReleaseMs.store (std::clamp (value, 1.0f, 2000.0f));
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
    else if (id == "hpfFreq") params.hpfFreq.store (std::clamp (value, 10.0f, 1000.0f));
    else if (id == "hpfSlope") params.hpfSlope.store (juce::jlimit (0, 3, (int) std::lround (value)));
    else if (id == "lpfOn") params.lpfOn.store (value > 0.5f);
    else if (id == "lpfFreq") params.lpfFreq.store (std::clamp (value, 1000.0f, 20000.0f));
    else if (id == "lpfSlope") params.lpfSlope.store (juce::jlimit (0, 3, (int) std::lround (value)));
    else if (id == "compOn") params.compOn.store (value > 0.5f);
    else if (id == "compType") params.compType.store (juce::jlimit (0, 7, (int) std::lround (value)));
    else if (id == "compThresh") params.compThreshDb.store (std::clamp (value, -60.0f, 0.0f));
    else if (id == "compRatio") params.compRatio.store (std::clamp (value, 1.0f, 20.0f));
    else if (id == "compAttack") params.compAttackMs.store (std::clamp (value, 0.05f, 200.0f));
    else if (id == "compRelease") params.compReleaseMs.store (std::clamp (value, 10.0f, 2000.0f));
    else if (id == "compKnee") params.compKneeDb.store (std::clamp (value, 0.0f, 24.0f));
    else if (id == "compMakeup") params.compMakeupDb.store (std::clamp (value, -12.0f, 24.0f));
    else if (id == "compMix") params.compMix.store (std::clamp (value, 0.0f, 1.0f));
    else if (id == "compDrive") params.compDriveDb.store (std::clamp (value, -24.0f, 24.0f));
    else if (id == "compOutput") params.compOutputDb.store (std::clamp (value, -24.0f, 24.0f));
    else if (id == "compColor") params.compColor.store (std::clamp (value, 0.0f, 1.0f));
    else if (id == "compFeedback") params.compFeedback.store (std::clamp (value, 0.0f, 0.45f));
    else if (id == "compLookahead") params.compLookaheadMs.store (std::clamp (value, 0.0f, 20.0f));
    else if (id == "compHold") params.compHoldMs.store (std::clamp (value, 0.0f, 500.0f));
    else if (id == "compRange") params.compRangeDb.store (std::clamp (value, 0.0f, 60.0f));
    else if (id == "compAmount") params.compAmount.store (std::clamp (value, 0.0f, 2.0f));
    else if (id == "compAutoRelease") params.compAutoRelease.store (value > 0.5f);
    else if (id == "compAutoGain") params.compAutoGain.store (value > 0.5f);
    else if (id == "scHpf") params.scHpf.store (std::clamp (value, 1.0f, 1000.0f));
    else if (id == "scEqFreq") params.scEqFreq.store (std::clamp (value, 20.0f, 20000.0f));
    else if (id == "scEqGain") params.scEqGain.store (std::clamp (value, -18.0f, 18.0f));
    else if (id == "scEqQ") params.scEqQ.store (std::clamp (value, 0.1f, 8.0f));
    else if (id == "satOn") params.satOn.store (value > 0.5f);
    else if (id == "satMode") params.satMode.store (juce::jlimit (0, 7, (int) std::lround (value)));
    else if (id == "satDrive") params.satDriveDb.store (std::clamp (value, 0.0f, 36.0f));
    else if (id == "satMix") params.satMix.store (std::clamp (value, 0.0f, 1.0f));
    else if (id == "satBias") params.satBias.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satTone") params.satTone.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satToneBass") params.satToneBass.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satToneMid") params.satToneMid.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satToneTreble") params.satToneTreble.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satTonePresence") params.satTonePresence.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satDynamics") params.satDynamics.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satFeedback") params.satFeedback.store (std::clamp (value, 0.0f, 1.0f));
    else if (id == "satFeedbackHz") params.satFeedbackHz.store (std::clamp (value, 20.0f, 2000.0f));
    else if (id == "satHq" || id == "satOversample" || id == "satAdaa") params.satOversample.store (value > 0.5f);
    else if (id == "satOutput") params.satOutputDb.store (std::clamp (value, -24.0f, 24.0f));
    else if (id == "outputDb" || id == "output") params.outputDb.store (std::clamp (value, -60.0f, 24.0f));
    else if (id == "width") params.width.store (std::clamp (value, 0.0f, 2.0f));
    else if (id == "mono") params.mono.store (value > 0.5f);
    else return;

    bumpRevision();
}

float MixRackChannel::getParam (const juce::String& id) const
{
    int bandNumber = 0;
    juce::String suffix;

    if (splitBandId (id, bandNumber, suffix))
        return getEqBandParam (params.bands[static_cast<size_t> (bandNumber - 1)], suffix);

    if (const auto legacy = legacyBandIndex (id, suffix); legacy >= 0)
        return getEqBandParam (params.bands[static_cast<size_t> (legacy)], suffix);

    if (id == "stripOn") return params.stripOn.load() ? 1.0f : 0.0f;
    if (id == "vocal") return params.vocal.load() ? 1.0f : 0.0f;
    if (id == "inputTrim" || id == "trim") return params.inputTrimDb.load();
    if (id == "phase") return params.phaseInvert.load() ? 1.0f : 0.0f;
    if (id == "eqDynAttack" || id == "dynAttack") return params.eqDynAttackMs.load();
    if (id == "eqDynRelease" || id == "dynRelease") return params.eqDynReleaseMs.load();
    if (id == "openMicOn") return params.openMicOn.load() ? 1.0f : 0.0f;
    if (id == "openMicDrive") return params.openMicDriveDb.load();
    if (id == "openMicMix") return params.openMicMix.load();
    if (id == "openMicTrim") return params.openMicTrimDb.load();
    if (id == "eqOn") return params.eqOn.load() ? 1.0f : 0.0f;
    if (id == "hpfOn") return params.hpfOn.load() ? 1.0f : 0.0f;
    if (id == "hpfFreq") return params.hpfFreq.load();
    if (id == "hpfSlope") return (float) params.hpfSlope.load();
    if (id == "lpfOn") return params.lpfOn.load() ? 1.0f : 0.0f;
    if (id == "lpfFreq") return params.lpfFreq.load();
    if (id == "lpfSlope") return (float) params.lpfSlope.load();
    if (id == "compOn") return params.compOn.load() ? 1.0f : 0.0f;
    if (id == "compType") return (float) params.compType.load();
    if (id == "compThresh") return params.compThreshDb.load();
    if (id == "compRatio") return params.compRatio.load();
    if (id == "compAttack") return params.compAttackMs.load();
    if (id == "compRelease") return params.compReleaseMs.load();
    if (id == "compKnee") return params.compKneeDb.load();
    if (id == "compMakeup") return params.compMakeupDb.load();
    if (id == "compMix") return params.compMix.load();
    if (id == "compDrive") return params.compDriveDb.load();
    if (id == "compOutput") return params.compOutputDb.load();
    if (id == "compColor") return params.compColor.load();
    if (id == "compFeedback") return params.compFeedback.load();
    if (id == "compLookahead") return params.compLookaheadMs.load();
    if (id == "compHold") return params.compHoldMs.load();
    if (id == "compRange") return params.compRangeDb.load();
    if (id == "compAmount") return params.compAmount.load();
    if (id == "compAutoRelease") return params.compAutoRelease.load() ? 1.0f : 0.0f;
    if (id == "compAutoGain") return params.compAutoGain.load() ? 1.0f : 0.0f;
    if (id == "scHpf") return params.scHpf.load();
    if (id == "scEqFreq") return params.scEqFreq.load();
    if (id == "scEqGain") return params.scEqGain.load();
    if (id == "scEqQ") return params.scEqQ.load();
    if (id == "satOn") return params.satOn.load() ? 1.0f : 0.0f;
    if (id == "satMode") return (float) params.satMode.load();
    if (id == "satDrive") return params.satDriveDb.load();
    if (id == "satMix") return params.satMix.load();
    if (id == "satBias") return params.satBias.load();
    if (id == "satTone") return params.satTone.load();
    if (id == "satToneBass") return params.satToneBass.load();
    if (id == "satToneMid") return params.satToneMid.load();
    if (id == "satToneTreble") return params.satToneTreble.load();
    if (id == "satTonePresence") return params.satTonePresence.load();
    if (id == "satDynamics") return params.satDynamics.load();
    if (id == "satFeedback") return params.satFeedback.load();
    if (id == "satFeedbackHz") return params.satFeedbackHz.load();
    if (id == "satHq" || id == "satOversample" || id == "satAdaa") return params.satOversample.load() ? 1.0f : 0.0f;
    if (id == "satOutput") return params.satOutputDb.load();
    if (id == "outputDb" || id == "output") return params.outputDb.load();
    if (id == "width") return params.width.load();
    if (id == "mono") return params.mono.load() ? 1.0f : 0.0f;
    return 0.0f;
}

float MixRackChannel::bandDynamicGainDb (int band) const
{
    return impl->eq.dynamicGainDb (band);
}

float MixRackChannel::bandDebugScale (int band) const
{
    return impl->eq.bandDebugScale (band);
}

float MixRackChannel::bandDebugRefGain (int band) const
{
    return impl->eq.bandDebugRefGain (band);
}

double MixRackChannel::bandStaticDb (int band, double freq) const
{
    return impl->eq.bandStaticDb (band, freq);
}

double MixRackChannel::bandReferenceDb (int band, double freq) const
{
    return impl->eq.bandReferenceDb (band, freq);
}

double MixRackChannel::eqResponseDb (double freq) const
{
    return impl->eq.responseDb (freq);
}

juce::var MixRackChannel::toVar() const
{
    auto* object = new juce::DynamicObject();

    object->setProperty ("stripOn", params.stripOn.load());
    object->setProperty ("vocal", params.vocal.load());
    object->setProperty ("inputTrim", params.inputTrimDb.load());
    object->setProperty ("phase", params.phaseInvert.load());
    object->setProperty ("eqDynAttack", params.eqDynAttackMs.load());
    object->setProperty ("eqDynRelease", params.eqDynReleaseMs.load());
    object->setProperty ("openMicOn", params.openMicOn.load());
    object->setProperty ("openMicUserSet", params.openMicUserSet.load());
    object->setProperty ("openMicDrive", params.openMicDriveDb.load());
    object->setProperty ("openMicMix", params.openMicMix.load());
    object->setProperty ("openMicTrim", params.openMicTrimDb.load());
    object->setProperty ("eqOn", params.eqOn.load());
    object->setProperty ("hpfOn", params.hpfOn.load());
    object->setProperty ("hpfFreq", params.hpfFreq.load());
    object->setProperty ("hpfSlope", params.hpfSlope.load());
    object->setProperty ("lpfOn", params.lpfOn.load());
    object->setProperty ("lpfFreq", params.lpfFreq.load());
    object->setProperty ("lpfSlope", params.lpfSlope.load());

    for (int i = 0; i < numEqBands; ++i)
        publishBand (*object, i, params.bands[static_cast<size_t> (i)]);

    // Legacy aliases keep old sessions and Mixology's resolver output working.
    publishLegacyBand (*object, "low", params.bands[0], true);
    publishLegacyBand (*object, "lmf", params.bands[1], true);
    publishLegacyBand (*object, "hmf", params.bands[2], true);
    publishLegacyBand (*object, "high", params.bands[3], true);

    object->setProperty ("compOn", params.compOn.load());
    object->setProperty ("compType", params.compType.load());
    object->setProperty ("compThresh", params.compThreshDb.load());
    object->setProperty ("compRatio", params.compRatio.load());
    object->setProperty ("compAttack", params.compAttackMs.load());
    object->setProperty ("compRelease", params.compReleaseMs.load());
    object->setProperty ("compKnee", params.compKneeDb.load());
    object->setProperty ("compMakeup", params.compMakeupDb.load());
    object->setProperty ("compMix", params.compMix.load());
    object->setProperty ("compDrive", params.compDriveDb.load());
    object->setProperty ("compOutput", params.compOutputDb.load());
    object->setProperty ("compColor", params.compColor.load());
    object->setProperty ("compFeedback", params.compFeedback.load());
    object->setProperty ("compLookahead", params.compLookaheadMs.load());
    object->setProperty ("compHold", params.compHoldMs.load());
    object->setProperty ("compRange", params.compRangeDb.load());
    object->setProperty ("compAmount", params.compAmount.load());
    object->setProperty ("compAutoRelease", params.compAutoRelease.load());
    object->setProperty ("compAutoGain", params.compAutoGain.load());
    object->setProperty ("scHpf", params.scHpf.load());
    object->setProperty ("scEqFreq", params.scEqFreq.load());
    object->setProperty ("scEqGain", params.scEqGain.load());
    object->setProperty ("scEqQ", params.scEqQ.load());

    object->setProperty ("satOn", params.satOn.load());
    object->setProperty ("satMode", params.satMode.load());
    object->setProperty ("satDrive", params.satDriveDb.load());
    object->setProperty ("satMix", params.satMix.load());
    object->setProperty ("satBias", params.satBias.load());
    object->setProperty ("satTone", params.satTone.load());
    object->setProperty ("satToneBass", params.satToneBass.load());
    object->setProperty ("satToneMid", params.satToneMid.load());
    object->setProperty ("satToneTreble", params.satToneTreble.load());
    object->setProperty ("satTonePresence", params.satTonePresence.load());
    object->setProperty ("satDynamics", params.satDynamics.load());
    object->setProperty ("satFeedback", params.satFeedback.load());
    object->setProperty ("satFeedbackHz", params.satFeedbackHz.load());
    object->setProperty ("satOversample", params.satOversample.load());
    object->setProperty ("satOutput", params.satOutputDb.load());

    object->setProperty ("outputDb", params.outputDb.load());
    object->setProperty ("width", params.width.load());
    object->setProperty ("mono", params.mono.load());

    return juce::var (object);
}

void MixRackChannel::fromVar (const juce::var& state)
{
    if (! state.isObject())
        return;

    params.stripOn.store (readBool (state, "stripOn", params.stripOn.load()));
    params.vocal.store (readBool (state, "vocal", params.vocal.load()));
    params.inputTrimDb.store (readFloat (state, "inputTrim", params.inputTrimDb.load()));
    params.phaseInvert.store (readBool (state, "phase", params.phaseInvert.load()));
    params.eqDynAttackMs.store (readFloat (state, "eqDynAttack", params.eqDynAttackMs.load()));
    params.eqDynReleaseMs.store (readFloat (state, "eqDynRelease", params.eqDynReleaseMs.load()));
    params.openMicOn.store (readBool (state, "openMicOn", params.openMicOn.load()));
    params.openMicUserSet.store (readBool (state, "openMicUserSet", false));
    params.openMicDriveDb.store (readFloat (state, "openMicDrive", params.openMicDriveDb.load()));
    params.openMicMix.store (readFloat (state, "openMicMix", params.openMicMix.load()));
    params.openMicTrimDb.store (readFloat (state, "openMicTrim", params.openMicTrimDb.load()));
    params.eqOn.store (readBool (state, "eqOn", params.eqOn.load()));
    params.hpfOn.store (readBool (state, "hpfOn", params.hpfOn.load()));
    params.hpfFreq.store (readFloat (state, "hpfFreq", params.hpfFreq.load()));
    params.hpfSlope.store (juce::jlimit (0, 3, (int) std::lround (readFloat (state, "hpfSlope", (float) params.hpfSlope.load()))));
    params.lpfOn.store (readBool (state, "lpfOn", params.lpfOn.load()));
    params.lpfFreq.store (readFloat (state, "lpfFreq", params.lpfFreq.load()));
    params.lpfSlope.store (juce::jlimit (0, 3, (int) std::lround (readFloat (state, "lpfSlope", (float) params.lpfSlope.load()))));

    for (int i = 0; i < numEqBands; ++i)
        restoreBand (state, i, params.bands[static_cast<size_t> (i)]);

    // A session written by v2 has no bandN keys; adopt its four legacy
    // bands instead of resetting it to the new defaults.
    const auto readLegacy = [&] (const char* prefix, int index)
    {
        auto& band = params.bands[static_cast<size_t> (index)];
        if (hasProperty (state, juce::String ("band") + juce::String (index + 1) + "Freq"))
            return;
        band.freq.store (readFloat (state, juce::String (prefix) + "Freq", band.freq.load()));
        band.gainDb.store (readFloat (state, juce::String (prefix) + "Gain", band.gainDb.load()));
        band.q.store (readFloat (state, juce::String (prefix) + "Q", band.q.load()));
        band.on.store (true);
    };
    readLegacy ("low", 0);
    readLegacy ("lmf", 1);
    readLegacy ("hmf", 2);
    readLegacy ("high", 3);

    params.compOn.store (readBool (state, "compOn", params.compOn.load()));
    params.compType.store (juce::jlimit (0, 7, (int) std::lround (readFloat (state, "compType", (float) params.compType.load()))));
    params.compThreshDb.store (readFloat (state, "compThresh", params.compThreshDb.load()));
    params.compRatio.store (readFloat (state, "compRatio", params.compRatio.load()));
    params.compAttackMs.store (readFloat (state, "compAttack", params.compAttackMs.load()));
    params.compReleaseMs.store (readFloat (state, "compRelease", params.compReleaseMs.load()));
    params.compKneeDb.store (readFloat (state, "compKnee", params.compKneeDb.load()));
    params.compMakeupDb.store (readFloat (state, "compMakeup", params.compMakeupDb.load()));
    params.compMix.store (readFloat (state, "compMix", params.compMix.load()));
    params.compDriveDb.store (readFloat (state, "compDrive", params.compDriveDb.load()));
    params.compOutputDb.store (readFloat (state, "compOutput", params.compOutputDb.load()));
    params.compColor.store (readFloat (state, "compColor", params.compColor.load()));
    params.compFeedback.store (readFloat (state, "compFeedback", params.compFeedback.load()));
    params.compLookaheadMs.store (readFloat (state, "compLookahead", params.compLookaheadMs.load()));
    params.compHoldMs.store (readFloat (state, "compHold", params.compHoldMs.load()));
    params.compRangeDb.store (readFloat (state, "compRange", params.compRangeDb.load()));
    params.compAmount.store (readFloat (state, "compAmount", params.compAmount.load()));
    params.compAutoRelease.store (readBool (state, "compAutoRelease", params.compAutoRelease.load()));
    params.compAutoGain.store (readBool (state, "compAutoGain", params.compAutoGain.load()));
    params.scHpf.store (readFloat (state, "scHpf", params.scHpf.load()));
    params.scEqFreq.store (readFloat (state, "scEqFreq", params.scEqFreq.load()));
    params.scEqGain.store (readFloat (state, "scEqGain", params.scEqGain.load()));
    params.scEqQ.store (readFloat (state, "scEqQ", params.scEqQ.load()));
    params.satOn.store (readBool (state, "satOn", params.satOn.load()));
    params.satMode.store (juce::jlimit (0, 7, (int) std::lround (readFloat (state, "satMode", (float) params.satMode.load()))));
    params.satDriveDb.store (readFloat (state, "satDrive", params.satDriveDb.load()));
    params.satMix.store (readFloat (state, "satMix", params.satMix.load()));
    params.satBias.store (readFloat (state, "satBias", params.satBias.load()));
    params.satTone.store (readFloat (state, "satTone", params.satTone.load()));
    params.satToneBass.store (readFloat (state, "satToneBass", params.satToneBass.load()));
    params.satToneMid.store (readFloat (state, "satToneMid", params.satToneMid.load()));
    params.satToneTreble.store (readFloat (state, "satToneTreble", params.satToneTreble.load()));
    params.satTonePresence.store (readFloat (state, "satTonePresence", params.satTonePresence.load()));
    params.satDynamics.store (readFloat (state, "satDynamics", params.satDynamics.load()));
    params.satFeedback.store (readFloat (state, "satFeedback", params.satFeedback.load()));
    params.satFeedbackHz.store (readFloat (state, "satFeedbackHz", params.satFeedbackHz.load()));
    params.satOversample.store (readBool (state, "satOversample", params.satOversample.load()));
    params.satOutputDb.store (readFloat (state, "satOutput", params.satOutputDb.load()));
    params.outputDb.store (readFloat (state, "outputDb", params.outputDb.load()));
    params.width.store (readFloat (state, "width", params.width.load()));
    params.mono.store (readBool (state, "mono", params.mono.load()));

    bumpRevision();
}

//==============================================================================
struct MasterRack::Impl
{
    mixrack::ProEq eq;
    mixrack::ProEq deq;
    mixrack::SaturnSat sat;
    mixrack::MultibandCompressor dyn;
    mixrack::MultibandSaturn exc;
    overlay::master::dsp::ImagerModule img;
    fx::LookaheadLimiter limiter;
    fx::LoudnessMeter loudness;

    double sampleRate = 48000.0;
    int maxBlock = 0;
    bool prepared = false;
    float autoGainDb = 0.0f;
    std::uint32_t cachedRevision = 0xffffffffu;
    bool eqOn = true, deqOn = false, satOn = false, dynOn = false, excOn = false, imgOn = false, maxOn = true;

    float lufs = -70.0f;

    void configure (const Params& p)
    {
        eq.setFilters (p.hpfOn.load(), p.hpfFreq.load(), p.hpfSlope.load(),
                       p.lpfOn.load(), p.lpfFreq.load(), p.lpfSlope.load());

        for (int i = 0; i < MasterRack::numEqBands; ++i)
        {
            const auto& b = p.bands[static_cast<size_t> (i)];
            mixrack::EqBandSettings s;
            s.on = b.on.load();
            s.type = b.type.load();
            s.freq = b.freq.load();
            s.gainDb = b.gainDb.load();
            s.q = b.q.load();
            s.slope = b.slope.load();
            s.channel = b.channel.load();
            s.dynOn = b.dynOn.load();
            s.dynThreshDb = b.dynThreshDb.load();
            s.dynRangeDb = b.dynRangeDb.load();
            s.dynAttackMs = b.dynAttackMs.load();
            s.dynReleaseMs = b.dynReleaseMs.load();
            eq.setBand (i, s);
        }

        for (int i = 0; i < 4; ++i)
        {
            const auto& b = p.deqBands[static_cast<size_t> (i)];
            mixrack::EqBandSettings s;
            s.on = p.deqOn.load() && b.on.load();
            s.type = b.type.load();
            s.freq = b.freq.load();
            s.gainDb = 0.0f;
            s.q = b.q.load();
            s.slope = 1;
            s.channel = b.channel.load();
            s.dynOn = s.on && std::abs (b.dynRangeDb.load()) >= 0.01f;
            s.dynThreshDb = b.dynThreshDb.load();
            s.dynRangeDb = b.dynRangeDb.load();
            s.dynAttackMs = b.dynAttackMs.load();
            s.dynReleaseMs = b.dynReleaseMs.load();
            deq.setBand (i, s);
        }

        mixrack::SaturnSettings ss;
        ss.driveDb = p.satDriveDb.load();
        ss.mix = p.satMix.load();
        ss.style = p.satMode.load();
        ss.bias = p.satBias.load();
        ss.toneBass = p.satToneBass.load();
        ss.toneMid = p.satToneMid.load();
        ss.toneTreble = p.satToneTreble.load() + p.satTone.load() * 0.5f;
        ss.tonePresence = p.satTonePresence.load() + p.satTone.load() * 0.5f;
        ss.dynamics = p.satDynamics.load();
        ss.feedback = p.satFeedback.load();
        ss.feedbackHz = p.satFeedbackHz.load();
        ss.outputDb = p.satOutputDb.load();
        ss.hq = p.satOversample.load();
        sat.setSettings (ss);

        std::array<mixrack::MultibandCompressor::Band, MasterRack::numDynBands> dynBands;

        for (int k = 0; k < MasterRack::numDynBands; ++k)
        {
            const auto& b = p.dynBands[static_cast<size_t> (k)];
            dynBands[static_cast<size_t> (k)] = {
                b.on.load(), b.threshDb.load(), b.ratio.load(),
                b.attackMs.load(), b.releaseMs.load(), b.gainDb.load()
            };
        }

        (void) p.dynRms; // the typed compressor derives RMS/peak from its character
        dyn.configure (p.dynBandCount.load(), p.dynX1.load(), p.dynX2.load(), p.dynX3.load(),
                       p.dynType.load(), p.dynKneeDb.load(), p.dynMix.load(), dynBands);

        std::array<mixrack::MultibandSaturn::Band, MasterRack::numExcBands> excBands;

        for (int k = 0; k < MasterRack::numExcBands; ++k)
        {
            const auto& b = p.excBands[static_cast<size_t> (k)];
            excBands[static_cast<size_t> (k)] = {
                b.driveDb.load(), b.mix.load(), b.mode.load(), b.dynamics.load(),
                b.toneBass.load(), b.toneMid.load(), b.toneTreble.load(),
                b.tonePresence.load(), b.feedback.load(), b.levelDb.load()
            };
        }

        exc.configure (p.excBandCount.load(), p.excX1.load(), p.excX2.load(), p.excX3.load(), excBands);

        std::array<float, MasterRack::numImgBands> widths {};
        for (int k = 0; k < MasterRack::numImgBands; ++k)
            widths[static_cast<size_t> (k)] = p.imgWidth[static_cast<size_t> (k)].load();

        img.setParams (p.imgBandCount.load(), p.imgX1.load(), p.imgX2.load(), p.imgX3.load(),
                       widths, p.imgMonoBelow.load());

        eqOn = p.eqOn.load();
        deqOn = p.deqOn.load();
        satOn = p.satOn.load();
        dynOn = p.dynOn.load();
        excOn = p.excOn.load();
        imgOn = p.imgOn.load();
        maxOn = p.maxOn.load();
    }
};

MasterRack::MasterRack() : impl (std::make_unique<Impl>())
{
    initialiseDefaultBands();
}

MasterRack::~MasterRack() = default;

void MasterRack::initialiseDefaultBands()
{
    struct Default { bool on; int type; float freq, gain, q; };
    static constexpr Default defaults[numEqBands] {
        { false, 3, 30.0f,    0.0f, 0.707f },  // low cut (off until asked for)
        { true,  1, 90.0f,    0.0f, 0.707f },  // low shelf
        { true,  0, 250.0f,   0.0f, 1.0f   },
        { true,  0, 700.0f,   0.0f, 1.0f   },
        { true,  0, 2000.0f,  0.0f, 1.0f   },
        { true,  0, 5000.0f,  0.0f, 1.0f   },
        { true,  2, 10000.0f, 0.0f, 0.707f },
        { false, 4, 19000.0f, 0.0f, 0.707f },
    };

    for (int i = 0; i < numEqBands; ++i)
    {
        auto& b = params.bands[static_cast<size_t> (i)];
        b.on.store (defaults[i].on);
        b.type.store (defaults[i].type);
        b.freq.store (defaults[i].freq);
        b.gainDb.store (defaults[i].gain);
        b.q.store (defaults[i].q);
        b.slope.store (0);
        b.channel.store (0);
        b.dynOn.store (false);
        b.dynThreshDb.store (-20.0f);
        b.dynRangeDb.store (0.0f);
        b.dynAttackMs.store (10.0f);
        b.dynReleaseMs.store (120.0f);
    }

    static constexpr float deqFreqs[4] { 150.0f, 600.0f, 3000.0f, 8000.0f };

    for (int k = 0; k < 4; ++k)
    {
        auto& b = params.deqBands[static_cast<size_t> (k)];
        b.on.store (k == 2);
        b.type.store (0);
        b.freq.store (deqFreqs[k]);
        b.gainDb.store (0.0f);
        b.q.store (1.0f);
        b.slope.store (1);
        b.channel.store (0);
        b.dynOn.store (true);
        b.dynThreshDb.store (-20.0f);
        b.dynRangeDb.store (k == 2 ? -4.0f : 0.0f);
        b.dynAttackMs.store (10.0f);
        b.dynReleaseMs.store (120.0f);
    }

    for (int k = 0; k < numDynBands; ++k)
    {
        params.dynBands[static_cast<size_t> (k)].on.store (true);
        params.dynBands[static_cast<size_t> (k)].threshDb.store (0.0f);
        params.dynBands[static_cast<size_t> (k)].ratio.store (2.0f);
        params.dynBands[static_cast<size_t> (k)].attackMs.store (20.0f);
        params.dynBands[static_cast<size_t> (k)].releaseMs.store (150.0f);
        params.dynBands[static_cast<size_t> (k)].gainDb.store (0.0f);
        params.imgWidth[static_cast<size_t> (k)].store (1.0f);
    }

    for (int k = 0; k < numExcBands; ++k)
    {
        params.excBands[static_cast<size_t> (k)].driveDb.store (6.0f);
        params.excBands[static_cast<size_t> (k)].mix.store (0.0f);
        params.excBands[static_cast<size_t> (k)].mode.store (0);
    }
}

void MasterRack::prepare (double sampleRate, int maxBlockSize)
{
    auto& d = *impl;
    d.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    d.maxBlock = std::max (1, maxBlockSize);

    d.eq.prepare (d.sampleRate);
    d.deq.prepare (d.sampleRate);
    d.sat.prepare (d.sampleRate);
    d.dyn.prepare (d.sampleRate);
    d.exc.prepare (d.sampleRate);
    d.img.prepare (d.sampleRate);
    d.limiter.prepare (d.sampleRate);
    d.loudness.prepare (d.sampleRate, 2);
    d.autoGainDb = 0.0f;
    d.cachedRevision = 0xffffffffu;
    d.prepared = true;
    d.configure (params);
    d.cachedRevision = params.revision.load (std::memory_order_relaxed);
}

void MasterRack::reset()
{
    auto& d = *impl;
    d.eq.reset();
    d.deq.reset();
    d.sat.reset();
    d.dyn.reset();
    d.exc.reset();
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

    const auto revision = params.revision.load (std::memory_order_relaxed);
    if (revision != d.cachedRevision)
    {
        d.configure (params);
        d.cachedRevision = revision;
    }

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

    if (d.eqOn)
    {
        for (int i = 0; i < numSamples; ++i)
            d.eq.process (left[i], right[i]);
    }

    if (d.deqOn)
    {
        for (int i = 0; i < numSamples; ++i)
            d.deq.process (left[i], right[i]);
    }

    if (d.satOn)
    {
        for (int i = 0; i < numSamples; ++i)
            d.sat.process (left[i], right[i]);
    }

    if (d.dynOn)
        d.dyn.process (left, right, numSamples);

    if (d.excOn)
        d.exc.process (left, right, numSamples);

    if (d.imgOn)
        d.img.process (left, right, numSamples);

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

    if (d.maxOn)
    {
        fx::LookaheadLimiter::Params limiterParams;
        limiterParams.thresholdDb = params.maxThresholdDb.load();
        limiterParams.ceilingDb = params.ceilingDb.load();
        limiterParams.releaseMs = params.maxReleaseMs.load();
        limiterParams.link = std::clamp (params.maxLink.load(), 0.0f, 1.0f);
        limiterParams.transient = std::clamp (params.maxTransient.load(), 0.0f, 1.0f);

        switch (params.maxMode.load())
        {
            case 1:  limiterParams.mode = fx::LookaheadLimiter::Mode::smooth; break;
            case 2:  limiterParams.mode = fx::LookaheadLimiter::Mode::punch;  break;
            default: limiterParams.mode = fx::LookaheadLimiter::Mode::clean;  break;
        }

        d.limiter.setParams (limiterParams);
        d.limiter.setDetectFactor (2 << juce::jlimit (0, 3, params.maxOversample.load()));
        d.limiter.process (left, right, numSamples);
    }
}

void MasterRack::setParam (const juce::String& id, float value)
{
    int bandNumber = 0;
    juce::String suffix;

    if (splitBandId (id, bandNumber, suffix))
    {
        setEqBandParam (params.bands[static_cast<size_t> (bandNumber - 1)], suffix, value);
    }
    else if (const auto legacy = legacyBandIndex (id, suffix); legacy >= 0)
    {
        auto& band = params.bands[static_cast<size_t> (legacy)];
        if (suffix.equalsIgnoreCase ("Gain"))
            band.gainDb.store (std::clamp (value, -12.0f, 12.0f));
        else if (suffix.equalsIgnoreCase ("Freq"))
            band.freq.store (std::clamp (value, 20.0f, 20000.0f));
        else if (suffix.equalsIgnoreCase ("Q"))
            band.q.store (std::clamp (value, 0.1f, 18.0f));
        else
            return;

        // The v2 master applied low/mid/high unconditionally. A legacy write
        // therefore also makes the band audible on the new 8-band surface.
        band.on.store (true);
    }
    else if (id == "eqOn") params.eqOn.store (value > 0.5f);
    else if (id == "deqOn") params.deqOn.store (value > 0.5f);
    else if (id == "hpfOn") params.hpfOn.store (value > 0.5f);
    else if (id == "hpfFreq") params.hpfFreq.store (std::clamp (value, 10.0f, 500.0f));
    else if (id == "hpfSlope") params.hpfSlope.store (juce::jlimit (0, 3, (int) std::lround (value)));
    else if (id == "lpfOn") params.lpfOn.store (value > 0.5f);
    else if (id == "lpfFreq") params.lpfFreq.store (std::clamp (value, 1000.0f, 20000.0f));
    else if (id == "lpfSlope") params.lpfSlope.store (juce::jlimit (0, 3, (int) std::lround (value)));
    else if (id == "satOn") params.satOn.store (value > 0.5f);
    else if (id == "satMode") params.satMode.store (juce::jlimit (0, 7, (int) std::lround (value)));
    else if (id == "satDrive") params.satDriveDb.store (std::clamp (value, 0.0f, 36.0f));
    else if (id == "satMix") params.satMix.store (std::clamp (value, 0.0f, 1.0f));
    else if (id == "satBias") params.satBias.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satTone") params.satTone.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satToneBass") params.satToneBass.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satToneMid") params.satToneMid.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satToneTreble") params.satToneTreble.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satTonePresence") params.satTonePresence.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satDynamics") params.satDynamics.store (std::clamp (value, -1.0f, 1.0f));
    else if (id == "satFeedback") params.satFeedback.store (std::clamp (value, 0.0f, 1.0f));
    else if (id == "satFeedbackHz") params.satFeedbackHz.store (std::clamp (value, 20.0f, 2000.0f));
    else if (id == "satHq" || id == "satOversample" || id == "satAdaa") params.satOversample.store (value > 0.5f);
    else if (id == "satOutput") params.satOutputDb.store (std::clamp (value, -24.0f, 24.0f));
    else if (id.startsWithIgnoreCase ("deq") && id.length() > 4
             && juce::CharacterFunctions::isDigit (id[3]))
    {
        const auto band = juce::jlimit (1, 4, id.substring (3, 4).getIntValue()) - 1;
        const auto field = id.substring (4);
        auto& b = params.deqBands[static_cast<size_t> (band)];

        if (field == "On") b.on.store (value > 0.5f);
        else if (field == "Type") b.type.store (juce::jlimit (0, 2, (int) std::lround (value)));
        else if (field == "Freq") b.freq.store (std::clamp (value, 20.0f, 20000.0f));
        else if (field == "Q") b.q.store (std::clamp (value, 0.1f, 12.0f));
        else if (field == "Thresh") b.dynThreshDb.store (std::clamp (value, -60.0f, 0.0f));
        else if (field == "Range") b.dynRangeDb.store (std::clamp (value, -18.0f, 18.0f));
        else if (field == "Atk") b.dynAttackMs.store (std::clamp (value, 0.5f, 200.0f));
        else if (field == "Rel") b.dynReleaseMs.store (std::clamp (value, 10.0f, 2000.0f));
        else if (field == "Ch") b.channel.store (juce::jlimit (0, 2, (int) std::lround (value)));
        else return;
    }
    else if (id == "dynOn" || id == "compOn") params.dynOn.store (value > 0.5f);
    else if (id == "dynThresh" || id == "compThresh") params.dynBands[0].threshDb.store (std::clamp (value, -60.0f, 0.0f));
    else if (id == "dynRatio" || id == "compRatio") params.dynBands[0].ratio.store (std::clamp (value, 1.0f, 20.0f));
    else if (id == "dynType") params.dynType.store (juce::jlimit (0, 7, (int) std::lround (value)));
    else if (id == "dynBands") params.dynBandCount.store (juce::jlimit (1, 4, (int) std::lround (value) + 1));
    else if (id == "dynX1") params.dynX1.store (std::clamp (value, 40.0f, 600.0f));
    else if (id == "dynX2") params.dynX2.store (std::clamp (value, 300.0f, 6000.0f));
    else if (id == "dynX3") params.dynX3.store (std::clamp (value, 2000.0f, 16000.0f));
    else if (id == "dynKnee" || id == "compKnee") params.dynKneeDb.store (std::clamp (value, 0.0f, 24.0f));
    else if (id == "dynDetect") params.dynRms.store (value > 0.5f);
    else if (id == "dynMix") params.dynMix.store (std::clamp (value, 0.0f, 1.0f));
    else if (id == "excOn") params.excOn.store (value > 0.5f);
    else if (id == "excBands") params.excBandCount.store (juce::jlimit (1, 4, (int) std::lround (value) + 1));
    else if (id == "excX1") params.excX1.store (std::clamp (value, 40.0f, 600.0f));
    else if (id == "excX2") params.excX2.store (std::clamp (value, 300.0f, 6000.0f));
    else if (id == "excX3") params.excX3.store (std::clamp (value, 2000.0f, 16000.0f));
    else if (id == "imgOn") params.imgOn.store (value > 0.5f);
    else if (id == "imgBands") params.imgBandCount.store (juce::jlimit (1, 4, (int) std::lround (value) + 1));
    else if (id == "imgX1") params.imgX1.store (std::clamp (value, 40.0f, 600.0f));
    else if (id == "imgX2") params.imgX2.store (std::clamp (value, 300.0f, 6000.0f));
    else if (id == "imgX3") params.imgX3.store (std::clamp (value, 2000.0f, 16000.0f));
    else if (id == "imgMono") params.imgMonoBelow.store (std::clamp (value, 0.0f, 300.0f));
    else if (id == "width") params.imgWidth[0].store (std::clamp (value, 0.0f, 2.0f));
    else if (id == "maxOn") params.maxOn.store (value > 0.5f);
    else if (id == "maxThresh") params.maxThresholdDb.store (std::clamp (value, -24.0f, 0.0f));
    else if (id == "ceiling" || id == "maxCeiling") params.ceilingDb.store (std::clamp (value, -6.0f, 0.0f));
    else if (id == "maxRelease") params.maxReleaseMs.store (std::clamp (value, 10.0f, 1000.0f));
    else if (id == "maxMode") params.maxMode.store (juce::jlimit (0, 2, (int) std::lround (value)));
    else if (id == "maxOversample") params.maxOversample.store (juce::jlimit (0, 3, (int) std::lround (value)));
    else if (id == "maxTransient") params.maxTransient.store (std::clamp (value, 0.0f, 1.0f));
    else if (id == "maxLink") params.maxLink.store (std::clamp (value, 0.0f, 1.0f));
    else if (id == "automixOn") params.automixOn.store (value > 0.5f);
    else if (id == "targetLufs") params.targetLufs.store (std::clamp (value, -30.0f, -5.0f));
    else if (id == "automixProfile")
    {
        static constexpr float targets[] { -14.0f, -9.0f, -12.0f, -10.0f, -16.0f, -23.0f };
        const auto profile = juce::jlimit (0, 5, static_cast<int> (std::round (value)));
        params.automixProfile.store (profile);
        params.targetLufs.store (targets[profile]);
    }
    else if (id == "compare" || id == "bypass") params.compare.store (value > 0.5f);
    else if (id == "trimDb" || id == "outputTrim") params.trimDb.store (std::clamp (value, -24.0f, 12.0f));
    else if (id.startsWithIgnoreCase ("dyn") && id.length() > 3
             && juce::CharacterFunctions::isDigit (id[3]))
    {
        const auto band = juce::jlimit (1, 4, id.substring (3, 4).getIntValue()) - 1;
        const auto field = id.substring (4);
        auto& b = params.dynBands[static_cast<size_t> (band)];

        if (field == "On") b.on.store (value > 0.5f);
        else if (field == "Thresh") b.threshDb.store (std::clamp (value, -60.0f, 0.0f));
        else if (field == "Ratio") b.ratio.store (std::clamp (value, 1.0f, 20.0f));
        else if (field == "Atk") b.attackMs.store (std::clamp (value, 0.1f, 200.0f));
        else if (field == "Rel") b.releaseMs.store (std::clamp (value, 10.0f, 2000.0f));
        else if (field == "Gain") b.gainDb.store (std::clamp (value, -12.0f, 12.0f));
        else return;
    }
    else if (id.startsWithIgnoreCase ("exc") && id.length() > 3
             && juce::CharacterFunctions::isDigit (id[3]))
    {
        const auto band = juce::jlimit (1, 4, id.substring (3, 4).getIntValue()) - 1;
        const auto field = id.substring (4);
        auto& b = params.excBands[static_cast<size_t> (band)];

        if (field == "Drive") b.driveDb.store (std::clamp (value, 0.0f, 24.0f));
        else if (field == "Mix") b.mix.store (std::clamp (value, 0.0f, 1.0f));
        else if (field == "Mode") b.mode.store (juce::jlimit (0, 7, (int) std::lround (value)));
        else if (field == "Dyn") b.dynamics.store (std::clamp (value, -1.0f, 1.0f));
        else if (field == "ToneBass") b.toneBass.store (std::clamp (value, -1.0f, 1.0f));
        else if (field == "ToneMid") b.toneMid.store (std::clamp (value, -1.0f, 1.0f));
        else if (field == "ToneTreble") b.toneTreble.store (std::clamp (value, -1.0f, 1.0f));
        else if (field == "TonePresence") b.tonePresence.store (std::clamp (value, -1.0f, 1.0f));
        else if (field == "Feedback") b.feedback.store (std::clamp (value, 0.0f, 1.0f));
        else if (field == "Level") b.levelDb.store (std::clamp (value, -24.0f, 24.0f));
        else return;
    }
    else if (id.startsWithIgnoreCase ("img") && id.length() > 3
             && juce::CharacterFunctions::isDigit (id[3]))
    {
        const auto band = juce::jlimit (1, 4, id.substring (3, 4).getIntValue()) - 1;
        if (id.substring (4) == "Width")
            params.imgWidth[static_cast<size_t> (band)].store (std::clamp (value, 0.0f, 2.0f));
        else
            return;
    }
    else
    {
        return;
    }

    bumpRevision();
}

float MasterRack::getParam (const juce::String& id) const
{
    int bandNumber = 0;
    juce::String suffix;

    if (splitBandId (id, bandNumber, suffix))
        return getEqBandParam (params.bands[static_cast<size_t> (bandNumber - 1)], suffix);

    if (const auto legacy = legacyBandIndex (id, suffix); legacy >= 0)
        return getEqBandParam (params.bands[static_cast<size_t> (legacy)], suffix);

    if (id == "eqOn") return params.eqOn.load() ? 1.0f : 0.0f;
    if (id == "deqOn") return params.deqOn.load() ? 1.0f : 0.0f;
    if (id == "hpfOn") return params.hpfOn.load() ? 1.0f : 0.0f;
    if (id == "hpfFreq") return params.hpfFreq.load();
    if (id == "hpfSlope") return (float) params.hpfSlope.load();
    if (id == "lpfOn") return params.lpfOn.load() ? 1.0f : 0.0f;
    if (id == "lpfFreq") return params.lpfFreq.load();
    if (id == "lpfSlope") return (float) params.lpfSlope.load();
    if (id == "satOn") return params.satOn.load() ? 1.0f : 0.0f;
    if (id == "satMode") return (float) params.satMode.load();
    if (id == "satDrive") return params.satDriveDb.load();
    if (id == "satMix") return params.satMix.load();
    if (id == "satBias") return params.satBias.load();
    if (id == "satTone") return params.satTone.load();
    if (id == "satToneBass") return params.satToneBass.load();
    if (id == "satToneMid") return params.satToneMid.load();
    if (id == "satToneTreble") return params.satToneTreble.load();
    if (id == "satTonePresence") return params.satTonePresence.load();
    if (id == "satDynamics") return params.satDynamics.load();
    if (id == "satFeedback") return params.satFeedback.load();
    if (id == "satFeedbackHz") return params.satFeedbackHz.load();
    if (id == "satHq" || id == "satOversample") return params.satOversample.load() ? 1.0f : 0.0f;
    if (id == "satOutput") return params.satOutputDb.load();
    if (id.startsWithIgnoreCase ("deq") && id.length() > 4
        && juce::CharacterFunctions::isDigit (id[3]))
    {
        const auto band = juce::jlimit (1, 4, id.substring (3, 4).getIntValue()) - 1;
        const auto field = id.substring (4);
        const auto& b = params.deqBands[static_cast<size_t> (band)];
        if (field == "On") return b.on.load() ? 1.0f : 0.0f;
        if (field == "Type") return (float) b.type.load();
        if (field == "Freq") return b.freq.load();
        if (field == "Q") return b.q.load();
        if (field == "Thresh") return b.dynThreshDb.load();
        if (field == "Range") return b.dynRangeDb.load();
        if (field == "Atk") return b.dynAttackMs.load();
        if (field == "Rel") return b.dynReleaseMs.load();
        if (field == "Ch") return (float) b.channel.load();
    }

    if (id == "dynOn") return params.dynOn.load() ? 1.0f : 0.0f;
    if (id == "dynThresh" || id == "compThresh") return params.dynBands[0].threshDb.load();
    if (id == "dynRatio" || id == "compRatio") return params.dynBands[0].ratio.load();
    if (id == "dynType") return (float) params.dynType.load();
    if (id == "dynBands") return (float) (params.dynBandCount.load() - 1);
    if (id == "dynX1") return params.dynX1.load();
    if (id == "dynX2") return params.dynX2.load();
    if (id == "dynX3") return params.dynX3.load();
    if (id == "dynKnee") return params.dynKneeDb.load();
    if (id == "dynDetect") return params.dynRms.load() ? 1.0f : 0.0f;
    if (id == "dynMix") return params.dynMix.load();
    if (id == "excOn") return params.excOn.load() ? 1.0f : 0.0f;
    if (id == "excBands") return (float) (params.excBandCount.load() - 1);
    if (id == "excX1") return params.excX1.load();
    if (id == "excX2") return params.excX2.load();
    if (id == "excX3") return params.excX3.load();
    if (id == "imgOn") return params.imgOn.load() ? 1.0f : 0.0f;
    if (id == "imgBands") return (float) (params.imgBandCount.load() - 1);
    if (id == "imgX1") return params.imgX1.load();
    if (id == "imgX2") return params.imgX2.load();
    if (id == "imgX3") return params.imgX3.load();
    if (id == "imgMono") return params.imgMonoBelow.load();
    if (id == "width") return params.imgWidth[0].load();
    if (id == "maxOn") return params.maxOn.load() ? 1.0f : 0.0f;
    if (id == "maxThresh") return params.maxThresholdDb.load();
    if (id == "ceiling" || id == "maxCeiling") return params.ceilingDb.load();
    if (id == "maxRelease") return params.maxReleaseMs.load();
    if (id == "maxMode") return (float) params.maxMode.load();
    if (id == "maxOversample") return (float) params.maxOversample.load();
    if (id == "maxTransient") return params.maxTransient.load();
    if (id == "maxLink") return params.maxLink.load();
    if (id == "automixOn") return params.automixOn.load() ? 1.0f : 0.0f;
    if (id == "targetLufs") return params.targetLufs.load();
    if (id == "automixProfile") return (float) params.automixProfile.load();
    if (id == "compare") return params.compare.load() ? 1.0f : 0.0f;
    if (id == "trimDb") return params.trimDb.load();

    if (id.startsWithIgnoreCase ("dyn") && id.length() > 3
        && juce::CharacterFunctions::isDigit (id[3]))
    {
        const auto band = juce::jlimit (1, 4, id.substring (3, 4).getIntValue()) - 1;
        const auto field = id.substring (4);
        const auto& b = params.dynBands[static_cast<size_t> (band)];
        if (field == "On") return b.on.load() ? 1.0f : 0.0f;
        if (field == "Thresh") return b.threshDb.load();
        if (field == "Ratio") return b.ratio.load();
        if (field == "Atk") return b.attackMs.load();
        if (field == "Rel") return b.releaseMs.load();
        if (field == "Gain") return b.gainDb.load();
    }

    if (id.startsWithIgnoreCase ("exc") && id.length() > 3
        && juce::CharacterFunctions::isDigit (id[3]))
    {
        const auto band = juce::jlimit (1, 4, id.substring (3, 4).getIntValue()) - 1;
        const auto field = id.substring (4);
        const auto& b = params.excBands[static_cast<size_t> (band)];
        if (field == "Drive") return b.driveDb.load();
        if (field == "Mix") return b.mix.load();
        if (field == "Mode") return (float) b.mode.load();
        if (field == "Dyn") return b.dynamics.load();
        if (field == "ToneBass") return b.toneBass.load();
        if (field == "ToneMid") return b.toneMid.load();
        if (field == "ToneTreble") return b.toneTreble.load();
        if (field == "TonePresence") return b.tonePresence.load();
        if (field == "Feedback") return b.feedback.load();
        if (field == "Level") return b.levelDb.load();
    }

    if (id.startsWithIgnoreCase ("img") && id.length() > 3
        && juce::CharacterFunctions::isDigit (id[3]))
    {
        const auto band = juce::jlimit (1, 4, id.substring (3, 4).getIntValue()) - 1;
        if (id.substring (4) == "Width") return params.imgWidth[static_cast<size_t> (band)].load();
    }

    return 0.0f;
}

juce::var MasterRack::toVar() const
{
    auto* object = new juce::DynamicObject();

    object->setProperty ("eqOn", params.eqOn.load());
    object->setProperty ("hpfOn", params.hpfOn.load());
    object->setProperty ("hpfFreq", params.hpfFreq.load());
    object->setProperty ("hpfSlope", params.hpfSlope.load());
    object->setProperty ("lpfOn", params.lpfOn.load());
    object->setProperty ("lpfFreq", params.lpfFreq.load());
    object->setProperty ("lpfSlope", params.lpfSlope.load());

    for (int i = 0; i < numEqBands; ++i)
        publishBand (*object, i, params.bands[static_cast<size_t> (i)]);

    publishLegacyBand (*object, "low", params.bands[0], false);
    publishLegacyBand (*object, "mid", params.bands[1], false);
    publishLegacyBand (*object, "high", params.bands[2], false);

    object->setProperty ("deqOn", params.deqOn.load());

    for (int i = 0; i < 4; ++i)
    {
        const auto& b = params.deqBands[static_cast<size_t> (i)];
        const auto n = juce::String (i + 1);
        object->setProperty ("deq" + n + "On", b.on.load());
        object->setProperty ("deq" + n + "Type", b.type.load());
        object->setProperty ("deq" + n + "Freq", (double) b.freq.load());
        object->setProperty ("deq" + n + "Q", (double) b.q.load());
        object->setProperty ("deq" + n + "Thresh", (double) b.dynThreshDb.load());
        object->setProperty ("deq" + n + "Range", (double) b.dynRangeDb.load());
        object->setProperty ("deq" + n + "Atk", (double) b.dynAttackMs.load());
        object->setProperty ("deq" + n + "Rel", (double) b.dynReleaseMs.load());
        object->setProperty ("deq" + n + "Ch", b.channel.load());
    }

    object->setProperty ("satOn", params.satOn.load());
    object->setProperty ("satMode", params.satMode.load());
    object->setProperty ("satDrive", params.satDriveDb.load());
    object->setProperty ("satMix", params.satMix.load());
    object->setProperty ("satBias", params.satBias.load());
    object->setProperty ("satTone", params.satTone.load());
    object->setProperty ("satToneBass", params.satToneBass.load());
    object->setProperty ("satToneMid", params.satToneMid.load());
    object->setProperty ("satToneTreble", params.satToneTreble.load());
    object->setProperty ("satTonePresence", params.satTonePresence.load());
    object->setProperty ("satDynamics", params.satDynamics.load());
    object->setProperty ("satFeedback", params.satFeedback.load());
    object->setProperty ("satFeedbackHz", params.satFeedbackHz.load());
    object->setProperty ("satOversample", params.satOversample.load());
    object->setProperty ("satOutput", params.satOutputDb.load());

    object->setProperty ("dynOn", params.dynOn.load());
    object->setProperty ("dynType", params.dynType.load());
    object->setProperty ("dynBands", params.dynBandCount.load() - 1);
    object->setProperty ("dynX1", (double) params.dynX1.load());
    object->setProperty ("dynX2", (double) params.dynX2.load());
    object->setProperty ("dynX3", (double) params.dynX3.load());
    object->setProperty ("dynKnee", (double) params.dynKneeDb.load());
    object->setProperty ("dynDetect", params.dynRms.load());
    object->setProperty ("dynMix", (double) params.dynMix.load());

    for (int k = 0; k < numDynBands; ++k)
    {
        const auto& b = params.dynBands[static_cast<size_t> (k)];
        const auto n = juce::String (k + 1);
        object->setProperty ("dyn" + n + "On", b.on.load());
        object->setProperty ("dyn" + n + "Thresh", (double) b.threshDb.load());
        object->setProperty ("dyn" + n + "Ratio", (double) b.ratio.load());
        object->setProperty ("dyn" + n + "Atk", (double) b.attackMs.load());
        object->setProperty ("dyn" + n + "Rel", (double) b.releaseMs.load());
        object->setProperty ("dyn" + n + "Gain", (double) b.gainDb.load());
    }

    object->setProperty ("excOn", params.excOn.load());
    object->setProperty ("excBands", params.excBandCount.load() - 1);
    object->setProperty ("excX1", (double) params.excX1.load());
    object->setProperty ("excX2", (double) params.excX2.load());
    object->setProperty ("excX3", (double) params.excX3.load());

    for (int k = 0; k < numExcBands; ++k)
    {
        const auto& b = params.excBands[static_cast<size_t> (k)];
        const auto n = juce::String (k + 1);
        object->setProperty ("exc" + n + "Drive", (double) b.driveDb.load());
        object->setProperty ("exc" + n + "Mix", (double) b.mix.load());
        object->setProperty ("exc" + n + "Mode", b.mode.load());
        object->setProperty ("exc" + n + "Dyn", (double) b.dynamics.load());
        object->setProperty ("exc" + n + "ToneBass", (double) b.toneBass.load());
        object->setProperty ("exc" + n + "ToneMid", (double) b.toneMid.load());
        object->setProperty ("exc" + n + "ToneTreble", (double) b.toneTreble.load());
        object->setProperty ("exc" + n + "TonePresence", (double) b.tonePresence.load());
        object->setProperty ("exc" + n + "Feedback", (double) b.feedback.load());
        object->setProperty ("exc" + n + "Level", (double) b.levelDb.load());
    }

    object->setProperty ("imgOn", params.imgOn.load());
    object->setProperty ("imgBands", params.imgBandCount.load() - 1);
    object->setProperty ("imgX1", (double) params.imgX1.load());
    object->setProperty ("imgX2", (double) params.imgX2.load());
    object->setProperty ("imgX3", (double) params.imgX3.load());
    object->setProperty ("imgMono", (double) params.imgMonoBelow.load());

    for (int k = 0; k < numImgBands; ++k)
        object->setProperty ("img" + juce::String (k + 1) + "Width",
                             (double) params.imgWidth[static_cast<size_t> (k)].load());

    object->setProperty ("maxOn", params.maxOn.load());
    object->setProperty ("maxThresh", (double) params.maxThresholdDb.load());
    object->setProperty ("ceiling", (double) params.ceilingDb.load());
    object->setProperty ("maxRelease", (double) params.maxReleaseMs.load());
    object->setProperty ("maxMode", params.maxMode.load());
    object->setProperty ("maxOversample", params.maxOversample.load());
    object->setProperty ("maxTransient", (double) params.maxTransient.load());
    object->setProperty ("maxLink", (double) params.maxLink.load());

    object->setProperty ("automixOn", params.automixOn.load());
    object->setProperty ("targetLufs", (double) params.targetLufs.load());
    object->setProperty ("automixProfile", params.automixProfile.load());
    object->setProperty ("compare", params.compare.load());
    object->setProperty ("trimDb", (double) params.trimDb.load());

    return juce::var (object);
}

void MasterRack::fromVar (const juce::var& state)
{
    if (! state.isObject())
        return;

    params.eqOn.store (readBool (state, "eqOn", params.eqOn.load()));
    params.hpfOn.store (readBool (state, "hpfOn", params.hpfOn.load()));
    params.hpfFreq.store (readFloat (state, "hpfFreq", params.hpfFreq.load()));
    params.hpfSlope.store (juce::jlimit (0, 3, (int) std::lround (readFloat (state, "hpfSlope", (float) params.hpfSlope.load()))));
    params.lpfOn.store (readBool (state, "lpfOn", params.lpfOn.load()));
    params.lpfFreq.store (readFloat (state, "lpfFreq", params.lpfFreq.load()));
    params.lpfSlope.store (juce::jlimit (0, 3, (int) std::lround (readFloat (state, "lpfSlope", (float) params.lpfSlope.load()))));

    for (int i = 0; i < numEqBands; ++i)
        restoreBand (state, i, params.bands[static_cast<size_t> (i)]);

    const auto readLegacy = [&] (const char* prefix, int index)
    {
        auto& band = params.bands[static_cast<size_t> (index)];
        if (hasProperty (state, juce::String ("band") + juce::String (index + 1) + "Freq"))
            return;
        band.freq.store (readFloat (state, juce::String (prefix) + "Freq", band.freq.load()));
        band.gainDb.store (readFloat (state, juce::String (prefix) + "Gain", band.gainDb.load()));
        // v2 sessions expect their low/mid/high moves to stay audible.
        band.on.store (true);
    };
    readLegacy ("low", 0);
    readLegacy ("mid", 1);
    readLegacy ("high", 2);

    params.deqOn.store (readBool (state, "deqOn", params.deqOn.load()));

    for (int i = 0; i < 4; ++i)
    {
        auto& b = params.deqBands[static_cast<size_t> (i)];
        const auto n = juce::String (i + 1);
        b.on.store (readBool (state, ("deq" + n + "On").toRawUTF8(), b.on.load()));
        b.type.store (juce::jlimit (0, 2, (int) std::lround (readFloat (state, ("deq" + n + "Type").toRawUTF8(), (float) b.type.load()))));
        b.freq.store (readFloat (state, ("deq" + n + "Freq").toRawUTF8(), b.freq.load()));
        b.q.store (readFloat (state, ("deq" + n + "Q").toRawUTF8(), b.q.load()));
        b.dynThreshDb.store (readFloat (state, ("deq" + n + "Thresh").toRawUTF8(), b.dynThreshDb.load()));
        b.dynRangeDb.store (readFloat (state, ("deq" + n + "Range").toRawUTF8(), b.dynRangeDb.load()));
        b.dynAttackMs.store (readFloat (state, ("deq" + n + "Atk").toRawUTF8(), b.dynAttackMs.load()));
        b.dynReleaseMs.store (readFloat (state, ("deq" + n + "Rel").toRawUTF8(), b.dynReleaseMs.load()));
        b.channel.store (juce::jlimit (0, 2, (int) std::lround (readFloat (state, ("deq" + n + "Ch").toRawUTF8(), (float) b.channel.load()))));
    }

    params.satOn.store (readBool (state, "satOn", params.satOn.load()));
    params.satMode.store (juce::jlimit (0, 7, (int) std::lround (readFloat (state, "satMode", (float) params.satMode.load()))));
    params.satDriveDb.store (readFloat (state, "satDrive", params.satDriveDb.load()));
    params.satMix.store (readFloat (state, "satMix", params.satMix.load()));
    params.satBias.store (readFloat (state, "satBias", params.satBias.load()));
    params.satTone.store (readFloat (state, "satTone", params.satTone.load()));
    params.satToneBass.store (readFloat (state, "satToneBass", params.satToneBass.load()));
    params.satToneMid.store (readFloat (state, "satToneMid", params.satToneMid.load()));
    params.satToneTreble.store (readFloat (state, "satToneTreble", params.satToneTreble.load()));
    params.satTonePresence.store (readFloat (state, "satTonePresence", params.satTonePresence.load()));
    params.satDynamics.store (readFloat (state, "satDynamics", params.satDynamics.load()));
    params.satFeedback.store (readFloat (state, "satFeedback", params.satFeedback.load()));
    params.satFeedbackHz.store (readFloat (state, "satFeedbackHz", params.satFeedbackHz.load()));
    params.satOversample.store (readBool (state, "satOversample", params.satOversample.load()));
    params.satOutputDb.store (readFloat (state, "satOutput", params.satOutputDb.load()));

    params.dynOn.store (readBool (state, "dynOn", params.dynOn.load()));
    params.dynType.store (juce::jlimit (0, 7, (int) std::lround (readFloat (state, "dynType", (float) params.dynType.load()))));
    params.dynBandCount.store (juce::jlimit (1, 4, (int) std::lround (readFloat (state, "dynBands", (float) (params.dynBandCount.load() - 1))) + 1));
    params.dynX1.store (readFloat (state, "dynX1", params.dynX1.load()));
    params.dynX2.store (readFloat (state, "dynX2", params.dynX2.load()));
    params.dynX3.store (readFloat (state, "dynX3", params.dynX3.load()));
    params.dynKneeDb.store (readFloat (state, "dynKnee", params.dynKneeDb.load()));
    params.dynRms.store (readBool (state, "dynDetect", params.dynRms.load()));
    params.dynMix.store (readFloat (state, "dynMix", params.dynMix.load()));

    for (int k = 0; k < numDynBands; ++k)
    {
        auto& b = params.dynBands[static_cast<size_t> (k)];
        const auto n = juce::String (k + 1);
        b.on.store (readBool (state, ("dyn" + n + "On").toRawUTF8(), b.on.load()));
        b.threshDb.store (readFloat (state, ("dyn" + n + "Thresh").toRawUTF8(), b.threshDb.load()));
        b.ratio.store (readFloat (state, ("dyn" + n + "Ratio").toRawUTF8(), b.ratio.load()));
        b.attackMs.store (readFloat (state, ("dyn" + n + "Atk").toRawUTF8(), b.attackMs.load()));
        b.releaseMs.store (readFloat (state, ("dyn" + n + "Rel").toRawUTF8(), b.releaseMs.load()));
        b.gainDb.store (readFloat (state, ("dyn" + n + "Gain").toRawUTF8(), b.gainDb.load()));
    }

    params.excOn.store (readBool (state, "excOn", params.excOn.load()));
    params.excBandCount.store (juce::jlimit (1, 4, (int) std::lround (readFloat (state, "excBands", (float) (params.excBandCount.load() - 1))) + 1));
    params.excX1.store (readFloat (state, "excX1", params.excX1.load()));
    params.excX2.store (readFloat (state, "excX2", params.excX2.load()));
    params.excX3.store (readFloat (state, "excX3", params.excX3.load()));

    for (int k = 0; k < numExcBands; ++k)
    {
        auto& b = params.excBands[static_cast<size_t> (k)];
        const auto n = juce::String (k + 1);
        b.driveDb.store (readFloat (state, ("exc" + n + "Drive").toRawUTF8(), b.driveDb.load()));
        b.mix.store (readFloat (state, ("exc" + n + "Mix").toRawUTF8(), b.mix.load()));
        b.mode.store (juce::jlimit (0, 7, (int) std::lround (readFloat (state, ("exc" + n + "Mode").toRawUTF8(), (float) b.mode.load()))));
        b.dynamics.store (readFloat (state, ("exc" + n + "Dyn").toRawUTF8(), b.dynamics.load()));
        b.toneBass.store (readFloat (state, ("exc" + n + "ToneBass").toRawUTF8(), b.toneBass.load()));
        b.toneMid.store (readFloat (state, ("exc" + n + "ToneMid").toRawUTF8(), b.toneMid.load()));
        b.toneTreble.store (readFloat (state, ("exc" + n + "ToneTreble").toRawUTF8(), b.toneTreble.load()));
        b.tonePresence.store (readFloat (state, ("exc" + n + "TonePresence").toRawUTF8(), b.tonePresence.load()));
        b.feedback.store (readFloat (state, ("exc" + n + "Feedback").toRawUTF8(), b.feedback.load()));
        b.levelDb.store (readFloat (state, ("exc" + n + "Level").toRawUTF8(), b.levelDb.load()));
    }

    params.imgOn.store (readBool (state, "imgOn", params.imgOn.load()));
    params.imgBandCount.store (juce::jlimit (1, 4, (int) std::lround (readFloat (state, "imgBands", (float) (params.imgBandCount.load() - 1))) + 1));
    params.imgX1.store (readFloat (state, "imgX1", params.imgX1.load()));
    params.imgX2.store (readFloat (state, "imgX2", params.imgX2.load()));
    params.imgX3.store (readFloat (state, "imgX3", params.imgX3.load()));
    params.imgMonoBelow.store (readFloat (state, "imgMono", params.imgMonoBelow.load()));

    for (int k = 0; k < numImgBands; ++k)
    {
        const auto n = juce::String (k + 1);
        params.imgWidth[static_cast<size_t> (k)].store (
            readFloat (state, ("img" + n + "Width").toRawUTF8(), params.imgWidth[static_cast<size_t> (k)].load()));
    }

    params.maxOn.store (readBool (state, "maxOn", params.maxOn.load()));
    params.maxThresholdDb.store (readFloat (state, "maxThresh", params.maxThresholdDb.load()));
    params.ceilingDb.store (readFloat (state, "ceiling", params.ceilingDb.load()));
    params.maxReleaseMs.store (readFloat (state, "maxRelease", params.maxReleaseMs.load()));
    params.maxMode.store (juce::jlimit (0, 2, (int) std::lround (readFloat (state, "maxMode", (float) params.maxMode.load()))));
    params.maxOversample.store (juce::jlimit (0, 3, (int) std::lround (readFloat (state, "maxOversample", (float) params.maxOversample.load()))));
    params.maxTransient.store (readFloat (state, "maxTransient", params.maxTransient.load()));
    params.maxLink.store (readFloat (state, "maxLink", params.maxLink.load()));

    params.automixOn.store (readBool (state, "automixOn", params.automixOn.load()));
    params.targetLufs.store (readFloat (state, "targetLufs", params.targetLufs.load()));
    params.automixProfile.store (static_cast<int> (std::round (readFloat (state, "automixProfile", (float) params.automixProfile.load()))));
    params.compare.store (readBool (state, "compare", params.compare.load()));
    params.trimDb.store (readFloat (state, "trimDb", params.trimDb.load()));

    bumpRevision();
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

double MasterRack::eqResponseDb (double freq) const { return impl->eq.responseDb (freq); }
double MasterRack::deqResponseDb (double freq) const { return impl->deq.responseDb (freq); }
float MasterRack::dynGainReductionDb (int band) const { return impl->dyn.gainReductionDb (band); }

float MasterRack::shortTermLufs() const { return impl->loudness.shortTerm(); }
float MasterRack::integratedLufs() const { return impl->loudness.integrated(); }
float MasterRack::truePeakDb() const { return impl->loudness.truePeakMaxDbtp(); }
float MasterRack::gainReductionDb() const { return impl->limiter.gainReductionDb(); }
int MasterRack::latencySamples() const { return impl->limiter.latencySamples(); }
} // namespace nc
