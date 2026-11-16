#include "TrackInsertRack.h"

#include <algorithm>
#include <cmath>
#include "HostedInsert.h"
#include "PluginEditorRegistry.h"

namespace nc
{
void TrackInsertRack::TrackDelay::prepare (int maxSamples)
{
    capacity = juce::jmax (1, maxSamples) + 1;
    left.assign (static_cast<size_t> (capacity), 0.0f);
    right.assign (static_cast<size_t> (capacity), 0.0f);
    writeIndex = 0;
}

void TrackInsertRack::TrackDelay::process (juce::AudioBuffer<float>& buffer, int delaySamples)
{
    if (capacity <= 0 || delaySamples <= 0 || buffer.getNumSamples() == 0)
        return;

    const auto delay = juce::jlimit (0, capacity - 1, delaySamples);
    auto* outLeft = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0) : nullptr;
    auto* outRight = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        auto readIndex = writeIndex - delay;

        if (readIndex < 0)
            readIndex += capacity;

        const auto delayedLeft = left[static_cast<size_t> (readIndex)];
        const auto delayedRight = right[static_cast<size_t> (readIndex)];
        const auto inputLeft = outLeft != nullptr ? outLeft[i] : 0.0f;
        const auto inputRight = outRight != nullptr ? outRight[i] : inputLeft;

        left[static_cast<size_t> (writeIndex)] = inputLeft;
        right[static_cast<size_t> (writeIndex)] = inputRight;

        if (outLeft != nullptr)
            outLeft[i] = delayedLeft;

        if (outRight != nullptr)
            outRight[i] = delayedRight;

        writeIndex = (writeIndex + 1) % capacity;
    }
}

void TrackInsertRack::prepare (double sampleRate, int maxBlockSize, int active)
{
    preparedSampleRate = sampleRate;
    preparedBlockSize = maxBlockSize;
    setActiveTrackCount (active);

    for (auto& track : tracks)
        for (auto& insert : track)
            if (insert.owned != nullptr)
                insert.owned->prepare (sampleRate, maxBlockSize);

    for (auto& instrument : instruments)
        if (instrument.owned != nullptr)
            instrument.owned->prepare (sampleRate, maxBlockSize);

    if (master.owned != nullptr)
        master.owned->prepare (sampleRate, maxBlockSize);

    for (auto& delay : delays)
        delay.prepare (maxCompensationSamples);
}

void TrackInsertRack::setActiveTrackCount (int active)
{
    activeTracks.store (juce::jlimit (1, trackCount, active), std::memory_order_release);
}

void TrackInsertRack::beginBlock()
{
    blockCount.fetch_add (1, std::memory_order_acq_rel);
    audioInCallback.store (true, std::memory_order_release);
    int worst = 0;
    overBudgetFlag = false;
    const auto active = activeTracks.load (std::memory_order_acquire);

    for (int track = 0; track < trackCount; ++track)
    {
        const auto latency = track < active ? chainLatency (track) : 0;
        trackLatencies[static_cast<size_t> (track)] = latency;
        worst = juce::jmax (worst, latency);
    }

    if (worst > maxCompensationSamples)
    {
        worst = maxCompensationSamples;
        overBudgetFlag = true;
    }
    worstLatency.store (worst, std::memory_order_release);
}

void TrackInsertRack::processTrack (int track, juce::AudioBuffer<float>& buffer,
                                    const engine::TransportBlock& block)
{
    if (! validTrack (track) || track >= activeTracks.load (std::memory_order_acquire))
        return;

    for (int index = 0; index < slotsPerTrack; ++index)
    {
        auto& insert = slotUnchecked (track, index);
        auto* processor = insert.active.load (std::memory_order_acquire);

        if (processor == nullptr || insert.bypassed.load (std::memory_order_relaxed))
            continue;

        processor->process (buffer, block);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* samples = buffer.getWritePointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (! std::isfinite (samples[sample]))
                    samples[sample] = 0.0f;
        }
    }

    const auto compensation = juce::jmax (0, worstLatency.load (std::memory_order_acquire)
                                                - trackLatencies[static_cast<size_t> (track)]);
    delays[static_cast<size_t> (track)].process (buffer, compensation);
}

void TrackInsertRack::processMaster (juce::AudioBuffer<float>& buffer,
                                      const engine::TransportBlock& block)
{
    auto* processor = master.active.load (std::memory_order_acquire);

    if (processor == nullptr || master.bypassed.load (std::memory_order_relaxed))
        return;

    processor->process (buffer, block);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite (samples[sample]))
                samples[sample] = 0.0f;
    }
}

void TrackInsertRack::endBlock()
{
    audioInCallback.store (false, std::memory_order_release);
}

