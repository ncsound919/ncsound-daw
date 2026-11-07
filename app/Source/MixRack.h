#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
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

/** One track's baked signal chain.

    The strip is the Overlay Channel Strip topology (HPF, low shelf, two
    peaking bands, high shelf, compressor, saturation, output trim). Vocal
    tracks run the Open Mic saturation stage before the strip, on by default
    and bypassable per channel.
    Everything is preallocated in prepare(); process() never allocates, locks
    or touches the message thread.
*/
class MixRackChannel
{
public:
    struct Params
    {
        std::atomic<bool> stripOn { true };
        std::atomic<bool> vocal { false };
        std::atomic<bool> openMicOn { false };
        std::atomic<bool> openMicUserSet { false };
        std::atomic<float> openMicDriveDb { 6.0f };
        std::atomic<float> openMicMix { 0.30f };
        std::atomic<float> openMicTrimDb { 0.0f };

        std::atomic<bool> eqOn { true };
        std::atomic<bool> hpfOn { true };
        std::atomic<float> hpfFreq { 30.0f };
        std::atomic<bool> lpfOn { false };
        std::atomic<float> lpfFreq { 18000.0f };
        std::atomic<float> lowFreq { 100.0f };
        std::atomic<float> lowGainDb { 0.0f };
        std::atomic<float> lmfFreq { 700.0f };
        std::atomic<float> lmfGainDb { 0.0f };
        std::atomic<float> lmfQ { 1.0f };
        std::atomic<float> hmfFreq { 3000.0f };
        std::atomic<float> hmfGainDb { 0.0f };
        std::atomic<float> hmfQ { 1.0f };
        std::atomic<float> highFreq { 10000.0f };
        std::atomic<float> highGainDb { 0.0f };

        std::atomic<bool> compOn { false };
        std::atomic<float> compThreshDb { -18.0f };
        std::atomic<float> compRatio { 2.0f };
        std::atomic<float> compAttackMs { 10.0f };
        std::atomic<float> compReleaseMs { 120.0f };
        std::atomic<float> compMakeupDb { 0.0f };

        std::atomic<bool> satOn { false };
        std::atomic<float> satDriveDb { 0.0f };
        std::atomic<float> satMix { 0.0f };

        std::atomic<float> outputDb { 0.0f };
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

    /** Parameter ids are the Overlay Channel Strip names, so Mixology's
        resolver output can be applied without an intermediate mapping table. */
    void setParam (const juce::String& id, float value);
    float getParam (const juce::String& id) const;

    juce::var toVar() const;
    void fromVar (const juce::var& state);

    Params params;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/** The master bus chain: Overlay Master EQ, dynamics, imager, maximizer and
    the automix loudness controller. A/B stores two complete parameter
    snapshots. */
class MasterRack
{
public:
    struct Params
    {
        std::atomic<bool> eqOn { true };
        std::atomic<bool> hpfOn { false };
        std::atomic<float> hpfFreq { 30.0f };
        std::atomic<bool> lpfOn { false };
        std::atomic<float> lpfFreq { 18000.0f };
        std::atomic<float> lowFreq { 120.0f };
        std::atomic<float> lowGainDb { 0.0f };
        std::atomic<float> midFreq { 1000.0f };
        std::atomic<float> midGainDb { 0.0f };
        std::atomic<float> highFreq { 10000.0f };
        std::atomic<float> highGainDb { 0.0f };

        std::atomic<bool> dynOn { false };
        std::atomic<float> dynThreshDb { -14.0f };
        std::atomic<float> dynRatio { 2.0f };

        std::atomic<bool> imgOn { false };
        std::atomic<float> width { 1.0f };

        std::atomic<bool> maxOn { true };
        std::atomic<float> ceilingDb { -1.0f };

        std::atomic<bool> automixOn { false };
        std::atomic<float> targetLufs { -14.0f };
        std::atomic<int> automixProfile { 0 };

        std::atomic<bool> compare { false };
        std::atomic<float> trimDb { 0.0f };
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

    Params params;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::array<juce::var, 2> ab;
    std::array<bool, 2> abValid { false, false };
    std::atomic<int> activeSlot { 0 };
};
} // namespace nc
