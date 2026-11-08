#include "MixologyBridge.h"

#include "AudioEngine.h"
#include "MixRack.h"
#include "Session.h"

#include "Engine/ChainResolver.h"
#include "Engine/OverlaySessionWriter.h"

#include <string>
#include <vector>

namespace nc
{
namespace
{
using mixology::BusTarget;

int findTrack (const juce::String& a, const juce::String& b, int fallback)
{
    const auto& tracks = sessionTracks();

    for (int i = 0; i < static_cast<int> (tracks.size()); ++i)
    {
        const auto name = tracks[static_cast<size_t> (i)].name.toUpperCase();

        if (name.contains (a) || (b.isNotEmpty() && name.contains (b)))
            return i;
    }

    return fallback;
}

int trackForBus (BusTarget bus, int activeTracks)
{
    int track = -1;

    switch (bus)
    {
        case BusTarget::lead_vocal: track = findTrack ("VOX", "VOCAL", 3); break;
        case BusTarget::bgv:        track = findTrack ("BGV", "BG VOX", 12); break;
        case BusTarget::adlib:      track = findTrack ("ADLIB", "AD LIB", 11); break;
        case BusTarget::bass808:    track = findTrack ("BASS", "808", 1); break;
        case BusTarget::drum_bus:   track = findTrack ("DRUM", "KIT", 0); break;
        case BusTarget::master:     track = -1; break;
    }

    return track >= 0 && track < activeTracks ? track : -1;
}

bool isVocalBus (BusTarget bus)
{
    return bus == BusTarget::lead_vocal || bus == BusTarget::bgv || bus == BusTarget::adlib;
}

std::vector<mixology::SelectedModuleInstance> instancesForPreset (int preset)
{
    // Technique module ids only — no engineer names. Preset names are
    // generic sonic goals (808 Weight, Vocal Presence, Subtractive, etc.).
    // Mapping: lowend_harmonic_stack (808), vocal_clarity_chain (presence),
    // vocal_emotion_chain (emotion), subtractive_foundation (subtractive),
    // master_contour (master).
    switch (juce::jlimit (0, 4, preset))
    {
        case 1:
            return {
                { "lowend_harmonic_stack", 0.90, true, false, BusTarget::bass808 },
                { "subtractive_foundation", 0.65, true, false, BusTarget::master }
            };

        case 2:
            return {
                { "vocal_clarity_chain", 0.85, true, false, BusTarget::lead_vocal },
                { "master_contour", 0.50, true, false, BusTarget::master }
            };

        case 3:
            return {
                { "subtractive_foundation", 0.90, true, false, BusTarget::master },
                { "vocal_emotion_chain", 0.55, true, false, BusTarget::lead_vocal }
            };

        case 4:
            return {
                { "vocal_emotion_chain", 0.80, true, false, BusTarget::lead_vocal },
                { "vocal_clarity_chain", 0.70, true, false, BusTarget::lead_vocal },
                { "lowend_harmonic_stack", 0.85, true, false, BusTarget::bass808 },
                { "subtractive_foundation", 0.75, true, false, BusTarget::master },
                { "master_contour", 0.55, true, false, BusTarget::master }
            };

        case 0:
        default:
            return {
                { "vocal_emotion_chain", 0.85, true, false, BusTarget::lead_vocal },
                { "subtractive_foundation", 0.70, true, false, BusTarget::lead_vocal },
                { "master_contour", 0.55, true, false, BusTarget::master }
            };
    }
}
} // namespace

juce::StringArray mixologyPresetNames()
{
    return { "VOCAL FOCUS", "808 WEIGHT", "VOCAL PRESENCE", "SUBTRACTIVE", "FULL CHAIN" };
}

MixologyReport applyMixologyPreset (AudioEngine& engine, int preset)
{
    MixologyReport report;
    const auto instances = instancesForPreset (preset);
    const auto resolution = mixology::resolveSignalChain (instances);
    const auto activeTracks = engine.getActiveTrackCount();

    for (const auto& entry : resolution.stepsByBus)
    {
        const auto bus = entry.first;
        const auto track = trackForBus (bus, activeTracks);

        // Master steps can always land; a bus with no host track is a skip,
        // never a silent write to track 0.
        if (bus != BusTarget::master && track < 0)
        {
            report.skipped += static_cast<int> (entry.second.size());
            continue;
        }

        for (const auto& step : entry.second)
        {
            mixology::OverlayInstanceSpec spec;
            std::string skipReason;

            if (! mixology::mapTechniqueToOverlay (step, spec, skipReason))
            {
                ++report.skipped;
                continue;
            }

            // The baked rack is the channel strip and master chain. A
            // technique that resolves to a delay or reverb return is not a
            // strip parameter, so it is reported as skipped rather than
            // pretending a send was built.
            if (spec.plugin != "Overlay Channel Strip")
            {
                ++report.skipped;
                continue;
            }

            if (bus == BusTarget::master)
            {
                for (const auto& param : spec.params)
                    engine.masterMix().setParam (juce::String (param.first), static_cast<float> (param.second));

                engine.masterMix().setParam ("eqOn", 1.0f);
            }
            else
            {
                auto& channel = engine.mixChannel (track);
                channel.setParam ("stripOn", 1.0f);
                channel.setParam ("eqOn", 1.0f);

                for (const auto& param : spec.params)
                    channel.setParam (juce::String (param.first), static_cast<float> (param.second));

                if (isVocalBus (bus))
                {
                    channel.setVocal (true);
                    channel.setParam ("openMicOn", 1.0f);
                    channel.setParam ("openMicDrive", 5.0f + 5.0f * static_cast<float> (step.intensity));
                    channel.setParam ("openMicMix", 0.22f + 0.20f * static_cast<float> (step.intensity));
                }
            }

            ++report.applied;
        }
    }

    for (const auto& audit : resolution.headroomAudit)
    {
        if (audit.first == BusTarget::master)
            engine.masterMix().setParam ("trimDb", static_cast<float> (audit.second.appliedAutoTrimDb));
        else if (const auto track = trackForBus (audit.first, activeTracks); track >= 0)
            engine.mixChannel (track).setParam ("outputDb", static_cast<float> (audit.second.appliedAutoTrimDb));
    }

    report.text = "MIXOLOGY / " + juce::String (report.applied) + " INSERTS APPLIED";

    if (report.skipped > 0)
        report.text += " / " + juce::String (report.skipped) + " SKIPPED";

    if (! resolution.conflicts.empty())
        report.text += " / " + juce::String (static_cast<int> (resolution.conflicts.size())) + " CONFLICTS RESOLVED";

    return report;
}
} // namespace nc