bool TrackInsertRack::install (int track, int slotIndex, std::unique_ptr<InsertProcessor> processor,
                               const engine::PluginSlotState& state, juce::String& error)
{
    if (! validTrack (track) || ! validSlot (slotIndex))
    {
        error = "Insert target is outside the session";
        return false;
    }

    if (processor == nullptr)
    {
        error = "The hosted insert is empty";
        return false;
    }

    processor->prepare (preparedSampleRate, preparedBlockSize);
    auto& insert = slotUnchecked (track, slotIndex);

    if (insert.owned != nullptr)
        retire (insert, std::move (insert.owned));

    insert.owned = std::move (processor);
    insert.active.store (insert.owned.get(), std::memory_order_release);
    insert.saved = state;
    insert.displayName = insert.owned->name();
    insert.bypassed.store (state.bypassed || insert.owned->isBypassed(), std::memory_order_relaxed);
    insert.missing = false;
    return true;
}

bool TrackInsertRack::installMaster (std::unique_ptr<InsertProcessor> processor,
                                     const engine::PluginSlotState& state, juce::String& error)
{
    if (processor == nullptr)
    {
        error = "The hosted insert is empty";
        return false;
    }

    processor->prepare (preparedSampleRate, preparedBlockSize);

    if (master.owned != nullptr)
        retire (master, std::move (master.owned));

    master.owned = std::move (processor);
    master.active.store (master.owned.get(), std::memory_order_release);
    master.saved = state;
    master.displayName = master.owned->name();
    master.bypassed.store (state.bypassed || master.owned->isBypassed(), std::memory_order_relaxed);
    master.missing = false;
    return true;
}

void TrackInsertRack::processTrackInstrument (int track, juce::AudioBuffer<float>& buffer,
                                                 const engine::TransportBlock& block,
                                                 juce::MidiBuffer& midi)
{
    if (! validTrack (track) || track >= activeTracks.load (std::memory_order_acquire))
        return;

    auto& instrument = instruments[static_cast<size_t> (track)];
    auto* processor = instrument.active.load (std::memory_order_acquire);

    if (processor == nullptr || instrument.bypassed.load (std::memory_order_relaxed))
        return;

    processor->process (buffer, block, midi);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite (samples[sample]))
                samples[sample] = 0.0f;
    }
}

bool TrackInsertRack::installInstrument (int track, std::unique_ptr<InsertProcessor> processor,
                                         const engine::PluginSlotState& state, juce::String& error)
{
    if (! validTrack (track))
    {
        error = "Instrument target is outside the session";
        return false;
    }
    if (processor == nullptr)
    {
        error = "The hosted instrument is empty";
        return false;
    }

    processor->prepare (preparedSampleRate, preparedBlockSize);
    auto& instrument = instruments[static_cast<size_t> (track)];

    if (instrument.owned != nullptr)
        retire (instrument, std::move (instrument.owned));

    instrument.owned = std::move (processor);
    instrument.active.store (instrument.owned.get(), std::memory_order_release);
    instrument.saved = state;
    instrument.displayName = instrument.owned->name();
    instrument.bypassed.store (state.bypassed || instrument.owned->isBypassed(), std::memory_order_relaxed);
    instrument.missing = false;
    return true;
}

juce::String TrackInsertRack::instrumentName (int track) const
{
    if (! validTrack (track))
        return {};
    const auto& instrument = instruments[static_cast<size_t> (track)];
    return instrument.missing ? (instrument.displayName.isNotEmpty() ? instrument.displayName : juce::String ("MISSING"))
                              : instrument.displayName;
}

bool TrackInsertRack::isInstrumentMissing (int track) const
{
    return validTrack (track) && instruments[static_cast<size_t> (track)].missing;
}

engine::PluginSlotState TrackInsertRack::instrumentState (int track) const
{
    if (! validTrack (track))
        return {};
    const auto& instrument = instruments[static_cast<size_t> (track)];
    if (! instrument.missing && instrument.active.load (std::memory_order_acquire) == nullptr)
        return {};
    return instrument.saved;
}

TrackInsertRack::Slot& TrackInsertRack::instrumentSlot (int track)
{
    return instruments[static_cast<size_t> (juce::jlimit (0, trackCount - 1, track))];
}

const TrackInsertRack::Slot& TrackInsertRack::instrumentSlot (int track) const
{
    return instruments[static_cast<size_t> (juce::jlimit (0, trackCount - 1, track))];
}

void TrackInsertRack::removeInstrument (int track)
{
    if (! validTrack (track))
        return;
    auto& instrument = instruments[static_cast<size_t> (track)];
    if (instrument.owned != nullptr)
        retire (instrument, std::move (instrument.owned));
    instrument.active.store (nullptr, std::memory_order_release);
    instrument.saved = {};
    instrument.displayName = {};
    instrument.bypassed.store (false, std::memory_order_relaxed);
    instrument.missing = false;
}

