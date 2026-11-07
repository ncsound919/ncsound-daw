#pragma once

#include <nc/engine/MidiClip.hpp>
#include <nc/engine/TempoMap.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace nc::engine
{
/** One scheduled MIDI event inside a single audio block. `sampleOffset`
    is relative to the block start; events are sorted by it, note-offs
    after note-ons at the same offset so a re-triggered pitch never sticks. */
struct MidiEvent
{
    int sampleOffset = 0;
    int pitch = 60;
    int velocity = 0;   // 0 == note-off
    int channel = 0;
};

/** Events for one MIDI clip inside the PPQ window
    [blockStartPpq, blockEndPpq). Clip times are clip-relative beats;
    the clip starts at `clipStartBar` (1-based). Note-ons whose start falls
    in the window and note-offs whose end falls in the window are emitted;
    a note already sounding at the window start gets a note-on at offset 0
    so a seek/loop into the middle of a note still sounds it.

    `clipLengthBars` sizes the clip; notes past its end are ignored.
    Pure and allocation-free per event (output is caller-owned), so the
    audio callback can build a juce::MidiBuffer from it without a lock. */
inline void scheduleMidiClip (const std::vector<MidiNote>& notes,
                              double clipStartBar,
                              double clipLengthBars,
                              double blockStartPpq,
                              double blockEndPpq,
                              const TempoMap& tempoMap,
                              double sampleRate,
                              int blockSize,
                              std::vector<MidiEvent>& out)
{
    if (sampleRate <= 0.0 || ! std::isfinite (sampleRate) || blockSize <= 0)
        return;

    if (! std::isfinite (blockStartPpq) || ! std::isfinite (blockEndPpq) || blockEndPpq <= blockStartPpq)
        return;

    const auto startBeat = clipStartBeat (clipStartBar);
    const auto endBeat = startBeat + clipLengthBeats (clipLengthBars);
    const auto blockStartSeconds = ppqToSeconds (tempoMap, blockStartPpq);

    auto offsetForPpq = [&] (double ppq) -> int
    {
        const auto seconds = ppqToSeconds (tempoMap, ppq) - blockStartSeconds;
        const auto samples = std::llround (seconds * sampleRate);
        return static_cast<int> (std::clamp<int64_t> (samples, 0, blockSize));
    };

    for (const auto& note : notes)
    {
        const auto noteStartPpq = startBeat + note.startBeat;
        const auto noteEndPpq = noteStartPpq + note.lengthBeats;

        if (noteEndPpq <= startBeat || noteStartPpq >= endBeat)
            continue;

        const bool startsInBlock = noteStartPpq >= blockStartPpq && noteStartPpq < blockEndPpq;
        const bool endsInBlock = noteEndPpq > blockStartPpq && noteEndPpq <= blockEndPpq;
        const bool soundingAtStart = noteStartPpq < blockStartPpq && noteEndPpq > blockStartPpq;

        if (! startsInBlock && ! endsInBlock && ! soundingAtStart)
            continue;

        if (startsInBlock || soundingAtStart)
        {
            const auto offset = soundingAtStart ? 0 : offsetForPpq (noteStartPpq);
            out.push_back ({ offset, note.pitch, note.velocity, note.channel });
        }

        if (endsInBlock)
            out.push_back ({ offsetForPpq (noteEndPpq), note.pitch, 0, note.channel });
    }

    std::sort (out.begin(), out.end(), [] (const MidiEvent& a, const MidiEvent& b)
    {
        if (a.sampleOffset != b.sampleOffset)
            return a.sampleOffset < b.sampleOffset;

        return a.velocity < b.velocity; // note-off (0) before note-on so a same-pitch retrigger sounds
    });
}
} // namespace nc::engine
