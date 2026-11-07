#pragma once

#include <nc/engine/MidiSchedule.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

namespace nc::engine
{
/** Cursor state for one MIDI clip, carried across audio blocks.

    The reference scheduleMidiClip() rescans every note and sorts on every
    block. This cursor instead remembers the first note/CC/bend that has
    not yet ended before the current block, so a block costs O(active
    events), not O(all notes). Notes/CCs/bends must be sorted by beat
    (sanitizeNotes/sanitizeMidiCCs/sanitizeMidiBends guarantee this).

    A seek or loop (block start moving backwards) or a snapshot change is
    detected via lastBlockStartPpq and the cursor rewinds to 0; that rescan
    happens once per seek, never per block. No allocation in render. */
struct MidiCursor
{
    std::size_t noteIndex = 0;
    std::size_t ccIndex = 0;
    std::size_t bendIndex = 0;
    double lastBlockStartPpq = -1.0;
    bool valid = false;
};

/** Cursor-based equivalent of scheduleMidiClip() plus CC/bend events.
    Produces the same note event stream as the reference (asserted by
    property test); CC/bend events are additional. `out` must be empty or
    cleared by the caller per block. */
inline void scheduleMidiClipCursor (const std::vector<MidiNote>& notes,
                                    const std::vector<MidiCC>& ccs,
                                    const std::vector<MidiBend>& bends,
                                    double clipStartBar,
                                    double clipLengthBars,
                                    double blockStartPpq,
                                    double blockEndPpq,
                                    const TempoMap& tempoMap,
                                    double sampleRate,
                                    int blockSize,
                                    MidiCursor& cursor,
                                    std::vector<MidiEvent>& out)
{
    if (sampleRate <= 0.0 || ! std::isfinite (sampleRate) || blockSize <= 0)
        return;

    if (! std::isfinite (blockStartPpq) || ! std::isfinite (blockEndPpq) || blockEndPpq <= blockStartPpq)
        return;

    const auto startBeat = clipStartBeat (clipStartBar);
    const auto endBeat = startBeat + clipLengthBeats (clipLengthBars);
    const auto blockStartSeconds = ppqToSeconds (tempoMap, blockStartPpq);

    if (! cursor.valid || blockStartPpq < cursor.lastBlockStartPpq)
    {
        cursor.noteIndex = 0;
        cursor.ccIndex = 0;
        cursor.bendIndex = 0;
        cursor.valid = true;
    }

    cursor.lastBlockStartPpq = blockStartPpq;

    auto offsetForPpq = [&] (double ppq) -> int
    {
        const auto seconds = ppqToSeconds (tempoMap, ppq) - blockStartSeconds;
        const auto samples = std::llround (seconds * sampleRate);
        return static_cast<int> (std::clamp<int64_t> (samples, 0, blockSize));
    };

    // Advance past events that ended before this block.
    while (cursor.noteIndex < notes.size())
    {
        const auto& note = notes[cursor.noteIndex];
        if (startBeat + note.startBeat + note.lengthBeats <= blockStartPpq)
            ++cursor.noteIndex;
        else
            break;
    }

    while (cursor.ccIndex < ccs.size() && startBeat + ccs[cursor.ccIndex].beat < blockStartPpq)
        ++cursor.ccIndex;

    while (cursor.bendIndex < bends.size() && startBeat + bends[cursor.bendIndex].beat < blockStartPpq)
        ++cursor.bendIndex;

    for (std::size_t i = cursor.noteIndex; i < notes.size(); ++i)
    {
        const auto& note = notes[i];
        const auto noteStartPpq = startBeat + note.startBeat;
        const auto noteEndPpq = noteStartPpq + note.lengthBeats;

        if (noteStartPpq >= endBeat)
            break;

        if (noteStartPpq >= blockEndPpq)
            break;

        const bool startsInBlock = noteStartPpq >= blockStartPpq;
        const bool endsInBlock = noteEndPpq > blockStartPpq && noteEndPpq <= blockEndPpq;
        const bool soundingAtStart = noteStartPpq < blockStartPpq && noteEndPpq > blockStartPpq;

        if (! startsInBlock && ! endsInBlock && ! soundingAtStart)
            continue;

        if (startsInBlock || soundingAtStart)
        {
            MidiEvent event;
            event.sampleOffset = soundingAtStart ? 0 : offsetForPpq (noteStartPpq);
            event.pitch = note.pitch;
            event.velocity = note.velocity;
            event.channel = note.channel;
            event.kind = MidiEvent::Kind::noteOn;
            out.push_back (event);
        }

        if (endsInBlock)
        {
            MidiEvent event;
            event.sampleOffset = offsetForPpq (noteEndPpq);
            event.pitch = note.pitch;
            event.velocity = 0;
            event.channel = note.channel;
            event.kind = MidiEvent::Kind::noteOff;
            out.push_back (event);
        }
    }

    for (std::size_t i = cursor.ccIndex; i < ccs.size(); ++i)
    {
        const auto ppq = startBeat + ccs[i].beat;
        if (ppq >= endBeat || ppq >= blockEndPpq)
            break;
        if (ppq < blockStartPpq)
            continue;

        MidiEvent event;
        event.sampleOffset = offsetForPpq (ppq);
        event.channel = ccs[i].channel;
        event.kind = MidiEvent::Kind::cc;
        event.controller = ccs[i].controller;
        event.value = ccs[i].value;
        out.push_back (event);
    }

    for (std::size_t i = cursor.bendIndex; i < bends.size(); ++i)
    {
        const auto ppq = startBeat + bends[i].beat;
        if (ppq >= endBeat || ppq >= blockEndPpq)
            break;
        if (ppq < blockStartPpq)
            continue;

        MidiEvent event;
        event.sampleOffset = offsetForPpq (ppq);
        event.channel = bends[i].channel;
        event.kind = MidiEvent::Kind::bend;
        event.value = bends[i].bend;
        out.push_back (event);
    }

    std::sort (out.begin(), out.end(), [] (const MidiEvent& a, const MidiEvent& b)
    {
        if (a.sampleOffset != b.sampleOffset)
            return a.sampleOffset < b.sampleOffset;

        return static_cast<int> (a.kind) < static_cast<int> (b.kind);
    });
}
} // namespace nc::engine