void TrackInsertRack::markInstrumentMissing (int track, const engine::PluginSlotState& state,
                                             juce::String displayName)
{
    if (! validTrack (track))
        return;
    auto& instrument = instruments[static_cast<size_t> (track)];
    if (instrument.owned != nullptr)
        retire (instrument, std::move (instrument.owned));
    instrument.active.store (nullptr, std::memory_order_release);
    instrument.saved = state;
    instrument.displayName = std::move (displayName);
    instrument.bypassed.store (state.bypassed, std::memory_order_relaxed);
    instrument.missing = true;
}

void TrackInsertRack::remove (int track, int slotIndex)
{
    if (! validTrack (track) || ! validSlot (slotIndex))
        return;

    auto& insert = slotUnchecked (track, slotIndex);

    if (insert.owned != nullptr)
        retire (insert, std::move (insert.owned));

    insert.active.store (nullptr, std::memory_order_release);
    insert.saved = {};
    insert.displayName = {};
    insert.bypassed.store (false, std::memory_order_relaxed);
    insert.missing = false;
}

bool TrackInsertRack::moveInsert (int track, int from, int to)
{
    if (! validTrack (track) || ! validSlot (from) || ! validSlot (to) || from == to)
        return false;

    auto& a = slotUnchecked (track, from);
    auto& b = slotUnchecked (track, to);

    // Unpublish both first: the callback must never observe a slot mid-move.
    a.active.store (nullptr, std::memory_order_release);
    b.active.store (nullptr, std::memory_order_release);

    std::swap (a.owned, b.owned);
    std::swap (a.saved, b.saved);
    std::swap (a.displayName, b.displayName);
    std::swap (a.retiredOwned, b.retiredOwned);
    std::swap (a.missing, b.missing);

    const auto aBypassed = a.bypassed.load (std::memory_order_relaxed);
    const auto bBypassed = b.bypassed.load (std::memory_order_relaxed);
    a.bypassed.store (bBypassed, std::memory_order_relaxed);
    b.bypassed.store (aBypassed, std::memory_order_relaxed);

    a.active.store (a.owned.get(), std::memory_order_release);
    b.active.store (b.owned.get(), std::memory_order_release);
    return true;
}

void TrackInsertRack::removeMaster()
{
    if (master.owned != nullptr)
        retire (master, std::move (master.owned));

    master.active.store (nullptr, std::memory_order_release);
    master.saved = {};
    master.displayName = {};
    master.bypassed.store (false, std::memory_order_relaxed);
    master.missing = false;
}

void TrackInsertRack::markMissing (int track, int slotIndex, const engine::PluginSlotState& state,
                                   juce::String displayName)
{
    if (! validTrack (track) || ! validSlot (slotIndex))
        return;

    auto& insert = slotUnchecked (track, slotIndex);

    if (insert.owned != nullptr)
        retire (insert, std::move (insert.owned));

    insert.active.store (nullptr, std::memory_order_release);
    insert.saved = state;
    insert.displayName = std::move (displayName);
    insert.bypassed.store (state.bypassed, std::memory_order_relaxed);
    insert.missing = true;
}

void TrackInsertRack::markMasterMissing (const engine::PluginSlotState& state, juce::String displayName)
{
    if (master.owned != nullptr)
        retire (master, std::move (master.owned));
    master.active.store (nullptr, std::memory_order_release);
    master.saved = state;
    master.displayName = std::move (displayName);
    master.bypassed.store (state.bypassed, std::memory_order_relaxed);
    master.missing = true;
}

void TrackInsertRack::retire (Slot& slot, std::unique_ptr<InsertProcessor> processor)
{
    if (processor == nullptr)
        return;

    // Stamp before the caller republishes the slot, so the age is never newer
    // than the swap a callback could race with.
    slot.retiredOwned.push_back ({ std::move (processor),
                                   blockCount.load (std::memory_order_acquire) });
}

void TrackInsertRack::collectGarbage (bool noCallbackCanRun)
{
    const auto now = blockCount.load (std::memory_order_acquire);

    const auto canFree = [now, noCallbackCanRun] (const RetiredProcessor& retired)
    {
        return noCallbackCanRun || retired.retiredAtBlock + 2 <= now;
    };

    // An editor window built from a retired plug-in holds the raw processor the
    // erase below destroys. Close its window first: closing deletes the window
    // and detaches the editor, exactly as its close button does.
    const auto sweep = [&canFree] (Slot& slot)
    {
        for (auto& retired : slot.retiredOwned)
            if (canFree (retired))
                if (auto* hosted = dynamic_cast<HostedInsert*> (retired.processor.get()))
                    pluginEditors::closeWindowsFor (hosted->instance());

        slot.retiredOwned.erase (
            std::remove_if (slot.retiredOwned.begin(), slot.retiredOwned.end(), canFree),
            slot.retiredOwned.end());
    };

    for (auto& track : tracks)
        for (auto& insert : track)
            sweep (insert);

    for (auto& instrument : instruments)
        sweep (instrument);

    sweep (master);
}

