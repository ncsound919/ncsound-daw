#pragma once

#include <map>
#include <string>
#include <vector>

#include "../Data/TechniqueLibrary.h"

namespace mixology
{
    struct SelectedModuleInstance
    {
        std::string moduleId;
        double intensity { 0.7 };
        bool enabled { true };
        bool hasTargetBusOverride { false };
        BusTarget targetBusOverride { BusTarget::master };
    };

    struct ScaledParam
    {
        double value { 0.0 };
        std::string unit;
        double originalDefault { 0.0 };
        std::string scalingLaw;
    };

    /// Scales a technique parameter deterministically from its explicit law and intensity.
    ScaledParam scaleParameter (const std::string& paramKey, const ParamSpec& spec, double intensity);

    struct ResolvedChainStep
    {
        int stepIndex { 0 };
        BusTarget bus { BusTarget::master };
        std::string techniqueId;
        std::string techniqueName;
        std::string signature;
        std::string sourceModuleId;
        std::string sourceModuleName;
        double intensity { 0.0 };
        std::map<std::string, ScaledParam> scaledParameters;
        int instanceCount { 1 };
        double netGainDeltaDb { 0.0 };
        double compensationTrimDb { 0.0 };
        std::string resolutionNote;
    };

    struct ConflictReport
    {
        std::string id;
        std::string type;       // ordering | frequency | philosophy | phase_coherence | gain_staging_headroom
        std::string severity;   // resolved_automatically | creative_split | warning
        std::vector<std::string> modulesInvolved;
        std::vector<std::string> techniquesInvolved;
        BusTarget bus { BusTarget::master };
        std::string description;
        std::string resolutionStrategy;
        std::string appliedAction;
    };

    struct BusHeadroomAudit
    {
        BusTarget bus { BusTarget::master };
        double cumulativeUncompensatedGainDb { 0.0 };
        double appliedAutoTrimDb { 0.0 };
        double estimatedPostChainMarginDb { -6.0 };
        std::string phaseStatus { "coherent" };
    };

    /// One real ChordStudio host operation (a mixer route or a headless command
    /// id). Parameter values are stringified exactly as the host's routes read
    /// them. This replaced the invented "insert_plugin"/"create_sidechain" RPCs.
    struct HostCommand
    {
        std::string method;                              // e.g. "addInsertFromCatalog"
        std::map<std::string, std::string> params;       // e.g. {"bus","track","plugin","purpose"}
    };

    struct ResolutionResult
    {
        std::map<BusTarget, std::vector<ResolvedChainStep>> stepsByBus;
        std::vector<ConflictReport> conflicts;
        std::vector<HostCommand> rpcPayload;
        std::map<BusTarget, BusHeadroomAudit> headroomAudit;
    };

    /// The signal-chain resolver: dedupes shared techniques across modules, applies the
    /// documented ordering / masking / philosophy / phase conflicts, and audits gain staging.
    ResolutionResult resolveSignalChain (const std::vector<SelectedModuleInstance>& instances);

    /// Post-hoc critique: proposes intensity reductions from headroom math + optional
    /// measured crest/sibilance. `hasTelemetry` gates the measured branches.
    struct CritiqueAction
    {
        std::string id;
        std::string targetModuleId;
        BusTarget targetBus { BusTarget::master };
        std::string issueDetected;
        std::string measuredEvidence;
        std::string recommendedAdjustment;
        double previousIntensity { 0.0 };
        double suggestedIntensity { 0.0 };
    };

    struct LiveTelemetry
    {
        double crestFactorDb { 12.0 };
        double peakDb { -3.0 };
        double rmsDb { -15.0 };
        double sibilanceToMidDeltaDb { -12.0 };
        double subToMidDeltaDb { 0.0 };
    };

    std::vector<CritiqueAction> evaluateMixCritique (const std::vector<SelectedModuleInstance>& instances,
                                                     const ResolutionResult& resolution,
                                                     bool hasTelemetry,
                                                     const LiveTelemetry& telemetry);

    /// Masking-based bounded EQ dip proposal (the honest core of the Neutron-style model):
    /// where a sibling's masking-danger band overlaps this instrument's band, propose a dip
    /// bounded by the technique library's max cut and scaled by sensitivity.
    struct MaskingDip
    {
        BusTarget bus { BusTarget::master };
        double freqHz { 0.0 };
        double q { 1.5 };
        double maxCutDb { 0.0 };
        double suggestedCutDb { 0.0 };
        std::string reason;
    };

    std::vector<MaskingDip> proposeMaskingDips (double sourceBodyHz,
                                                double siblingDangerHzLo,
                                                double siblingDangerHzHi,
                                                BusTarget bus,
                                                double sensitivity);
}
