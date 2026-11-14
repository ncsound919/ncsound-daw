#include <catch2/catch_test_macros.hpp>

#include <nc/engine/MidiSchedule.hpp>
#include <nc/engine/MidiScheduler.hpp>
#include <nc/engine/ProjectJson.hpp>
#include <nc/engine/ProjectState.hpp>

#include <algorithm>
#include <random>

using namespace nc::engine;

namespace
{
bool sameNoteEvents (const std::vector<MidiEvent>& a, const std::vector<MidiEvent>& b)
{
    std::vector<MidiEvent> notesA;
    std::vector<MidiEvent> notesB;

    for (const auto& e : a)
        if (e.kind == MidiEvent::Kind::noteOn || e.kind == MidiEvent::Kind::noteOff)
            notesA.push_back (e);
    for (const auto& e : b)
        if (e.kind == MidiEvent::Kind::noteOn || e.kind == MidiEvent::Kind::noteOff)
            notesB.push_back (e);

    if (notesA.size() != notesB.size())
        return false;

    for (size_t i = 0; i < notesA.size(); ++i)
    {
        if (notesA[i].sampleOffset != notesB[i].sampleOffset
            || notesA[i].pitch != notesB[i].pitch
            || notesA[i].velocity != notesB[i].velocity
            || notesA[i].channel != notesB[i].channel
            || notesA[i].kind != notesB[i].kind)
            return false;
    }

    return true;
}
} // namespace

TEST_CASE ("cursor scheduler matches the reference scheduler block by block")
{
    const auto map = constantTempoMap (120.0);
    std::vector<MidiNote> notes;
    std::mt19937 rng (42);
    std::uniform_real_distribution<double> beat (0.0, 15.0);
    std::uniform_int_distribution<int> pitch (36, 84);

    for (int i = 0; i < 200; ++i)
        notes.push_back ({ beat (rng), 0.5, pitch (rng), 100, 0 });

    sanitizeNotes (notes, clipLengthBeats (4.0));

    MidiCursor cursor;
    std::vector<MidiEvent> cursorEvents;
    std::vector<MidiEvent> referenceEvents;

    // 128-sample blocks across 16 beats (4 bars).
    for (double ppq = 0.0; ppq < 16.0; ppq += 0.25)
    {
        cursorEvents.clear();
        referenceEvents.clear();

        scheduleMidiClipCursor (notes, {}, {}, 1.0, 4.0, ppq, ppq + 0.25, map, 48000.0, 6000, cursor, cursorEvents);
        scheduleMidiClip (notes, 1.0, 4.0, ppq, ppq + 0.25, map, 48000.0, 6000, referenceEvents, ppq == 0.0);

        REQUIRE (sameNoteEvents (cursorEvents, referenceEvents));
    }
}

TEST_CASE ("cursor scheduler emits cc and bend events in order")
{
    const auto map = constantTempoMap (120.0);
    const std::vector<MidiNote> notes { { 1.0, 0.5, 60, 100, 0 } };
    const std::vector<MidiCC> ccs { { 0.5, 0, 64, 127 }, { 1.0, 0, 64, 0 } };
    const std::vector<MidiBend> bends { { 0.75, 0, 16383 } };

    MidiCursor cursor;
    std::vector<MidiEvent> events;
    scheduleMidiClipCursor (notes, ccs, bends, 1.0, 4.0, 0.0, 4.0, map, 48000.0, 96000, cursor, events);

    // cc at 0.5, bend at 0.75, note-on at 1.0, cc at 1.0 (cc sorts before note-on), note-off at 1.5
    REQUIRE (events.size() == 5);
    REQUIRE (events[0].kind == MidiEvent::Kind::cc);
    REQUIRE (events[0].controller == 64);
    REQUIRE (events[0].sampleOffset == 12000);
    REQUIRE (events[1].kind == MidiEvent::Kind::bend);
    REQUIRE (events[1].value == 16383);
    REQUIRE (events[2].kind == MidiEvent::Kind::cc); // same offset as note-on, cc first
    REQUIRE (events[3].kind == MidiEvent::Kind::noteOn);
    REQUIRE (events[4].kind == MidiEvent::Kind::noteOff);
}

TEST_CASE ("cursor scheduler rewinds on seek and retriggers sounding notes")
{
    const auto map = constantTempoMap (120.0);
    const std::vector<MidiNote> notes { { 0.0, 4.0, 64, 80, 0 } };

    MidiCursor cursor;
    std::vector<MidiEvent> events;

    scheduleMidiClipCursor (notes, {}, {}, 1.0, 4.0, 0.0, 1.0, map, 48000.0, 24000, cursor, events);
    REQUIRE (events.size() == 1); // note-on at 0

    // Seek backwards: cursor must rewind and retrigger.
    events.clear();
    scheduleMidiClipCursor (notes, {}, {}, 1.0, 4.0, 0.0, 1.0, map, 48000.0, 24000, cursor, events);
    REQUIRE (events.size() == 1);
    REQUIRE (events[0].kind == MidiEvent::Kind::noteOn);
    REQUIRE (events[0].sampleOffset == 0);

    // Jump into the middle of the note: retrigger at 0.
    events.clear();
    scheduleMidiClipCursor (notes, {}, {}, 1.0, 4.0, 2.0, 3.0, map, 48000.0, 24000, cursor, events);
    REQUIRE (events.size() == 1);
    REQUIRE (events[0].sampleOffset == 0);
}

TEST_CASE ("cursor scheduler does not re-attack sustained notes on contiguous blocks")
{
    const auto map = constantTempoMap (120.0);
    const std::vector<MidiNote> notes { { 0.0, 4.0, 64, 80, 0 } };

    MidiCursor cursor;
    int noteOns = 0;
    int noteOffs = 0;

    for (double ppq = 0.0; ppq < 4.0; ppq += 0.5)
    {
        std::vector<MidiEvent> events;
        scheduleMidiClipCursor (notes, {}, {}, 1.0, 4.0, ppq, ppq + 0.5, map, 48000.0, 6000, cursor, events);

        for (const auto& event : events)
        {
            if (event.kind == MidiEvent::Kind::noteOn)
                ++noteOns;
            else if (event.kind == MidiEvent::Kind::noteOff)
                ++noteOffs;
        }
    }

    // One attack, one release across the whole four-beat note.
    REQUIRE (noteOns == 1);
    REQUIRE (noteOffs == 1);
}
TEST_CASE ("cc and bends survive a project round trip")
{
    ProjectDocument doc;
    ClipState midi;
    midi.isMidi = true;
    midi.name = "CC";
    midi.track = 0;
    midi.startBar = 1.0;
    midi.lengthBars = 4.0;
    midi.ccs = { { 0.0, 0, 64, 127 } };
    midi.bends = { { 1.0, 0, 16383 } };
    doc.clips = { midi };

    const auto parsed = fromJson (toJson (sanitize (doc)));
    REQUIRE (parsed.has_value());
    const auto clean = sanitize (*parsed);
    REQUIRE (clean.clips.size() == 1);
    REQUIRE (clean.clips[0].ccs.size() == 1);
    REQUIRE (clean.clips[0].ccs[0].value == 127);
    REQUIRE (clean.clips[0].bends.size() == 1);
    REQUIRE (clean.clips[0].bends[0].bend == 16383);
}