void TrackInsertRack::setBypassed (int track, int slotIndex, bool shouldBeBypassed)
{
    if (! validTrack (track) || ! validSlot (slotIndex))
        return;

    auto& insert = slotUnchecked (track, slotIndex);
    insert.bypassed.store (shouldBeBypassed, std::memory_order_relaxed);
    insert.saved.bypassed = shouldBeBypassed;

    if (auto* processor = insert.active.load (std::memory_order_acquire))
        processor->setBypassed (shouldBeBypassed);
}

bool TrackInsertRack::isBypassed (int track, int slotIndex) const
{
    return validTrack (track) && validSlot (slotIndex)
               ? slotUnchecked (track, slotIndex).bypassed.load (std::memory_order_relaxed)
               : false;
}

void TrackInsertRack::setMasterBypassed (bool shouldBeBypassed)
{
    master.bypassed.store (shouldBeBypassed, std::memory_order_relaxed);
    master.saved.bypassed = shouldBeBypassed;

    if (auto* processor = master.active.load (std::memory_order_acquire))
        processor->setBypassed (shouldBeBypassed);
}

int TrackInsertRack::chainLatency (int track) const
{
    if (! validTrack (track))
        return 0;

    int total = 0;

    const auto& instrument = instruments[static_cast<size_t> (track)];

    if (! instrument.bypassed.load (std::memory_order_relaxed))
        if (auto* processor = instrument.active.load (std::memory_order_acquire))
            total += juce::jlimit (0, maxCompensationSamples, processor->latencySamples());

    for (const auto& insert : tracks[static_cast<size_t> (track)])
    {
        if (insert.bypassed.load (std::memory_order_relaxed))
            continue;

        if (auto* processor = insert.active.load (std::memory_order_acquire))
            total += juce::jlimit (0, maxCompensationSamples, processor->latencySamples());
    }

    return juce::jlimit (0, maxCompensationSamples, total);
}

int TrackInsertRack::masterLatency() const
{
    if (master.bypassed.load (std::memory_order_relaxed))
        return 0;

    if (auto* processor = master.active.load (std::memory_order_acquire))
        return juce::jlimit (0, maxCompensationSamples, processor->latencySamples());

    return 0;
}

juce::String TrackInsertRack::slotName (int track, int slotIndex) const
{
    if (! validTrack (track) || ! validSlot (slotIndex))
        return {};

    const auto& insert = slotUnchecked (track, slotIndex);

    if (insert.missing)
        return insert.displayName.isNotEmpty() ? insert.displayName : juce::String ("MISSING");

    return insert.displayName;
}

juce::String TrackInsertRack::masterName() const
{
    if (master.missing)
        return master.displayName.isNotEmpty() ? master.displayName : juce::String ("MISSING");

    return master.displayName;
}

bool TrackInsertRack::isMissing (int track, int slotIndex) const
{
    return validTrack (track) && validSlot (slotIndex) && slotUnchecked (track, slotIndex).missing;
}

bool TrackInsertRack::isMasterMissing() const
{
    return master.missing;
}

engine::PluginSlotState TrackInsertRack::slotState (int track, int slotIndex) const
{
    if (! validTrack (track) || ! validSlot (slotIndex))
        return {};

    const auto& insert = slotUnchecked (track, slotIndex);

    if (! insert.missing && insert.active.load (std::memory_order_acquire) == nullptr)
        return {};

    return insert.saved;
}

engine::PluginSlotState TrackInsertRack::masterState() const
{
    if (! master.missing && master.active.load (std::memory_order_acquire) == nullptr)
        return {};

    return master.saved;
}

TrackInsertRack::Slot& TrackInsertRack::slot (int track, int slotIndex)
{
    return slotUnchecked (juce::jlimit (0, trackCount - 1, track),
                          juce::jlimit (0, slotsPerTrack - 1, slotIndex));
}

const TrackInsertRack::Slot& TrackInsertRack::slot (int track, int slotIndex) const
{
    return slotUnchecked (juce::jlimit (0, trackCount - 1, track),
                          juce::jlimit (0, slotsPerTrack - 1, slotIndex));
}
} // namespace nc
