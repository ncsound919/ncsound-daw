#pragma once

#include <nc/engine/ProjectLimits.hpp>
#include <nc/engine/SyncClock.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace nc::engine
{
/** One note in a MIDI clip. Times are in quarter-note beats relative to the
    clip start (0 == clip start, 1 == one beat, 4 == one bar at 4/4). The
    note is held from startBeat for lengthBeats. Velocity is MIDI 1..127;
    channel is the MIDI channel 0..15 the note was authored on. */
struct MidiNote
{
    double startBeat = 0.0;
    double lengthBeats = 0.25;
    int pitch = 60;          // 0..127, 60 == middle C
    int velocity = 100;      // 1..127
    int channel = 0;         // 0..15
};

/** Smallest editable length: a 64th note (1/4 of a 16th). Shorter than this
    is inaudible on any grid and is clamped in sanitizeNotes(). */
inline constexpr double minMidiNoteBeats = 0.0625;

/** Grid used for snapping in the piano-roll: 16th notes. */
inline constexpr double midiGridBeats = 0.25;

/** Beats in one bar at the fixed 4/4 the timeline and sync clock assume. */
inline constexpr double beatsPerBarMidi = beatsPerBar;

/** Clip start (1-based bar) to beats-from-bar-1. */
inline constexpr double clipStartBeat (double startBar) noexcept
{
    return std::max (0.0, startBar - 1.0) * beatsPerBarMidi;
}

/** Clip length (bars) to beats. */
inline constexpr double clipLengthBeats (double lengthBars) noexcept
{
    return std::max (0.0, lengthBars) * beatsPerBarMidi;
}

inline bool isBlackPitch (int pitch) noexcept
{
    const auto pc = ((pitch % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

inline const char* noteNameTable[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/** Scientific pitch notation, e.g. 60 -> "C4", 61 -> "C#4", 12 -> "C1". */
inline std::string midiNoteName (int pitch)
{
    const auto clamped = std::clamp (pitch, 0, 127);
    const auto octave = clamped / 12 - 1;
    return std::string (noteNameTable[clamped % 12]) + std::to_string (octave);
}

/** Snap a beat position to the grid (16ths by default). -0 / NaN map to 0. */
inline double quantizeBeat (double beat, double grid = midiGridBeats) noexcept
{
    if (! std::isfinite (beat) || grid <= 0.0 || ! std::isfinite (grid))
        return 0.0;

    const auto snapped = std::round (beat / grid) * grid;
    return std::max (0.0, snapped);
}

/** Brings a user-supplied note list inside every limit the audio thread
    relies on: finite times, pitch/velocity/channel ranges, length floor,
    sorted by (start, pitch), clipped to the clip, capped at
    maxMidiNotesPerClip. Notes whose start is beyond the clip are dropped. */
inline void sanitizeNotes (std::vector<MidiNote>& notes, double lengthBeats)
{
    const auto clipBeats = std::max (0.0, lengthBeats);

    std::vector<MidiNote> kept;
    kept.reserve (notes.size());

    for (auto note : notes)
    {
        if (! std::isfinite (note.startBeat) || ! std::isfinite (note.lengthBeats))
            continue;

        note.pitch = std::clamp (note.pitch, 0, 127);
        note.velocity = std::clamp (note.velocity, 1, 127);
        note.channel = std::clamp (note.channel, 0, 15);
        note.startBeat = std::max (0.0, note.startBeat);
        note.lengthBeats = std::max (minMidiNoteBeats, note.lengthBeats);

        if (note.startBeat >= clipBeats)
            continue;

        note.lengthBeats = std::min (note.lengthBeats, clipBeats - note.startBeat);

        if (note.lengthBeats < minMidiNoteBeats)
            continue;

        kept.push_back (note);

        if (static_cast<int> (kept.size()) >= nc::limits::maxMidiNotesPerClip)
            break;
    }

    std::sort (kept.begin(), kept.end(), [] (const MidiNote& a, const MidiNote& b)
    {
        if (a.startBeat != b.startBeat)
            return a.startBeat < b.startBeat;

        return a.pitch < b.pitch;
    });

    notes = std::move (kept);
}

/** Deterministic demo pattern for a MIDI clip with no authored notes, so the
    shipped session makes sound without hand-placing notes. The seed picks one
    of four characters (drums, bass, chords, melody); lengthBars sizes it.
    Beats are clip-relative; pitches are absolute MIDI numbers. */
inline std::vector<MidiNote> demoMidiNotes (std::uint32_t seed, double lengthBars, int track = 0)
{
    const auto beats = clipLengthBeats (lengthBars);
    std::vector<MidiNote> notes;

    if (beats <= 0.0)
        return notes;

    const auto kind = static_cast<int> (seed % 4u);
    const auto bars = static_cast<int> (std::max (1.0, std::ceil (lengthBars)));

    auto add = [&notes, beats] (double startBeat, double lengthBeat, int pitch, int velocity)
    {
        if (startBeat >= beats)
            return;

        MidiNote note;
        note.startBeat = startBeat;
        note.lengthBeats = std::min (lengthBeat, beats - startBeat);
        note.pitch = pitch;
        note.velocity = velocity;
        note.channel = 0;
        notes.push_back (note);
    };

    // A track hint keeps drums on drum rows even when the seed repeats.
    const auto isDrumTrack = track == 0 || track == 5;

    switch (kind)
    {
        case 0: // drums: kick on 1/3, snare on 2/4, closed hats on 8ths
        {
            for (int bar = 0; bar < bars; ++bar)
            {
                const auto base = static_cast<double> (bar) * beatsPerBarMidi;

                add (base + 0.0, 0.5, 36, 110);   // kick
                add (base + 2.0, 0.5, 36, 110);   // kick
                add (base + 1.0, 0.25, 38, 100);  // snare
                add (base + 3.0, 0.25, 38, 100);  // snare

                for (int step = 0; step < 8; ++step)
                    add (base + static_cast<double> (step) * 0.5, 0.125, 42, 60 + (step % 2) * 8);

                if (isDrumTrack && bar % 2 == 1)
                    add (base + 3.5, 0.125, 42, 80);
            }

            break;
        }

        case 1: // bass: root eighths with octave pops
        {
            static constexpr int roots[4] { 31, 31, 34, 29 }; // G1 G1 A#1 F1
            static constexpr int lengths[4] { 4, 4, 4, 4 };

            for (int bar = 0; bar < bars; ++bar)
            {
                const auto root = roots[bar % 4];
                const auto base = static_cast<double> (bar) * beatsPerBarMidi;

                for (int step = 0; step < 8; ++step)
                {
                    const auto pitch = (step == 6) ? root + 12 : root;
                    add (base + static_cast<double> (step) * 0.5,
                         step < 3 ? 0.5 : 0.25, pitch, 90);
                }

                (void) lengths;
            }

            break;
        }

        case 2: // chords: one stab per bar, slow arp pick
        {
            static constexpr int chords[4][3] { { 48, 55, 59 }, // C G B
                                              { 53, 60, 65 }, // F C F
                                              { 55, 62, 67 }, // G D G
                                              { 51, 58, 63 } }; // Eb Bb Eb

            for (int bar = 0; bar < bars; ++bar)
            {
                const auto base = static_cast<double> (bar) * beatsPerBarMidi;

                for (int i = 0; i < 3; ++i)
                    add (base + 0.0, 2.0, chords[bar % 4][i], 80);

                for (int step = 0; step < 4; ++step)
                    add (base + 2.0 + static_cast<double> (step) * 0.5, 0.5,
                         chords[bar % 4][step % 3] + 12, 70);
            }

            break;
        }

        case 3: // melody: descending pentatonic eighths
        {
            static constexpr int scale[8] { 72, 70, 67, 65, 63, 60, 62, 60 };

            int stepIndex = 0;

            for (int bar = 0; bar < bars; ++bar)
            {
                const auto base = static_cast<double> (bar) * beatsPerBarMidi;

                for (int step = 0; step < 8; ++step)
                {
                    const auto pitch = scale[stepIndex % 8];
                    const auto active = (stepIndex % 3 != 2) || (bar % 2 == 0);
                    ++stepIndex;

                    if (active)
                        add (base + static_cast<double> (step) * 0.5, 0.5, pitch, 84);
                }
            }

            break;
        }}

    sanitizeNotes (notes, beats);
    return notes;
}
} // namespace nc::engine
