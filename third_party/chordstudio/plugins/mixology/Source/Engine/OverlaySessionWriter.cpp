#include "OverlaySessionWriter.h"

#include <algorithm>
#include <cmath>

#include "Data/TechniqueLibrary.h"

namespace mixology
{
    namespace
    {
        double round2 (double v) { return std::round (v * 100.0) / 100.0; }

        // Overlay Channel Strip band selection: the plugin has a low shelf, an
        // lmf and hmf peaking pair and a high shelf. Pick the band whose range
        // contains the technique's nominal frequency.
        void addChannelStripBand (OverlayInstanceSpec& inst, double freqHz, double gainDb, double q)
        {
            if (freqHz <= 0.0)
                return;

            inst.params["eqOn"] = 1.0;
            if (freqHz < 300.0)
            {
                inst.params["lowFreq"] = round2 (freqHz);
                inst.params["lowGain"] = round2 (gainDb);
            }
            else if (freqHz < 1500.0)
            {
                inst.params["lmfFreq"] = round2 (freqHz);
                inst.params["lmfGain"] = round2 (gainDb);
                inst.params["lmfQ"] = round2 (q > 0.0 ? q : 1.4);
            }
            else if (freqHz < 6000.0)
            {
                inst.params["hmfFreq"] = round2 (freqHz);
                inst.params["hmfGain"] = round2 (gainDb);
                inst.params["hmfQ"] = round2 (q > 0.0 ? q : 1.4);
            }
            else
            {
                inst.params["highFreq"] = round2 (freqHz);
                inst.params["highGain"] = round2 (gainDb);
            }
        }
    }

    bool mapTechniqueToOverlay (const ResolvedChainStep& step, OverlayInstanceSpec& out, std::string& skipReason)
    {
        out = OverlayInstanceSpec { };
        const Technique* tech = findTechnique (step.techniqueId);
        if (tech == nullptr)
        {
            skipReason = "unknown technique id '" + step.techniqueId + "'";
            return false;
        }

        const double intensity = step.intensity;
        const double freq = tech->dspFreqHz;
        const double gain = tech->dspGainDb * intensity;
        const double q = tech->dspQFactor;

        const std::string& approx = tech->dspApproxType;

        if (approx == "biquad_peaking" || approx == "biquad_highshelf" || approx == "biquad_lowshelf")
        {
            out.plugin = "Overlay Channel Strip";
            out.purpose = tech->name + " (" + tech->signature + ")";
            addChannelStripBand (out, freq, gain, q);
            return true;
        }

        if (approx == "biquad_lowpass")
        {
            out.plugin = "Overlay Channel Strip";
            out.purpose = tech->name + " (master top-end contour)";
            out.params["lpfOn"] = 1.0;
            out.params["lpfFreq"] = round2 (freq > 0.0 ? freq : 16800.0);
            return true;
        }

        if (approx == "biquad_highpass_resonant")
        {
            out.plugin = "Overlay Channel Strip";
            out.purpose = tech->name;
            out.params["hpfOn"] = 1.0;
            out.params["hpfFreq"] = round2 (freq > 0.0 ? freq : 110.0);
            return true;
        }

        if (approx == "dynamics_comp")
        {
            out.plugin = "Overlay Channel Strip";
            out.purpose = tech->name + " (" + tech->signature + ")";
            out.params["compOn"] = 1.0;
            out.params["compThresh"] = -18.0 - 6.0 * intensity;
            out.params["compRatio"] = round2 (2.0 + 3.0 * intensity);
            out.params["compAttack"] = 10.0;
            out.params["compRelease"] = 100.0;
            out.params["compMix"] = 1.0;
            return true;
        }

        if (approx == "waveshaper_saturation")
        {
            out.plugin = "Overlay Channel Strip";
            out.purpose = tech->name + " (harmonic drive)";
            out.params["satOn"] = 1.0;
            out.params["satDrive"] = round2 (0.2 + 0.6 * intensity);
            out.params["satMix"] = round2 (0.3 + 0.4 * intensity);
            return true;
        }

        if (approx == "delay_feedback")
        {
            if (step.techniqueId == "delay_flow_timed")
            {
                out.plugin = "Overlay Delay";
                out.purpose = tech->name;
                out.params["mix"] = round2 (0.15 + 0.2 * intensity);
                out.params["timeMs"] = 350.0;
                out.params["feedback"] = round2 (0.2 + 0.2 * intensity);
                return true;
            }
            // The reverb throw is the only other delay_feedback technique.
            out.plugin = "Overlay Reverb";
            out.purpose = tech->name;
            out.params["mix"] = round2 (0.2 + 0.2 * intensity);
            out.params["predelay"] = 20.0;
            return true;
        }

        if (approx == "stereo_pan_width")
        {
            skipReason = "routing/width move; the host applies it as channel routing, not an insert";
            return false;
        }

        if (approx == "none")
        {
            skipReason = "utility/pitch move with no Overlay FX equivalent";
            return false;
        }

        skipReason = "no Overlay FX mapping for approx type '" + approx + "'";
        return false;
    }

