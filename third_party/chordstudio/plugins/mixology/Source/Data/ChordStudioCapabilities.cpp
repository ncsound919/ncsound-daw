#include "ChordStudioCapabilities.h"

namespace mixology
{
    const char* scopeName (ChordScope s) noexcept
    {
        switch (s)
        {
            case ChordScope::track:  return "track";
            case ChordScope::submix: return "submix";
            case ChordScope::master: return "master";
        }
        return "track";
    }

    const std::vector<OverlayPluginDef>& overlayPlugins()
    {
        static const std::vector<OverlayPluginDef> v {
            { "Overlay Channel Strip", "OverlayChannelStrip", "EQ, high/low pass, dynamics, saturation" },
            { "Overlay Delay",         "OverlayDelay",        "rhythmic delay / echo" },
            { "Overlay Lo-Fi",         "OverlayLoFi",         "noise, wobble, distortion, bit-crush (RetroColor)" },
            { "Overlay Master",        "OverlayMaster",       "master EQ, dynamics, exciter, imaging, limiter" },
            { "Overlay Modulator",     "OverlayModulator",    "chorus, flanger, phaser, tremolo, ring/fmod" },
            { "Overlay Reverb",        "OverlayReverb",       "convolution reverb" },
        };
        return v;
    }

    const OverlayPluginDef* findOverlayPlugin (const std::string& idOrName)
    {
        for (const auto& p : overlayPlugins())
            if (p.id == idOrName || p.displayName == idOrName)
                return &p;
        return nullptr;
    }

    const std::vector<HeadlessCommandDef>& headlessCommands()
    {
        static const std::vector<HeadlessCommandDef> v {
            { "session.prepare", "Offline graph state; no audio device is opened.", "" },
            { "session.sequence_summary", "The merged arrangement as a summary var.", "" },
            { "gen.regenerate",
              "Generate a new arrangement. Must be called before any export.",
              "settings: flat object, e.g. {\"tempo\":128,\"styleId\":0}; seed: 0 = random, else reproducible." },
            { "io.export_midi", "Write the arrangement as a Standard MIDI File.", "path: absolute .mid (required)." },
            { "io.render_wav", "Render the arrangement to a WAV.",
              "path: absolute .wav (required); startBeat, lengthBeats: optional." },
            { "io.export_stems", "Plan and render a stem set into a directory.", "folder: absolute directory (required)." },
        };
        return v;
    }

    const std::vector<MixerRouteDef>& mixerRoutes()
    {
        static const std::vector<MixerRouteDef> v {
            { "addInsertFromCatalog",     "trackIndex, pluginID: add an insert to a track." },
            { "addSubmixInsertFromCatalog", "submix, pluginID: add an insert to a submix bus." },
            { "addMasterInsertFromCatalog", "pluginID: add an insert to the master." },
            { "removeSlot",               "scope, trackIndex, index: remove an insert." },
            { "moveSlot",                 "scope, trackIndex, from, to: reorder a chain." },
            { "setSlotBypass",            "scope, trackIndex, index, bypass." },
            { "setTrackMix",              "trackIndex, gain, pan." },
            { "setSubmixLevel",           "submix, gain, pan." },
            { "setMasterLevel",           "gain." },
            { "setChannelRouting",        "trackIndex, sourceChannel, sourceBus, outputBus." },
            { "getChainState",            "read the current insert chains." },
            { "scanPlugins",              "rescan the VST3 catalogue." },
            { "getPluginCatalog",         "list scanned plugins (keys the session matcher)." },
        };
        return v;
    }

    const DuckDefaults& duckDefaults()
    {
        static const DuckDefaults d;
        return d;
    }
}
