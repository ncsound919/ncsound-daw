#pragma once

#include <string>
#include <vector>

#include "TechniqueLibrary.h"

namespace mixology
{
    struct InstrumentBiquadBand
    {
        std::string bandLabel;
        std::string type;     // lowpass | highpass | peaking | lowshelf | highshelf
        double freqHz { 0.0 };
        double qFactor { 0.707 };
        double gainDb { 0.0 };
        std::string purpose;
    };

    struct InstrumentCompressorSpec
    {
        double thresholdDb { -18.0 };
        double ratio { 4.0 };
        double attackMs { 10.0 };
        double releaseMs { 100.0 };
        double makeupGainDb { 0.0 };
        std::string topologyNote;
    };

    struct InstrumentPreset
    {
        std::string id;
        std::string instrumentName;
        BusTarget targetBus { BusTarget::master };
        std::string sonicGoal;
        double bodyResonanceHz { 0.0 };
        double maskingDangerZoneHzLo { 0.0 };
        double maskingDangerZoneHzHi { 0.0 };
        double recommendedPanPosition { 0.0 };
        std::vector<InstrumentBiquadBand> eqBands;
        InstrumentCompressorSpec compressor;
        double saturationDrive { 0.0 };
    };

    const std::vector<InstrumentPreset>& instrumentPresets();
    const InstrumentPreset* findInstrumentPreset (const std::string& id);
}
