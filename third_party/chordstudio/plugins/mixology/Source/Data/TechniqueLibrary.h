#pragma once

#include <string>
#include <vector>
#include <map>

namespace mixology
{
    /// Buses used by the mixing assistant. Values mirror the TS reference; the enum
    /// identifiers cannot start with a digit, so `808_bass` is `bass808` here and
    /// `busName()` returns the reference spelling.
    enum class BusTarget { lead_vocal, bgv, adlib, bass808, drum_bus, master };

    const char* busName (BusTarget b) noexcept;
    const std::vector<BusTarget>& allBuses() noexcept;

    struct ParamSpec
    {
        double def { 0.0 };
        double min { 0.0 };
        double max { 0.0 };
        std::string unit;
        std::string scalingLaw { "proportional" };
    };

    /// One atomic mixing technique (a single plugin insert / move).
    struct Technique
    {
        std::string id;
        std::string name;
        std::string signature;
        std::string category;
        std::vector<BusTarget> applicableTo;
        std::vector<std::string> requiresTechniques;
        std::vector<std::string> conflictsWith;
        std::string pluginType;
        std::string preferredPlugin;
        std::string fallbackType;
        std::map<std::string, ParamSpec> parameters;
        int orderWeight { 0 };
        double expectedGainDeltaDb { 0.0 };
        double autoTrimCompensationDb { 0.0 };
        std::string phaseImpact { "none" };
        std::string dspApproxType { "none" };
        double dspFreqHz { 0.0 };
        double dspGainDb { 0.0 };
        double dspQFactor { 0.0 };
    };

    /// A named multi-technique signature chain.
    struct TechniqueModule
    {
        std::string id;
        std::string name;
        std::string signature;
        std::string purpose;
        BusTarget primaryBus { BusTarget::master };
        std::vector<std::string> chain;
        std::vector<std::string> conflictsWithModules;
        double defaultIntensity { 0.7 };
    };

    const std::vector<Technique>& techniques();
    const Technique* findTechnique (const std::string& id);

    const std::vector<TechniqueModule>& techniqueModules();
    const TechniqueModule* findModule (const std::string& id);
}
