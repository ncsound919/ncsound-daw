#pragma once

#include <string>
#include <vector>

namespace mixology
{
    /// The ChordStudio ecosystem surface Mixology mixes against: the Overlay FX
    /// plugin suite, the host's mixer verbs, the headless agent command set, the
    /// engine limits and the sidechain-duck defaults. Everything here is copied
    /// from the host source, not invented:
    ///   - plugin names/params: plugins/overlay-fx/plugins/*/Source/Ids.h
    ///   - mixer verbs:         Source/app/PluginChainController.h + MainComponent_Bridge.cpp
    ///   - agent commands:      Source/app/HeadlessBridge.cpp
    ///   - limits:              Source/audio/AudioEngine.h, Source/audio/Track.h
    ///   - duck shape/defaults: Source/audio/Ducker.h, OverlaySession.h
    ///   - session schema:      Source/audio/OverlaySession.h (chordstudio.overlay-session/v1)

    enum class ChordScope { track, submix, master };
    const char* scopeName (ChordScope s) noexcept;

    /// One Overlay FX plugin the host can load onto an insert.
    struct OverlayPluginDef
    {
        std::string displayName;   // catalog display name, e.g. "Overlay Channel Strip"
        std::string id;            // spaceless form the session matcher keys on
        std::string role;          // what it is for
    };

    /// A headless agent command (Source/app/HeadlessBridge.cpp).
    struct HeadlessCommandDef
    {
        std::string id;
        std::string summary;
        std::string paramsDoc;
    };

    /// A mixer route the host exposes for direct chain edits
    /// (Source/app/MainComponent_Bridge.cpp / PluginChainController.h).
    struct MixerRouteDef
    {
        std::string id;
        std::string paramsDoc;
    };

    const std::vector<OverlayPluginDef>& overlayPlugins();
    const OverlayPluginDef* findOverlayPlugin (const std::string& idOrName);

    const std::vector<HeadlessCommandDef>& headlessCommands();
    const std::vector<MixerRouteDef>& mixerRoutes();

    /// Host limits (cs::AudioEngine / cs::Track).
    constexpr int kHostTracks          = 16;   // AudioEngine.h:27
    constexpr int kHostSubmixes        = 8;    // AudioEngine.h:28
    constexpr int kMaxInsertsPerChain  = 12;   // Track.h:27
    constexpr int kDrumChannels        = 24;   // Ducker.h key-channel range

    /// Sidechain duck defaults (cs::DuckSettings, mixing-practice values:
    /// 2-6 dB of reduction, a few ms attack, ~1/16-note release).
    struct DuckDefaults
    {
        double depthDb { 4.0 };
        double thresholdDb { -30.0 };
        double attackMs { 5.0 };
        double releaseMs { 100.0 };
        double keyLowHz { 40.0 };
        double keyHighHz { 150.0 };
    };
    const DuckDefaults& duckDefaults();

    /// The overlay-session schema string the host parses.
    constexpr const char* kOverlaySchema = "chordstudio.overlay-session/v1";

    /// The automix verbs the host applies a session through (MainComponent_Automix.cpp).
    constexpr const char* kAutomixApply   = "automixApplySession";
    constexpr const char* kAutomixMeasure = "automixMeasure";
    constexpr const char* kAutomixRevert  = "automixRevert";
}
