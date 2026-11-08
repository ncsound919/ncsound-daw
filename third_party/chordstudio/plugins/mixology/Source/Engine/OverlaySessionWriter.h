#pragma once

#include <map>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "Data/ChordStudioCapabilities.h"
#include "Engine/ChainResolver.h"

namespace mixology
{
    /// One Overlay FX instance the host will load: a real plugin display name
    /// plus parameter ids in the plugin's own (denormalised) units.
    struct OverlayInstanceSpec
    {
        std::string plugin;                    // Overlay FX display name
        std::string purpose;                   // audit-log role
        std::map<std::string, double> params;  // id -> value
        std::map<std::string, std::string> state;  // non-parameter APVTS props
    };

    /// Where a session channel lives. hostTrackIndex < 0 means "not bound".
    struct OverlayChannelBinding
    {
        BusTarget bus { BusTarget::master };
        std::string trackId;
        int hostTrackIndex { -1 };
    };

    /// Maps one resolved technique to a real Overlay FX insert. Returns false
    /// (with a reason) when the technique has no Overlay equivalent, e.g. pitch
    /// correction or a pure routing/utility move.
    bool mapTechniqueToOverlay (const ResolvedChainStep& step, OverlayInstanceSpec& out, std::string& skipReason);

    /// The bass-duck the host applies under the lead vocal when a masking
    /// conflict was detected. keyTrack is the host mixer track carrying the vocal.
    struct OverlayDuck
    {
        int keyTrack { -1 };
        int keyDrumChannel { -1 };
        double depthDb { 4.0 };
        double thresholdDb { -30.0 };
        double attackMs { 5.0 };
        double releaseMs { 100.0 };
    };

    /// Builds the `chordstudio.overlay-session/v1` document the host applies
    /// through automixApplySession(). `bindings` map each bus to a host track.
    std::string buildOverlaySessionJson (const ResolutionResult& resolution,
                                         const std::vector<OverlayChannelBinding>& bindings,
                                         double sampleRate,
                                         double hostMasterGain = 1.0);

    /// The session as a var, for the in-plugin viewer / native bridge. Exposed
    /// separately so tests can inspect structure without re-parsing a string.
    juce::var buildOverlaySession (const ResolutionResult& resolution,
                                   const std::vector<OverlayChannelBinding>& bindings,
                                   double sampleRate,
                                   double hostMasterGain = 1.0);

    /// True when the resolution detected the 808-vocal masking conflict.
    bool hasVocalMaskingConflict (const ResolutionResult& resolution);
}
