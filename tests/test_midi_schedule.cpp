#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nc/engine/MidiSchedule.hpp>
#include <nc/engine/TempoMap.hpp>

using namespace nc::engine;

TEST_CASE ("schedule emits note on and off at sample offsets in the block")
{
    const auto map = constantTempoMap (120.0);
    // 120 BPM, 48 kHz: one beat == 0.5s == 24000 samples. Block covers ppq 0..1 (24000 samples).
    const std::vector<MidiNote> notes { { 0.0, 0.5, 60, 100, 0 }, { 0.5, 0.5, 62, 90, 0 } };

    std::vector<MidiEvent> events;
    scheduleMidiClip (notes, 1.0, 4.0, 0.0, 1.0, map, 48000.0, 24000, events);

    // Note 1: on at 0, off at 12000. Note 2: on at 12000, off at 24000 (block end, clamped).
    REQUIRE (events.size() == 4);
    REQUIRE (events[0].sampleOffset == 0);
    REQUIRE (events[0].pitch == 60);
    REQUIRE (events[0].velocity == 100);
    REQUIRE (events[1].sampleOffset == 12000);
    REQUIRE (events[1].pitch == 60);
    REQUIRE (events[1].velocity == 0);
    REQUIRE (events[2].sampleOffset == 12000);
    REQUIRE (events[2].pitch == 62);
    REQUIRE (events[2].velocity == 90);
}

TEST_CASE ("schedule retriggers a note already sounding at the block start")
{
    const auto map = constantTempoMap (120.0);
    const std::vector<MidiNote> notes { { 0.0, 4.0, 64, 80, 0 } };

    std::vector<MidiEvent> events;
    // Block starts at ppq 2, in the middle of the note.
    scheduleMidiClip (notes, 1.0, 4.0, 2.0, 3.0, map, 48000.0, 24000, events);

    REQUIRE (events.size() == 1);
    REQUIRE (events[0].sampleOffset == 0);
    REQUIRE (events[0].pitch == 64);
    REQUIRE (events[0].velocity == 80);
}

TEST_CASE ("sustained silent windows when retriggering is suppressed")
{
    const auto map = constantTempoMap (120.0);
    const std::vector<MidiNote> notes { { 0.0, 4.0, 64, 80, 0 } };

    std::vector<MidiEvent> events;
    // A window that directly continues the previous one: no new note-on.
    scheduleMidiClip (notes, 1.0, 4.0, 1.0, 2.0, map, 48000.0, 24000, events, false);
    REQUIRE (events.empty());
}

TEST_CASE ("schedule is silent outside the clip and for bad blocks")
{
    const auto map = constantTempoMap (120.0);
    const std::vector<MidiNote> notes { { 0.0, 1.0, 60, 100, 0 } };

    std::vector<MidiEvent> events;
    scheduleMidiClip (notes, 1.0, 1.0, 100.0, 101.0, map, 48000.0, 512, events);
    REQUIRE (events.empty());

    scheduleMidiClip (notes, 1.0, 4.0, 0.0, 1.0, map, 0.0, 512, events);
    REQUIRE (events.empty());

    scheduleMidiClip (notes, 1.0, 4.0, 1.0, 1.0, map, 48000.0, 512, events);
    REQUIRE (events.empty());
}

TEST_CASE ("schedule follows a tempo change inside the block window")
{
    TempoMap map;
    map.points = { { 0.0, 120.0 }, { 0.5, 60.0 } };
    sanitizeTempoMap (map);
    // ppq 0..0.5 at 120 BPM = 0.25s. ppq 0.5..1.0 at 60 BPM = 0.5s.
    const std::vector<MidiNote> notes { { 1.0, 0.5, 60, 100, 0 } };

    std::vector<MidiEvent> events;
    scheduleMidiClip (notes, 1.0, 4.0, 0.0, 4.0, map, 48000.0, 96000, events);

    REQUIRE (events.size() == 2);
    REQUIRE (events[0].sampleOffset == 36000); // (0.25+0.5)s * 48000
}