    bool hasVocalMaskingConflict (const ResolutionResult& resolution)
    {
        for (const auto& c : resolution.conflicts)
            if (c.id == "conflict_freq_808_vocal_masking")
                return true;
        return false;
    }

    juce::var buildOverlaySession (const ResolutionResult& resolution,
                                   const std::vector<OverlayChannelBinding>& bindings,
                                   double sampleRate,
                                   double hostMasterGain)
    {
        using namespace juce;

        auto* root = new DynamicObject();
        root->setProperty ("schema", String (kOverlaySchema));
        root->setProperty ("planProfile", "mixology");
        root->setProperty ("sampleRate", sampleRate);
        root->setProperty ("hostMasterGain", hostMasterGain);

        // Resolve the lead-vocal host track so an 808 duck can be keyed to it.
        int vocalTrackIndex = -1;
        for (const auto& b : bindings)
            if (b.bus == BusTarget::lead_vocal)
                vocalTrackIndex = b.hostTrackIndex;
        const bool duckOn = hasVocalMaskingConflict (resolution) && vocalTrackIndex >= 0;
        const auto& dd = duckDefaults();

        Array<var> channels;
        Array<var> masterInserts;

        for (const auto& b : bindings)
        {
            if (b.bus == BusTarget::master)
            {
                for (const auto& step : resolution.stepsByBus.at (BusTarget::master))
                {
                    OverlayInstanceSpec spec;
                    std::string skip;
                    if (mapTechniqueToOverlay (step, spec, skip))
                    {
                        auto* inst = new DynamicObject();
                        inst->setProperty ("plugin", String (spec.plugin));
                        inst->setProperty ("purpose", String (spec.purpose));
                        auto* params = new DynamicObject();
                        for (const auto& kv : spec.params)
                            params->setProperty (juce::Identifier (kv.first), kv.second);
                        inst->setProperty ("params", var (params));
                        masterInserts.add (var (inst));
                    }
                }
                continue;
            }

            auto* ch = new DynamicObject();
            ch->setProperty ("trackId", String (b.trackId));

            // Only the audited auto-trim is applied as channel gain so the
            // session is loudness-neutral by default.
            auto auditIt = resolution.headroomAudit.find (b.bus);
            const double trimDb = auditIt != resolution.headroomAudit.end() ? auditIt->second.appliedAutoTrimDb : 0.0;
            ch->setProperty ("gainLinear", std::min (1.5, std::max (0.0, std::pow (10.0, -trimDb / 20.0))));
            ch->setProperty ("pan01", 0.5);
            ch->setProperty ("outputSubmix", -1);

            Array<var> inserts;
            for (const auto& step : resolution.stepsByBus.at (b.bus))
            {
                OverlayInstanceSpec spec;
                std::string skip;
                if (! mapTechniqueToOverlay (step, spec, skip))
                    continue;

                auto* inst = new DynamicObject();
                inst->setProperty ("plugin", String (spec.plugin));
                inst->setProperty ("purpose", String (spec.purpose));
                auto* params = new DynamicObject();
                for (const auto& kv : spec.params)
                    params->setProperty (juce::Identifier (kv.first), kv.second);
                inst->setProperty ("params", var (params));
                inserts.add (var (inst));
            }
            ch->setProperty ("inserts", var (inserts));

            if (b.bus == BusTarget::bass808 && duckOn)
            {
                auto* duck = new DynamicObject();
                duck->setProperty ("keyTrack", vocalTrackIndex);
                duck->setProperty ("keyDrumChannel", -1);
                duck->setProperty ("depthDb", dd.depthDb);
                duck->setProperty ("thresholdDb", dd.thresholdDb);
                duck->setProperty ("attackMs", dd.attackMs);
                duck->setProperty ("releaseMs", dd.releaseMs);
                ch->setProperty ("duck", var (duck));
            }

            channels.add (var (ch));
        }

        root->setProperty ("channels", var (channels));

        auto* master = new DynamicObject();
        master->setProperty ("inserts", var (masterInserts));
        root->setProperty ("master", var (master));

        // Empty but present, so the shape matches the parser's expectations.
        root->setProperty ("submixes", var (Array<var> { }));
        root->setProperty ("drumChannels", var (Array<var> { }));
        root->setProperty ("files", var (Array<var> { }));

        return var (root);
    }

    std::string buildOverlaySessionJson (const ResolutionResult& resolution,
                                         const std::vector<OverlayChannelBinding>& bindings,
                                         double sampleRate,
                                         double hostMasterGain)
    {
        return juce::JSON::toString (buildOverlaySession (resolution, bindings, sampleRate, hostMasterGain)).toStdString();
    }
}
