#pragma once

#include "HostedInsert.h"

#include <nc/engine/LatencyComp.hpp>
#include <nc/engine/ProjectLimits.hpp>
#include <nc/engine/ProjectState.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace nc
{
/** Eight serial insert slots per track plus the master slot.

    The message thread owns every InsertProcessor. The audio thread only sees
    raw pointers published through atomics. Replaced instances are retired with
    the block counter they were retired at and freed only once the callback has
    begun a later block, so no plugin is destroyed underneath processBlock().
    The "callback is running" flag alone was not enough: a pass could see it
    false in the window before the callback reached beginBlock().
*/
class TrackInsertRack
{
public:
    static constexpr int trackCount = nc::limits::maxTracks;
    static constexpr int slotsPerTrack = 8;
    static constexpr int maxCompensationSamples = 16384;

    struct RetiredProcessor
    {
        std::unique_ptr<InsertProcessor> processor;
        std::uint64_t retiredAtBlock = 0;
    };

    struct Slot
    {
        std::unique_ptr<InsertProcessor> owned;
        std::atomic<InsertProcessor*> active { nullptr };
        std::atomic<bool> bypassed { false };
        std::vector<RetiredProcessor> retiredOwned;
        engine::PluginSlotState saved;
        juce::String displayName;
        bool missing = false;
    };

    TrackInsertRack() = default;

    void prepare (double sampleRate, int maxBlockSize, int activeTracks);
    void setActiveTrackCount (int activeTracks);

    // Audio thread. beginBlock/endBlock bracket all processTrack calls.
    void beginBlock();
    void processTrack (int track, juce::AudioBuffer<float>& buffer,
                       const engine::TransportBlock& block);
    void processTrackInstrument (int track, juce::AudioBuffer<float>& buffer,
                                 const engine::TransportBlock& block,
                                 juce::MidiBuffer& midi);
    void processMaster (juce::AudioBuffer<float>& buffer,
                        const engine::TransportBlock& block);
    void endBlock();

    // Message thread.
    bool install (int track, int slot, std::unique_ptr<InsertProcessor>,
                  const engine::PluginSlotState&, juce::String& error);
    bool installInstrument (int track, std::unique_ptr<InsertProcessor>,
                            const engine::PluginSlotState&, juce::String& error);
    bool installMaster (std::unique_ptr<InsertProcessor>,
                        const engine::PluginSlotState&, juce::String& error);
    void remove (int track, int slot);
    void removeInstrument (int track);

    /** Moves the insert in `from` to `to` within one track, swapping whatever
        occupies `to`. Instances are moved, never destroyed, so a reorder keeps
        every plugin's loaded state. Message thread. */
    bool moveInsert (int track, int from, int to);
    void removeMaster();
    void markMissing (int track, int slot, const engine::PluginSlotState&, juce::String displayName);
    void markInstrumentMissing (int track, const engine::PluginSlotState&, juce::String displayName);
    void markMasterMissing (const engine::PluginSlotState&, juce::String displayName);

    /** Frees retired processors once the audio thread has begun two blocks
        since they were retired (the same rule the snapshot lists use), so a
        callback that loaded the old pointer before it was unpublished has
        finished with it. `noCallbackCanRun` frees everything and is only for
        the destructor and a stopped device; it is not a shortcut while a
        device is open. Message thread. */
    void collectGarbage (bool noCallbackCanRun = false);

    void setBypassed (int track, int slot, bool);
    void setMasterBypassed (bool);

    /** True when the slot's audio path is bypassed. Message thread. */
    bool isBypassed (int track, int slot) const;

    int chainLatency (int track) const;
    int masterLatency() const;
    int worstTrackLatency() const { return worstLatency.load (std::memory_order_acquire); }
    bool overBudget() const { return overBudgetFlag; }

    juce::String slotName (int track, int slot) const;
    juce::String instrumentName (int track) const;
    juce::String masterName() const;
    bool isMissing (int track, int slot) const;
    bool isInstrumentMissing (int track) const;
    bool isMasterMissing() const;
    engine::PluginSlotState slotState (int track, int slot) const;
    engine::PluginSlotState instrumentState (int track) const;
    engine::PluginSlotState masterState() const;
    Slot& slot (int track, int slot);
    const Slot& slot (int track, int slot) const;
    Slot& instrumentSlot (int track);
    const Slot& instrumentSlot (int track) const;
    Slot& masterSlot() { return master; }
    const Slot& masterSlot() const { return master; }

    bool isAudioCallbackActive() const { return audioInCallback.load (std::memory_order_acquire); }

private:
    struct TrackDelay
    {
        std::vector<float> left;
        std::vector<float> right;
        int writeIndex = 0;
        int capacity = 0;

        void prepare (int maxSamples);
        void process (juce::AudioBuffer<float>&, int delaySamples);
    };

    /** Moves a live processor into its slot's retirement list, stamped with the
        block the callback had begun at that moment. */
    void retire (Slot& slot, std::unique_ptr<InsertProcessor> processor);

    static bool validTrack (int track) noexcept { return juce::isPositiveAndBelow (track, trackCount); }
    static bool validSlot (int slot) noexcept { return juce::isPositiveAndBelow (slot, slotsPerTrack); }

    Slot& slotUnchecked (int track, int slot) noexcept
    {
        return tracks[static_cast<size_t> (track)][static_cast<size_t> (slot)];
    }

    const Slot& slotUnchecked (int track, int slot) const noexcept
    {
        return tracks[static_cast<size_t> (track)][static_cast<size_t> (slot)];
    }

    std::array<std::array<Slot, slotsPerTrack>, trackCount> tracks {};
    std::array<Slot, trackCount> instruments {};
    Slot master;
    std::array<TrackDelay, trackCount> delays {};
    std::array<int, trackCount> trackLatencies {};
    std::atomic<int> activeTracks { nc::limits::defaultActiveTracks };
    std::atomic<bool> audioInCallback { false };
    // Incremented by beginBlock() on the audio thread; retirement ages against
    // it. beginBlock() runs twice per engine block (tracks, then master), so
    // +2 always spans at least one full callback boundary.
    std::atomic<std::uint64_t> blockCount { 0 };
    double preparedSampleRate = 48000.0;
    int preparedBlockSize = 512;
    std::atomic<int> worstLatency { 0 };
    bool overBudgetFlag = false;

    JUCE_DECLARE_NON_COPYABLE (TrackInsertRack)
};
} // namespace nc
