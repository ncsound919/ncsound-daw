#include "ChainResolver.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace mixology
{
    namespace
    {
        bool appliesTo (const Technique& t, BusTarget bus)
        {
            return std::find (t.applicableTo.begin(), t.applicableTo.end(), bus) != t.applicableTo.end();
        }

        double round1 (double v) { return std::round (v * 10.0) / 10.0; }

        bool hasModule (const std::set<std::string>& ids, const char* id)
        {
            return ids.count (id) > 0;
        }
    }

    ScaledParam scaleParameter (const std::string& paramKey, const ParamSpec& spec, double intensity)
    {
        const double clampedIntensity = std::max (0.1, std::min (1.0, intensity));

        std::string law = spec.scalingLaw;
        if (law.empty())
            law = (paramKey == "instances" || spec.unit == "slots") ? "discrete_steps"
                : (spec.unit == "Hz" ? "fixed_anchor" : "proportional");

        ScaledParam out;
        out.unit = spec.unit;
        out.originalDefault = spec.def;
        out.scalingLaw = law;

        if (law == "discrete_steps")
        {
            out.value = std::max (spec.min, std::min (spec.max, std::round (spec.def * clampedIntensity)));
        }
        else if (law == "fixed_anchor")
        {
            out.value = std::round (spec.def * 10.0) / 10.0;
        }
        else if (law == "inverse")
        {
            const double span = spec.max - spec.def;
            const double raw = spec.def + span * (0.85 - clampedIntensity) * 0.5;
            const double clamped = std::max (spec.min, std::min (spec.max, raw));
            out.value = spec.unit == "Hz" ? std::round (clamped) : std::round (clamped * 10.0) / 10.0;
        }
        else // proportional
        {
            const double raw = spec.def * clampedIntensity;
            const double clamped = std::max (spec.min, std::min (spec.max, raw));
            out.value = std::round (clamped * 10.0) / 10.0;
        }
        return out;
    }

    ResolutionResult resolveSignalChain (const std::vector<SelectedModuleInstance>& instances)
    {
        ResolutionResult result;
        for (auto bus : allBuses())
        {
            result.stepsByBus[bus] = {};
            result.headroomAudit[bus] = { bus, 0.0, 0.0, -6.0, "coherent" };
        }

        std::vector<SelectedModuleInstance> active;
        std::set<std::string> activeModuleIds;
        for (const auto& i : instances)
            if (i.enabled && i.intensity > 0.0)
            {
                active.push_back (i);
                activeModuleIds.insert (i.moduleId);
            }

        // Dedupe shared techniques per bus, keeping the higher intensity.
        struct Entry { const Technique* technique; const TechniqueModule* module; double intensity; std::string note; };
        std::map<BusTarget, std::map<std::string, Entry>> busMap;

        for (const auto& inst : active)
        {
            const auto* mod = findModule (inst.moduleId);
            if (mod == nullptr)
                continue;

            for (const auto& techId : mod->chain)
            {
                const auto* tech = findTechnique (techId);
                if (tech == nullptr)
                    continue;

                BusTarget targetBus = inst.hasTargetBusOverride ? inst.targetBusOverride : mod->primaryBus;
                if (! appliesTo (*tech, targetBus))
                    targetBus = tech->applicableTo.empty() ? targetBus : tech->applicableTo.front();

                auto& entries = busMap[targetBus];
                auto it = entries.find (tech->id);
                if (it == entries.end() || it->second.intensity < inst.intensity)
                {
                    std::string note;
                    if (it != entries.end())
                        note = "Deduplicated shared technique (" + tech->id + ") across modules; kept higher intensity.";
                    entries[tech->id] = { tech, mod, inst.intensity, note };
                }
            }
        }

        const bool has808Weight = hasModule (activeModuleIds, "lowend_harmonic_stack");
        const bool hasVocalPresence = hasModule (activeModuleIds, "vocal_clarity_chain")
                                   || hasModule (activeModuleIds, "vocal_emotion_chain")
                                   || hasModule (activeModuleIds, "harmonic_authority");

        // 1. Ordering conflict: Subtractive Precision subtractive-first vs Vocal Emotion.
        if (hasModule (activeModuleIds, "subtractive_foundation")
            && hasModule (activeModuleIds, "vocal_emotion_chain"))
        {
            ConflictReport c;
            c.id = "conflict_ordering_subtractive_emotion";
            c.type = "ordering";
            c.severity = "resolved_automatically";
            c.modulesInvolved = { "subtractive_foundation", "vocal_emotion_chain" };
            c.techniquesInvolved = { "surgical_f6_cuts", "aggressive_subtractive_eq", "c6_multiband_compression" };
            c.bus = BusTarget::lead_vocal;
            c.description = "Ordering collision on Lead Vocal: subtractive EQ must precede dynamics, while pitch-lock and multiband compression also target the bus.";
            c.resolutionStrategy = "Interleave by signal-flow stage: pitch-lock and clip-gain first, subtractive notches + high-pass before dynamics, then 1176/LA-2A and C6 multiband.";
            c.appliedAction = "Deterministic pre-dynamics interleaving applied on Lead Vocal bus.";
            result.conflicts.push_back (c);

            auto& lv = busMap[BusTarget::lead_vocal];
            auto f6 = lv.find ("surgical_f6_cuts");
            if (f6 != lv.end())
                f6->second.note = "Ordered prior to multiband and 1176/LA-2A to satisfy the subtractive-first invariant.";
        }

        // 2. Frequency masking: 808 harmonics vs vocal presence.
        if (has808Weight && hasVocalPresence)
        {
            ConflictReport c;
            c.id = "conflict_freq_808_vocal_masking";
            c.type = "frequency";
            c.severity = "resolved_automatically";
            c.modulesInvolved = { "lowend_harmonic_stack", "vocal_clarity_chain" };
            c.techniquesInvolved = { "nls_8x_stack", "uad_voice_of_god", "subtractive_eq_vocal", "presence_boost" };
            c.bus = BusTarget::bass808;
            c.description = "8x console saturation on the 808 generates 250-700 Hz upper harmonics that encroach on the vocal fundamental pocket.";
            c.resolutionStrategy = "Insert a key-triggered dynamic carve (-2.5 dB @ 320 Hz on 808_bass keyed from lead_vocal) instead of over-boosting vocal highs.";
            c.appliedAction = "Injected sidechain dynamic carve link (lead_vocal -> 808_bass @ 320 Hz).";
            result.conflicts.push_back (c);
        }

        // 3. Philosophy: rounded master low-pass vs vocal air.
        if (hasModule (activeModuleIds, "master_contour")
            && (hasModule (activeModuleIds, "vocal_clarity_chain") || hasModule (activeModuleIds, "parallel_air_gloss")))
        {
            ConflictReport c;
            c.id = "conflict_philosophy_rounded_vs_air";
            c.type = "philosophy";
            c.severity = "creative_split";
            c.modulesInvolved = { "master_contour" };
            c.techniquesInvolved = { "low_pass_rounded", "presence_boost", "parallel_air_exciter" };
            c.bus = BusTarget::master;
            c.description = "Master top-end roll-off for warmth conflicts with vocal air boosts for intelligibility.";
            c.resolutionStrategy = "Bus-isolated parallel routing: apply the master contour to the instrument/drum sub-master; route vocal air to a parallel bus that sums post-contour.";
            c.appliedAction = "Configured parallel post-contour vocal air routing.";
            result.conflicts.push_back (c);
        }

        // 4. Phase coherence on the 808.
        {
            auto& bass = busMap[BusTarget::bass808];
            if (bass.count ("uad_voice_of_god") && bass.count ("loair_sub_enhance"))
            {
                ConflictReport c;
                c.id = "conflict_phase_sub_dispersion";
                c.type = "phase_coherence";
                c.severity = "resolved_automatically";
                c.modulesInvolved = { "lowend_harmonic_stack" };
                c.techniquesInvolved = { "uad_voice_of_god", "loair_sub_enhance" };
                c.bus = BusTarget::bass808;
                c.description = "Resonant high-pass phase rotation stacked into sub-harmonic synthesis can smear fast 808 transients.";
                c.resolutionStrategy = "Lock the sub-harmonic generator to a phase-aligned dry pass and apply a 1.8 ms lookahead compensation.";
                c.appliedAction = "Added phase-alignment lock on 808_bass chain.";
                result.conflicts.push_back (c);
            }
        }

        // Per-bus ordering, gain staging and host commands.
        for (auto bus : allBuses())
        {
            auto& entries = busMap[bus];
            std::vector<Entry> ordered;
            for (auto& kv : entries)
                ordered.push_back (kv.second);
            std::sort (ordered.begin(), ordered.end(),
                       [] (const Entry& a, const Entry& b) { return a.technique->orderWeight < b.technique->orderWeight; });

            double cumulativeGainDb = 0.0, cumulativeTrimDb = 0.0;
            int allpassCount = 0;

            int idx = 0;
            for (const auto& entry : ordered)
            {
                ResolvedChainStep step;
                step.stepIndex = ++idx;
                step.bus = bus;
                step.techniqueId = entry.technique->id;
                step.techniqueName = entry.technique->name;
                step.signature = entry.technique->signature;
                step.sourceModuleId = entry.module->id;
                step.sourceModuleName = entry.module->name;
                step.intensity = entry.intensity;
                step.resolutionNote = entry.note;

                for (const auto& kv : entry.technique->parameters)
                    step.scaledParameters[kv.first] = scaleParameter (kv.first, kv.second, entry.intensity);

                step.instanceCount = entry.technique->id == "nls_8x_stack"
                    ? static_cast<int> (step.scaledParameters.count ("instances") ? step.scaledParameters["instances"].value : 8.0)
                    : 1;

                step.netGainDeltaDb = round1 (entry.technique->expectedGainDeltaDb * entry.intensity);
                step.compensationTrimDb = round1 (entry.technique->autoTrimCompensationDb * entry.intensity);
                cumulativeGainDb += step.netGainDeltaDb;
                cumulativeTrimDb += step.compensationTrimDb;
                if (entry.technique->phaseImpact == "allpass_dispersion")
                    ++allpassCount;

                result.stepsByBus[bus].push_back (step);

                HostCommand cmd;
                cmd.method = "addInsertFromCatalog";
                cmd.params["bus"] = busName (bus);
                cmd.params["plugin"] = entry.technique->preferredPlugin;
                cmd.params["purpose"] = step.techniqueName + " (" + step.signature + ")";
                if (! entry.note.empty())
                    cmd.params["note"] = entry.note;
                result.rpcPayload.push_back (std::move (cmd));
            }

            if (cumulativeGainDb > 4.5)
            {
                ConflictReport c;
                c.id = "conflict_gain_headroom_" + std::string (busName (bus));
                c.type = "gain_staging_headroom";
                c.severity = "resolved_automatically";
                for (const auto& e : ordered)
                    c.modulesInvolved.push_back (e.module->id);
                c.bus = bus;
                c.description = "Cumulative gain buildup before final limiting risks inter-plugin clipping.";
                c.resolutionStrategy = "Inject automatic unity-gain trim compensation to preserve -6 dBFS bus headroom.";
                c.appliedAction = "Applied auto-trim in the RPC payload.";
                result.conflicts.push_back (c);
            }

            result.headroomAudit[bus] = { bus, round1 (cumulativeGainDb), round1 (cumulativeTrimDb),
                                          round1 (-6.0 + cumulativeGainDb + cumulativeTrimDb),
                                          allpassCount >= 2 ? "high_allpass_shift" : "coherent" };
        }

        if (has808Weight && hasVocalPresence)
        {
            HostCommand cmd;
            cmd.method = "automixApplySession";
            cmd.params["duck_key"] = "lead_vocal";
            cmd.params["duck_target_bus"] = busName (BusTarget::bass808);
            cmd.params["note"] = "808 duck keyed by the lead vocal (masking conflict); applied via the overlay session.";
            result.rpcPayload.push_back (std::move (cmd));
        }

        return result;
    }

    std::vector<CritiqueAction> evaluateMixCritique (const std::vector<SelectedModuleInstance>& instances,
                                                     const ResolutionResult& resolution,
                                                     bool hasTelemetry,
                                                     const LiveTelemetry& telemetry)
    {
        std::vector<CritiqueAction> actions;

        for (const auto& inst : instances)
        {
            if (! inst.enabled)
                continue;

            if (inst.moduleId == "lowend_harmonic_stack")
            {
                auto it = resolution.headroomAudit.find (BusTarget::bass808);
                const double subGain = it != resolution.headroomAudit.end() ? it->second.cumulativeUncompensatedGainDb : 0.0;
                const bool subOverload = hasTelemetry && telemetry.subToMidDeltaDb > 9.0;
                if (inst.intensity >= 0.8 || subGain > 7.5 || subOverload)
                {
                    CritiqueAction a;
                    a.id = "critique_808_mud_saturation";
                    a.targetModuleId = "lowend_harmonic_stack";
                    a.targetBus = BusTarget::bass808;
                    a.issueDetected = "Low-end sub accumulation and harmonic congestion on the 808 bus";
                    a.measuredEvidence = "808 bus uncompensated gain +" + std::to_string (round1 (subGain)) + " dB";
                    a.previousIntensity = inst.intensity;
                    a.suggestedIntensity = std::max (0.45, round1 (inst.intensity - 0.2));
                    a.recommendedAdjustment = "Reduce lowend_harmonic_stack intensity to restore transient definition.";
                    actions.push_back (a);
                }
            }

            if (inst.moduleId == "vocal_clarity_chain")
            {
                bool hasParallelAir = false;
                for (const auto& m : instances)
                    if (m.moduleId == "parallel_air_gloss" && m.enabled)
                        hasParallelAir = true;
                const bool sibilanceHot = hasTelemetry && telemetry.sibilanceToMidDeltaDb > -4.5;
                if ((hasParallelAir && inst.intensity > 0.75) || sibilanceHot)
                {
                    CritiqueAction a;
                    a.id = "critique_vocal_upper_mid_bite";
                    a.targetModuleId = "vocal_clarity_chain";
                    a.targetBus = BusTarget::lead_vocal;
                    a.issueDetected = "Cumulative 3.8-10.5 kHz upper-mid and air brittle buildup";
                    a.measuredEvidence = hasTelemetry
                        ? "measured sibilance-to-mid ratio " + std::to_string (round1 (telemetry.sibilanceToMidDeltaDb)) + " dB"
                        : "stacked vocal shelf plus parallel air exciter";
                    a.previousIntensity = inst.intensity;
                    a.suggestedIntensity = std::max (0.5, round1 (inst.intensity - 0.15));
                    a.recommendedAdjustment = "Trim vocal_clarity_chain so parallel air sits without fatiguing bite.";
                    actions.push_back (a);
                }
            }

            if (inst.moduleId == "harmonic_authority")
            {
                auto it = resolution.headroomAudit.find (BusTarget::lead_vocal);
                const double vocalGain = it != resolution.headroomAudit.end() ? it->second.cumulativeUncompensatedGainDb : 0.0;
                const bool lowCrest = hasTelemetry && telemetry.crestFactorDb < 6.5;
                if (vocalGain > 4.0 || lowCrest)
                {
                    CritiqueAction a;
                    a.id = "critique_vocal_crest_density";
                    a.targetModuleId = "harmonic_authority";
                    a.targetBus = BusTarget::lead_vocal;
                    a.issueDetected = "Reduced vocal micro-dynamic crest factor from stacked parallel crush";
                    a.measuredEvidence = hasTelemetry
                        ? "measured crest factor " + std::to_string (round1 (telemetry.crestFactorDb)) + " dB"
                        : "lead vocal bus cumulative drive +" + std::to_string (round1 (vocalGain)) + " dB";
                    a.previousIntensity = inst.intensity;
                    a.suggestedIntensity = std::max (0.45, round1 (inst.intensity - 0.2));
                    a.recommendedAdjustment = "Dial back harmonic_authority to preserve consonant transients.";
                    actions.push_back (a);
                }
            }
        }

        return actions;
    }

    std::vector<MaskingDip> proposeMaskingDips (double sourceBodyHz,
                                                double siblingDangerHzLo,
                                                double siblingDangerHzHi,
                                                BusTarget bus,
                                                double sensitivity)
    {
        std::vector<MaskingDip> dips;
        const double sens = std::max (0.0, std::min (1.0, sensitivity));

        // If this instrument's body resonance falls inside a sibling's masking-danger band,
        // the sibling masks it. Propose a bounded dip at the collision frequency.
        if (sourceBodyHz >= siblingDangerHzLo && sourceBodyHz <= siblingDangerHzHi)
        {
            MaskingDip d;
            d.bus = bus;
            d.freqHz = std::round (sourceBodyHz);
            d.q = 1.8;
            d.maxCutDb = 6.0;                                   // technique-library bound
            d.suggestedCutDb = -round1 (std::min (d.maxCutDb, 2.0 + 2.0 * sens));
            d.reason = "body resonance " + std::to_string (static_cast<int> (sourceBodyHz))
                     + " Hz sits inside the sibling's masking-danger band ("
                     + std::to_string (static_cast<int> (siblingDangerHzLo)) + "-"
                     + std::to_string (static_cast<int> (siblingDangerHzHi)) + " Hz)";
            dips.push_back (d);
        }
        return dips;
    }
}
