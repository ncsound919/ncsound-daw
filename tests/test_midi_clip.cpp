#include <catch2/catch_test_macros.hpp>

#include <nc/engine/MidiClip.hpp>
#include <nc/engine/ProjectJson.hpp>
#include <nc/engine/ProjectState.hpp>

#include <cmath>

using namespace nc::engine;

TEST_CASE ("midi note names use scientific pitch notation")
{
    REQUIRE (midiNoteName (60) == "C4");
    REQUIRE (midiNoteName (61) == "C#4");
    REQUIRE (midiNoteName (12) == "C1");
    REQUIRE (midiNoteName (127) == "G9");
    REQUIRE (isBlackPitch (61));
    REQUIRE_FALSE (isBlackPitch (60));
}

TEST_CASE ("quantize snaps to the 16th grid and refuses non-finite input")
{
    REQUIRE (quantizeBeat (0.13) == Catch::Approx (0.25));
    REQUIRE (quantizeBeat (0.12) == Catch::Approx (0.0));
    REQUIRE (quantizeBeat (1.0) == Catch::Approx (1.0));
    REQUIRE (quantizeBeat (std::nan ("")) == Catch::Approx (0.0));
    REQUIRE (quantizeBeat (-2.0) == Catch::Approx (0.0));
}

TEST_CASE ("sanitizeNotes clamps ranges, drops notes past the clip and sorts")
{
    std::vector<MidiNote> notes {
        { 6.0, 1.0, 200, 999, 99 },   // beyond clip, dropped
        { 2.0, 4.0, 300, 0, -1 },     // clamped and clipped to clip end
        { 0.0, 0.01, -10, 100, 0 },   // length floor applies
        { std::nan (""), 1.0, 60, 100, 0 },
    };

    sanitizeNotes (notes, 4.0);

    REQUIRE (notes.size() == 2);
    REQUIRE (notes[0].pitch == 0);
    REQUIRE (notes[0].lengthBeats >= minMidiNoteBeats);
    REQUIRE (notes[1].pitch == 127);
    REQUIRE (notes[1].velocity == 1);
    REQUIRE (notes[1].channel == 0);
    REQUIRE (notes[1].startBeat == Catch::Approx (2.0));
    REQUIRE (notes[1].lengthBeats == Catch::Approx (2.0));
}

TEST_CASE ("demo patterns are deterministic and stay inside the clip")
{
    const auto first = demoMidiNotes (0x1001, 4.0, 0);
    const auto again = demoMidiNotes (0x1001, 4.0, 0);

    REQUIRE_FALSE (first.empty());
    REQUIRE (first.size() == again.size());

    for (size_t i = 0; i < first.size(); ++i)
    {
        REQUIRE (first[i].startBeat == again[i].startBeat);
        REQUIRE (first[i].pitch == again[i].pitch);
        REQUIRE (first[i].startBeat >= 0.0);
        REQUIRE (first[i].startBeat < 16.0);
        REQUIRE (first[i].pitch >= 0 && first[i].pitch <= 127);
    }
}

TEST_CASE ("midi clips survive a project round trip and stay separate from audio")
{
    ProjectDocument doc;
    doc.bpm = 118.0;

    ClipState midi;
    midi.isMidi = true;
    midi.name = "PADS";
    midi.track = 2;
    midi.startBar = 1.0;
    midi.lengthBars = 4.0;
    midi.notes = { { 0.0, 2.0, 60, 90, 0 }, { 2.0, 1.0, 65, 80, 0 } };

    ClipState audio;
    audio.assetPath = "Media/vox.wav";
    audio.name = "VOX";
    audio.track = 3;
    audio.startBar = 5.0;
    audio.lengthBars = 2.0;

    doc.clips = { midi, audio };
    doc.tracks.resize (1);
    doc.tracks[0].instrument.identifier = "builtin-polysynth";

    const auto json = toJson (sanitize (doc));
    const auto parsed = fromJson (json);
    REQUIRE (parsed.has_value());

    const auto clean = sanitize (*parsed);
    REQUIRE (clean.clips.size() == 2);

    const auto& midiOut = clean.clips[0];
    REQUIRE (midiOut.isMidi);
    REQUIRE (midiOut.notes.size() == 2);
    REQUIRE (midiOut.notes[0].pitch == 60);
    REQUIRE (midiOut.notes[1].startBeat == Catch::Approx (2.0));

    const auto& audioOut = clean.clips[1];
    REQUIRE_FALSE (audioOut.isMidi);
    REQUIRE (audioOut.assetPath == "Media/vox.wav");
    REQUIRE (audioOut.notes.empty());

    REQUIRE (clean.tracks[0].instrument.identifier == "builtin-polysynth");
}

TEST_CASE ("v2 projects without kind or notes load as audio")
{
    const std::string legacy = "{"
        "\"version\":2,\"bpm\":120,\"activeTrackCount\":2,"
        "\"clips\":[{\"assetPath\":\"Media/a.wav\",\"name\":\"A\",\"track\":0,"
        "\"startBar\":1,\"lengthBars\":4,\"sourceOffsetSeconds\":0,"
        "\"gainDb\":0,\"fadeInBars\":0,\"fadeOutBars\":0}]}";

    const auto parsed = fromJson (legacy);
    REQUIRE (parsed.has_value());
    REQUIRE (parsed->version == 2);

    const auto clean = sanitize (*parsed);
    REQUIRE (clean.clips.size() == 1);
    REQUIRE_FALSE (clean.clips[0].isMidi);
    REQUIRE (clean.clips[0].assetPath == "Media/a.wav");
}
