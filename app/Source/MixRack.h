#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace nc
{
/** Vocal tracks carry the baked Open Mic stage. The track name is the only
    vocal marker the session model holds, and these tokens are the ones the
    Mixology vocal buses look for. */
inline bool isVocalTrackName (const juce::String& name)
{
    const auto upper = name.toUpperCase();

    return upper.contains ("VOX") || upper.contains ("VOCAL")
        || upper.contains ("BGV") || upper.contains ("ADLIB")
        || upper.contains ("AD LIB");
}

/** One EQ band, shared by the channel strip and the master rack. Values are
    atomics so the message thread can edit while the audio thread renders. */
struct MixEqBandParams
{
    std::atomic<bool> on { false };
    std::atomic<int> type { 0 };        // 0 bell, 1 low shelf, 2 high shelf, 3 low cut, 4 high cut, 5 notch, 6 BP, 7 AP
    std::atomic<float> freq { 1000.0f };
    std::atomic<float> gainDb { 0.0f };
    std::atomic<float> q { 1.0f };
    std::atomic<int> slope { 0 };       // 0 = 12 dB/oct ... 3 = 48 dB/oct
    std::atomic<int> channel { 0 };     // 0 stereo, 1 mid, 2 side
    std::atomic<bool> dynOn { false };
    std::atomic<float> dynThreshDb { -20.0f };
    std::atomic<float> dynRangeDb { 0.0f };
    std::atomic<float> dynAttackMs { 10.0f };
    std::atomic<float> dynReleaseMs { 120.0f };
};

/** One track's baked signal chain.

    v3 topology: Open Mic (vocals) -> 8-band Pro EQ (dynamic, M/S) -> typed
    compressor -> Saturn-style saturation -> width / output. Every parameter is
    an atomic, so the UI can edit while the transport runs. Everything is
    preallocated in prepare(); process() never allocates, locks or touches the
    message thread.
*/
class MixRackChannel
{
public:
    static constexpr int numEqBands = 8;

    struct Params
    {
        std::atomic<bool> stripOn { true };
        std::atomic<bool> vocal { false };
        std::atomic<float> inputTrimDb { 0.0f };
        std::atomic<bool> phaseInvert { false };
        std::atomic<bool> openMicOn { false };
        std::atomic<bool> openMicUserSet { false };
        std::atomic<float> openMicDriveDb { 6.0f };
        std::atomic<float> openMicMix { 0.30f };
        std::atomic<float> openMicTrimDb { 0.0f };

        // ---- Pro EQ
        std::atomic<bool> eqOn { true };
        std::atomic<bool> hpfOn { true };
        std::atomic<float> hpfFreq { 30.0f };
        std::atomic<int> hpfSlope { 0 };
        std::atomic<bool> lpfOn { false };
        std::atomic<float> lpfFreq { 18000.0f };
        std::atomic<int> lpfSlope { 0 };
        std::atomic<float> eqDynAttackMs { 10.0f };
        std::atomic<float> eqDynReleaseMs { 120.0f };
        std::array<MixEqBandParams, numEqBands> bands;

        // ---- Compressor
        std::atomic<bool> compOn { false };
        std::atomic<int> compType { 0 };     // VCA/FET/Opto/VariMu/Bus/Punch/Transparent/Limiter
        std::atomic<float> compThreshDb { -18.0f };
        std::atomic<float> compRatio { 2.0f };
        std::atomic<float> compAttackMs { 10.0f };
        std::atomic<float> compReleaseMs { 120.0f };
        std::atomic<float> compKneeDb { 6.0f };
        std::atomic<float> compMakeupDb { 0.0f };
        std::atomic<float> compMix { 1.0f };
        std::atomic<float> compDriveDb { 0.0f };
        std::atomic<float> compOutputDb { 0.0f };
        std::atomic<float> compColor { 0.0f };
        std::atomic<float> compFeedback { 0.0f };
        std::atomic<float> compLookaheadMs { 0.0f };
        std::atomic<float> compHoldMs { 0.0f };
        std::atomic<float> compRangeDb { 60.0f };
        std::atomic<float> compAmount { 1.0f };
        std::atomic<bool> compAutoRelease { false };
        std::atomic<bool> compAutoGain { false };
        std::atomic<float> scHpf { 20.0f };
        std::atomic<float> scEqFreq { 1000.0f };
        std::atomic<float> scEqGain { 0.0f };
        std::atomic<float> scEqQ { 1.0f };

        // ---- Saturation
        std::atomic<bool> satOn { false };
        std::atomic<int> satMode { 0 };       // Tape/Tube/Diode/Soft/Warm/Fold/Fuzz/Crunch
        std::atomic<float> satDriveDb { 6.0f };
        std::atomic<float> satMix { 0.3f };
        std::atomic<float> satBias { 0.0f };
        std::atomic<float> satTone { 0.0f };      // legacy macro: treble + presence
        std::atomic<float> satToneBass { 0.0f };
        std::atomic<float> satToneMid { 0.0f };
        std::atomic<float> satToneTreble { 0.0f };
        std::atomic<float> satTonePresence { 0.0f };
        std::atomic<float> satDynamics { 0.0f };
        std::atomic<float> satFeedback { 0.0f };
        std::atomic<float> satFeedbackHz { 220.0f };
        std::atomic<bool> satOversample { false }; // HQ / ADAA
        std::atomic<float> satOutputDb { 0.0f };

        // ---- Output
        std::atomic<float> outputDb { 0.0f };
        std::atomic<float> width { 1.0f };
        std::atomic<bool> mono { false };

        // Bumped by every public parameter write. The audio thread only
        // rebuilds its DSP snapshot when this changes.
        std::atomic<std::uint32_t> revision { 0 };
    };

    MixRackChannel();
    ~MixRackChannel();

    MixRackChannel (const MixRackChannel&) = delete;
    MixRackChannel& operator= (const MixRackChannel&) = delete;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** In-place stereo block. numSamples <= the prepared block size. */
    void process (juce::AudioBuffer<float>& buffer, int numSamples);

    /** One stereo frame, used by the engine while it sums the tracks. */
    void processSample (float& left, float& right);

    /** Sets the vocal role. A vocal track gets the Open Mic stage baked in
        unless the user has already switched that stage themselves, so adding
        or reopening a vocal track never leaves the mic stage off. */
    void setVocal (bool shouldBeVocal)
    {
        params.vocal.store (shouldBeVocal);

        if (shouldBeVocal && ! params.openMicUserSet.load())
            params.openMicOn.store (true);
    }
    bool isVocal() const { return params.vocal.load(); }

    /** Canonical ids are `band1On`, `band1Type`, ... `band8Q`, plus the
        legacy strip ids (`lowGain`, `lmfFreq`, ...) which map to bands 1-4. */
    void setParam (const juce::String& id, float value);
    float getParam (const juce::String& id) const;

    juce::var toVar() const;
    void fromVar (const juce::var& state);

    /** Current dynamic gain of one EQ band, in dB (0 when the band is not
        dynamic). Read-only diagnostic/metering hook. */
    float bandDynamicGainDb (int band) const;
    float bandDebugScale (int band) const;
    float bandDebugRefGain (int band) const;
    double bandStaticDb (int band, double freq) const;
    double bandReferenceDb (int band, double freq) const;
    /** Static channel-strip EQ response in dB at `freq` (for the UI curve). */
    double eqResponseDb (double freq) const;

    Params params;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    void initialiseDefaultBands();
    void bumpRevision() noexcept { params.revision.fetch_add (1, std::memory_order_relaxed); }
};

/** The master bus chain: 8-band Pro EQ, typed multiband dynamics, multiband
    exciter, Saturn-style saturation, imager, maximizer and the automix
    loudness controller. A/B stores two complete parameter snapshots. */
class MasterRack
{
public:
    static constexpr int numEqBands = 8;
    static constexpr int numDynBands = 4;
    static constexpr int numExcBands = 4;
    static constexpr int numImgBands = 4;

    struct DynBandParams
    {
        std::atomic<bool> on { true };
        std::atomic<float> threshDb { 0.0f };
        std::atomic<float> ratio { 2.0f };
        std::atomic<float> attackMs { 20.0f };
        std::atomic<float> releaseMs { 150.0f };
        std::atomic<float> gainDb { 0.0f };
    };

    struct ExcBandParams
    {
        std::atomic<float> driveDb { 6.0f };
        std::atomic<float> mix { 0.0f };
        std::atomic<int> mode { 0 };       // Tape/Tube/Diode/...
        std::atomic<float> dynamics { 0.0f };
        std::atomic<float> toneBass { 0.0f };
        std::atomic<float> toneMid { 0.0f };
        std::atomic<float> toneTreble { 0.0f };
        std::atomic<float> tonePresence { 0.0f };
        std::atomic<float> feedback { 0.0f };
        std::atomic<float> levelDb { 0.0f };
    };

    struct Params
    {
        std::atomic<bool> eqOn { true };
        std::atomic<bool> hpfOn { false };
        std::atomic<float> hpfFreq { 30.0f };
        std::atomic<int> hpfSlope { 0 };
        std::atomic<bool> lpfOn { false };
        std::atomic<float> lpfFreq { 18000.0f };
        std::atomic<int> lpfSlope { 0 };
        std::array<MixEqBandParams, numEqBands> bands;

        // ---- Dedicated dynamic EQ (4 bands), separate from the 8-band EQ.
        std::atomic<bool> deqOn { false };
        std::array<MixEqBandParams, 4> deqBands;

        std::atomic<bool> satOn { false };
        std::atomic<int> satMode { 0 };
        std::atomic<float> satDriveDb { 6.0f };
        std::atomic<float> satMix { 0.25f };
        std::atomic<float> satBias { 0.0f };
        std::atomic<float> satTone { 0.0f };
        std::atomic<float> satToneBass { 0.0f };
        std::atomic<float> satToneMid { 0.0f };
        std::atomic<float> satToneTreble { 0.0f };
        std::atomic<float> satTonePresence { 0.0f };
        std::atomic<float> satDynamics { 0.0f };
        std::atomic<float> satFeedback { 0.0f };
        std::atomic<float> satFeedbackHz { 220.0f };
        std::atomic<bool> satOversample { false };
        std::atomic<float> satOutputDb { 0.0f };

        std::atomic<bool> dynOn { false };
        std::atomic<int> dynType { 0 };
        std::atomic<int> dynBandCount { 1 };   // 1..4
        std::atomic<float> dynX1 { 120.0f };
        std::atomic<float> dynX2 { 1000.0f };
        std::atomic<float> dynX3 { 8000.0f };
        std::atomic<float> dynKneeDb { 6.0f };
        std::atomic<bool> dynRms { false };
        std::atomic<float> dynMix { 1.0f };
        std::array<DynBandParams, numDynBands> dynBands;

        std::atomic<bool> excOn { false };
        std::atomic<int> excBandCount { 1 };
        std::atomic<float> excX1 { 120.0f };
        std::atomic<float> excX2 { 1000.0f };
        std::atomic<float> excX3 { 8000.0f };
        std::array<ExcBandParams, numExcBands> excBands;

        std::atomic<bool> imgOn { false };
        std::atomic<int> imgBandCount { 1 };
        std::atomic<float> imgX1 { 120.0f };
        std::atomic<float> imgX2 { 1000.0f };
        std::atomic<float> imgX3 { 8000.0f };
        std::atomic<float> imgMonoBelow { 0.0f };
        std::array<std::atomic<float>, numImgBands> imgWidth;

        std::atomic<bool> maxOn { true };
        std::atomic<float> maxThresholdDb { 0.0f };
        std::atomic<float> ceilingDb { -1.0f };
        std::atomic<float> maxReleaseMs { 120.0f };
        std::atomic<int> maxMode { 0 };
        std::atomic<int> maxOversample { 1 };
        std::atomic<float> maxTransient { 0.0f };
        std::atomic<float> maxLink { 1.0f };

        std::atomic<bool> automixOn { false };
        std::atomic<float> targetLufs { -14.0f };
        std::atomic<int> automixProfile { 0 };

        std::atomic<bool> compare { false };
        std::atomic<float> trimDb { 0.0f };

        std::atomic<std::uint32_t> revision { 0 };
    };

    MasterRack();
    ~MasterRack();

    MasterRack (const MasterRack&) = delete;
    MasterRack& operator= (const MasterRack&) = delete;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, int numSamples);

    void setParam (const juce::String& id, float value);
    float getParam (const juce::String& id) const;

    /** Complete parameter snapshot. Message thread only. */
    void captureAB (int slot);
    void recallAB (int slot);
    int activeAB() const { return activeSlot.load(); }
    bool hasAB (int slot) const;

    juce::var toVar() const;
    void fromVar (const juce::var& state);

    float shortTermLufs() const;
    float integratedLufs() const;
    float truePeakDb() const;
    float gainReductionDb() const;
    int latencySamples() const;
    double eqResponseDb (double freq) const;
    double deqResponseDb (double freq) const;
    float dynGainReductionDb (int band) const;

    Params params;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::array<juce::var, 2> ab;
    std::array<bool, 2> abValid { false, false };
    std::atomic<int> activeSlot { 0 };

    void initialiseDefaultBands();
    void bumpRevision() noexcept { params.revision.fetch_add (1, std::memory_order_relaxed); }
};
} // namespace nc
