#pragma once

#include <nc/engine/SyncClock.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

#include <nc/engine/ProjectLimits.hpp>

#include "Theme.h"

namespace nc
{
struct TrackSpec
{
    juce::String name;
    juce::String kind;
    juce::String instrument;
    juce::String preset;
    juce::Colour tint;
    std::array<juce::String, 4> inserts;
    float pan = 0.0f;
    float sendA = 0.18f;
    float sendB = 0.08f;
};

/** Name of the nth user insert on a track. The baked Overlay Channel Strip
    is rendered separately, so the legacy copy in the demo insert array is
    skipped instead of appearing twice in the console. */
inline juce::String userInsertAt (const TrackSpec& spec, int slot)
{
    int found = 0;

    for (const auto& name : spec.inserts)
    {
        if (name.isEmpty() || name == "Overlay Channel Strip")
            continue;

        if (found++ == slot)
            return name;
    }

    return {};
}

struct ClipSpec
{
    int track = 0;
    int startBar = 1;
    int lengthBars = 4;
    juce::String name;
    int kind = 0; // 0 midi, 1 audio
    juce::uint32 seed = 0;
};

inline const std::array<TrackSpec, nc::limits::maxTracks>& sessionTracks()
{
    static const std::array<TrackSpec, nc::limits::maxTracks> tracks = []
    {
        std::array<TrackSpec, nc::limits::maxTracks> built {};

        // The 16-track demo record ships in the box. Rows past it are real but
        // empty until the user reveals them with Track > Add, up to the same
        // 64-track ceiling the engine allocates once at startup.
        const std::array<TrackSpec, nc::limits::defaultActiveTracks> core {{
        { "DRUMS", "INSTRUMENT", "Retro Drums", "Studio Kit 02", theme::trackTints[0],
          { "Overlay Channel Strip", "Overlay Retro Color", "Supa Level", "" }, -0.08f, 0.12f, 0.04f },
        { "BASS", "INSTRUMENT", "Apex Bass", "Sub Motion", theme::trackTints[1],
          { "Overlay Channel Strip", "Overlay Modulator", "", "" }, 0.0f, 0.06f, 0.0f },
        { "KEYS", "INSTRUMENT", "Chop Station", "Glass Keys", theme::trackTints[2],
          { "Overlay Channel Strip", "Overlay Delay", "Overlay Reverb", "" }, 0.14f, 0.30f, 0.12f },
        { "VOX", "AUDIO", "VoxStudio", "Lead - Bright", theme::trackTints[3],
          { "VoxStudio", "Overlay Channel Strip", "Overlay Delay", "" }, -0.12f, 0.22f, 0.18f },
        { "GTR", "AUDIO", "Aero Amp", "Clean DI", theme::trackTints[4],
          { "Aero Amp", "Overlay Channel Strip", "", "" }, 0.20f, 0.10f, 0.0f },
        { "PERC", "INSTRUMENT", "Percussion", "Room Shakers", theme::trackTints[5],
          { "Overlay Channel Strip", "Overlay Reverb", "", "" }, -0.22f, 0.16f, 0.08f },
        { "AUX 1", "RETURN", "Plate Reverb", "Large Plate", theme::trackTints[6],
          { "Overlay Reverb", "", "", "" }, 0.0f, 0.0f, 0.0f },
        { "AUX 2", "RETURN", "Temporal Delay", "Dotted 1/8", theme::trackTints[7],
          { "Temporal Delay", "", "", "" }, 0.0f, 0.0f, 0.0f },
        { "STRINGS", "INSTRUMENT", "Orchestra", "Chamber", theme::trackTints[0],
          { "Overlay Channel Strip", "Overlay Reverb", "", "" }, 0.05f, 0.14f, 0.06f },
        { "PAD", "INSTRUMENT", "Rompler", "Warm Pad", theme::trackTints[1],
          { "Overlay Channel Strip", "Overlay Delay", "", "" }, -0.05f, 0.18f, 0.10f },
        { "LEAD", "INSTRUMENT", "Chop Station", "Saw Lead", theme::trackTints[2],
          { "Overlay Channel Strip", "Supa Level", "", "" }, 0.08f, 0.10f, 0.04f },
        { "FX 1", "AUDIO", "Mixology", "Wide", theme::trackTints[3],
          { "Overlay Modulator", "", "", "" }, 0.0f, 0.08f, 0.08f },
        { "FX 2", "AUDIO", "VoxStudio", "Air", theme::trackTints[4],
          { "VoxStudio", "", "", "" }, 0.0f, 0.06f, 0.06f },
        { "BUS", "RETURN", "Overlay Master", "Glue", theme::trackTints[5],
          { "Overlay Master", "", "", "" }, 0.0f, 0.0f, 0.0f },
        { "AUX 3", "RETURN", "Nexus Reverb", "Hall", theme::trackTints[6],
          { "Nexus Reverb", "", "", "" }, 0.0f, 0.0f, 0.0f },
        { "AUX 4", "RETURN", "Temporal Delay", "Ping-Pong", theme::trackTints[7],
          { "Temporal Delay", "", "", "" }, 0.0f, 0.0f, 0.0f }
}};

        for (size_t i = 0; i < built.size(); ++i)
        {
            if (i < core.size())
            {
                built[i] = core[i];
                continue;
            }

            const auto index = static_cast<int> (i);
            const auto isReturn = (index % 8) == 6 || (index % 8) == 7;

            built[i].name = juce::String (isReturn ? "AUX " : "TRACK ") + juce::String (index + 1);
            built[i].kind = isReturn ? "RETURN" : (index % 3 == 0 ? "AUDIO" : "INSTRUMENT");
            built[i].instrument = isReturn ? "Empty Return" : "Unassigned";
            built[i].preset = "Init";
            built[i].tint = theme::trackTints[static_cast<size_t> (index % 8)];
            built[i].inserts = { "", "", "", "" };
            built[i].pan = 0.0f;
            built[i].sendA = 0.0f;
            built[i].sendB = 0.0f;
        }

        return built;
    }();

    return tracks;
}

inline std::vector<ClipSpec> sessionClips()
{
    return {
        { 0, 1, 4, "INTRO GROOVE", 0, 0x1001 },
        { 0, 5, 4, "MAIN GROOVE", 0, 0x1002 },
        { 0, 9, 4, "MAIN GROOVE", 0, 0x1003 },
        { 0, 13, 4, "OUTRO", 0, 0x1004 },
        { 1, 1, 8, "SUB MOTION", 0, 0x2001 },
        { 1, 9, 4, "LOW END", 0, 0x2002 },
        { 1, 13, 4, "SUB MOTION", 0, 0x2003 },
        { 2, 1, 4, "GLASS BED", 0, 0x3001 },
        { 2, 5, 4, "GLASS BED", 0, 0x3002 },
        { 2, 9, 2, "STAB", 0, 0x3003 },
        { 2, 11, 2, "STAB", 0, 0x3004 },
        { 2, 13, 4, "GLASS BED", 0, 0x3005 },
        { 3, 5, 4, "VERSE 1", 1, 0x4001 },
        { 3, 9, 4, "HOOK", 1, 0x4002 },
        { 3, 13, 4, "HOOK", 1, 0x4003 },
        { 4, 1, 4, "CLEAN RIFF", 1, 0x5001 },
        { 4, 5, 4, "CLEAN RIFF", 1, 0x5002 },
        { 4, 9, 4, "AMP LEAD", 1, 0x5003 },
        { 5, 1, 16, "SHAKER", 0, 0x6001 },
        { 5, 9, 4, "TAMBOURINE", 0, 0x6002 },
        { 6, 1, 16, "REVERB PRINT", 1, 0x7001 },
        { 7, 9, 8, "DELAY PRINT", 1, 0x8001 }
    };
}

inline juce::String barsBeats (double seconds, double bpm)
{
    // The musical position has one implementation, in the sync layer, so the
    // transport readout and the context handed to a hosted plugin can never
    // disagree about where the playhead is.
    const auto beats = seconds * juce::jmax (1.0, bpm) / 60.0;
    const auto position = engine::barBeatAt (beats);

    return juce::String (position.bar).paddedLeft ('0', 3) + "|"
         + juce::String (position.beat).paddedLeft ('0', 2) + "|"
         + juce::String (position.tick).paddedLeft ('0', 3);
}

inline juce::String timecode (double seconds)
{
    const auto whole = static_cast<int> (seconds);
    const auto millis = static_cast<int> ((seconds - whole) * 1000.0);

    return juce::String (whole / 60).paddedLeft ('0', 2) + ":"
         + juce::String (whole % 60).paddedLeft ('0', 2) + "."
         + juce::String (millis).paddedLeft ('0', 3);
}
} // namespace nc
