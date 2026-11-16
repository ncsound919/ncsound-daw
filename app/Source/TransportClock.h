#pragma once

#include <nc/engine/Markers.hpp>
#include <nc/engine/SyncClock.hpp>
#include <nc/engine/TempoMap.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace nc
{
/** A replaced snapshot, stamped with the audio-block counter at the moment it
    stopped being live. The block counter is the clock's; every list is swept
    once no callback can still be reading the old pointer. */
template <typename T>
struct RetiredSnapshot
{
    std::unique_ptr<T> owned;
    uint64_t retiredAtBlock = 0;
};

template <typename T>
void retireSnapshot (std::vector<RetiredSnapshot<T>>& list,
                     std::unique_ptr<T>& owned,
                     uint64_t blockNow)
{
    if (owned != nullptr)
        list.push_back ({ std::move (owned), blockNow });
}

/** Tempo, position, loop, markers and the published tempo map.

    Message thread: every setter and query. Audio thread: liveTempoMap,
    fallbackTempo, positionSamples, storePosition, takeResetRequest,
    takeSeekRequest and countBlock — all plain atomic loads/stores or one
    exchange, no allocation and no locks.

    The published tempo map is owned here and swapped under the same
    block-counter retirement the rest of the engine uses: a replaced map is
    freed only after a later callback has started, so a block that loaded the
    old pointer has finished with it.
*/
class TransportClock
{
public:
    TransportClock();

    void setPlaying (bool on) { playing.store (on); }
    bool isPlaying() const { return playing.load(); }

    void setMetronome (bool on) { metronome.store (on); }
    bool isMetronomeOn() const { return metronome.load(); }

    void setBpm (double newBpm);
    double bpm() const { return bpmValue.load(); }

    void setTempoMap (nc::engine::TempoMap map);
    nc::engine::TempoMap tempoMap() const;

    void setMarkers (std::vector<nc::engine::Marker> markers);
    const std::vector<nc::engine::Marker>& markers() const { return markers_; }

    double positionSeconds (double sampleRate) const;
    void resetPosition();
    void setPositionSeconds (double seconds, double sampleRate);

    void setLoopEnabled (bool on) { loopOn.store (on); }
    bool isLoopEnabled() const { return loopOn.load(); }
    void setLoopBars (double startBar, double endBar);
    double loopStartBar() const { return loopStart.load(); }
    double loopEndBar() const { return loopEnd.load(); }

    /** Musical and transport context in the shape a hosted plugin expects. */
    nc::engine::TransportBlock block (double sampleRate, int blockSize) const;

    //==========================================================================
    // Audio thread. Every one of these is a single atomic operation.

    const nc::engine::TempoMap* liveTempoMap() const
    {
        return tempoSlot.live.load (std::memory_order_acquire);
    }

    const nc::engine::TempoMap& fallbackTempo() const { return fallback; }

    /** Loop bounds in samples for a tempo map and rate. The caller supplies the
        map it is already holding, so a block never loads it twice. */
    void loopPoints (const nc::engine::TempoMap& map, double rate,
                     int64_t& start, int64_t& end) const;

    int64_t positionSamples() const { return position.load(); }
    void storePosition (int64_t samples) { position.store (samples); }

    bool takeResetRequest() { return resetRequest.exchange (false); }
    bool takeSeekRequest() { return seekRequest.exchange (false); }
    void requestReset() { resetRequest.store (true); }
    void requestSeek() { seekRequest.store (true); }
    void clearRequests()
    {
        resetRequest.store (false);
        seekRequest.store (false);
    }

    void countBlock() { blocks.fetch_add (1, std::memory_order_acq_rel); }
    uint64_t blockCount() const { return blocks.load (std::memory_order_acquire); }

    /** Frees replaced tempo maps once no callback can still be holding one. */
    void reclaimTempo (uint64_t blockNow, bool callbackRunning);
    void reclaimAll() { tempoRetired.clear(); }

    /** Diagnostics and tests: replaced maps still retained. */
    int retainedTempoCount() const { return static_cast<int> (tempoRetired.size()); }

private:
    struct TempoSlot
    {
        std::unique_ptr<nc::engine::TempoMap> owned;
        std::atomic<const nc::engine::TempoMap*> live { nullptr };
    };

    TempoSlot tempoSlot;
    std::vector<RetiredSnapshot<nc::engine::TempoMap>> tempoRetired;
    nc::engine::TempoMap fallback = nc::engine::constantTempoMap (120.0);
    std::vector<nc::engine::Marker> markers_;

    std::atomic<bool> playing { false };
    std::atomic<bool> metronome { false };
    std::atomic<double> bpmValue { 120.0 };
    std::atomic<int64_t> position { 0 };
    std::atomic<bool> resetRequest { false };
    std::atomic<bool> seekRequest { false };
    std::atomic<bool> loopOn { true };
    std::atomic<double> loopStart { 1.0 };
    std::atomic<double> loopEnd { 17.0 };
    std::atomic<uint64_t> blocks { 0 };
};
} // namespace nc
