#include "InstrumentPresets.h"

namespace mixology
{
    namespace
    {
        std::vector<InstrumentPreset> buildPresets()
        {
            using B = BusTarget;
            std::vector<InstrumentPreset> v;

            auto band = [] (const char* label, const char* type, double f, double q, double g, const char* purpose)
            {
                InstrumentBiquadBand b;
                b.bandLabel = label; b.type = type; b.freqHz = f; b.qFactor = q; b.gainDb = g; b.purpose = purpose;
                return b;
            };

            InstrumentPreset lead;
            lead.id = "inst_preset_lead_vocal";
            lead.instrumentName = "Lead Vocal (Rap / R&B / Pop)";
            lead.targetBus = B::lead_vocal;
            lead.sonicGoal = "Forward, detailed, sibilance-controlled lead vocal";
            lead.bodyResonanceHz = 240;
            lead.maskingDangerZoneHzLo = 220; lead.maskingDangerZoneHzHi = 480;
            lead.recommendedPanPosition = 0.0;
            lead.eqBands = {
                band ("Rumble High-Pass", "highpass", 95, 0.707, 0.0, "remove sub-vocal rumble"),
                band ("Room Mode Notch", "peaking", 195, 3.2, -4.2, "Subtractive Precision room-mode notch"),
                band ("Low-Mid Cloud Cut", "peaking", 320, 1.8, -3.5, "Vocal Clarity low-mid cloud"),
                band ("Sibilance Guard Notch", "peaking", 7000, 3.5, -4.0, "de-ess before presence"),
                band ("Presence & Air Shelf", "highshelf", 3800, 0.707, 3.2, "presence shelf"),
            };
            lead.compressor = { -18.0, 4.0, 8.0, 65.0, 0.0, "FET into opto" };
            v.push_back (lead);

            InstrumentPreset sub;
            sub.id = "inst_preset_808_sub";
            sub.instrumentName = "808 Sub-Bass & Synth Bass";
            sub.targetBus = B::bass808;
            sub.sonicGoal = "Deep, controlled 808 with upper-harmonic knock";
            sub.bodyResonanceHz = 62;
            sub.maskingDangerZoneHzLo = 220; sub.maskingDangerZoneHzHi = 400;
            sub.recommendedPanPosition = 0.0;
            sub.eqBands = {
                band ("Infrasonic Waste Guard", "highpass", 28, 0.707, 0.0, "remove unusable infrasonic energy"),
                band ("Voice Of God Bloom", "peaking", 62, 2.1, 4.2, "Drum Anchor resonant low bloom"),
                band ("Kick & Vocal Pocket Carve", "peaking", 285, 2.0, -3.8, "unmask kick and vocal"),
                band ("Upper-Harmonic Bite", "peaking", 750, 1.5, 2.8, "audible knock on small speakers"),
            };
            sub.compressor = { -16.0, 3.5, 25.0, 90.0, 0.0, "slow opto" };
            v.push_back (sub);

            InstrumentPreset drums;
            drums.id = "inst_preset_drum_bus";
            drums.instrumentName = "Kick & Snare Drum Bus";
            drums.targetBus = B::drum_bus;
            drums.sonicGoal = "Punchy, cohesive drum bus with crack";
            drums.bodyResonanceHz = 110;
            drums.maskingDangerZoneHzLo = 350; drums.maskingDangerZoneHzHi = 550;
            drums.recommendedPanPosition = 0.0;
            drums.eqBands = {
                band ("Punch Shelf", "lowshelf", 110, 0.85, 2.6, "Neve 1073 punch"),
                band ("Cardboard Mud Cut", "peaking", 420, 2.2, -3.2, "remove boxiness"),
                band ("Transient Crack", "peaking", 1600, 1.4, 2.8, "snare crack and beater"),
            };
            drums.compressor = { -15.0, 4.0, 30.0, 100.0, 0.0, "VCA bus glue" };
            v.push_back (drums);

            InstrumentPreset keys;
            keys.id = "inst_preset_piano_rhodes";
            keys.instrumentName = "Acoustic Grand Piano & Fender Rhodes";
            keys.targetBus = B::bgv;
            keys.sonicGoal = "Warm keys that sit behind the vocal";
            keys.bodyResonanceHz = 280;
            keys.maskingDangerZoneHzLo = 240; keys.maskingDangerZoneHzHi = 420;
            keys.recommendedPanPosition = -0.25;
            keys.eqBands = {
                band ("Sub-Bass Roll-Off", "highpass", 85, 0.707, 0.0, "clear space for bass"),
                band ("Vocal Pocket Carve", "peaking", 280, 1.9, -3.6, "unmask vocal warmth"),
                band ("Hammer & Tine Lift", "peaking", 2800, 1.4, 2.4, "presence for felt/tine"),
                band ("Top Velvet Air", "highshelf", 8500, 0.707, 1.8, "air without brittleness"),
            };
            keys.compressor = { -19.0, 2.5, 18.0, 120.0, 0.0, "gentle opto" };
            v.push_back (keys);

            InstrumentPreset guitar;
            guitar.id = "inst_preset_electric_guitar";
            guitar.instrumentName = "Electric Guitar (Clean / R&B / Funk Pluck)";
            guitar.targetBus = B::bgv;
            guitar.sonicGoal = "Percussive clean guitar that yields to the vocal";
            guitar.bodyResonanceHz = 340;
            guitar.maskingDangerZoneHzLo = 2800; guitar.maskingDangerZoneHzHi = 4200;
            guitar.recommendedPanPosition = 0.35;
            guitar.eqBands = {
                band ("Cabinet Thump High-Pass", "highpass", 130, 0.75, 0.0, "remove cab rumble"),
                band ("Pick Attack Bark", "peaking", 1850, 1.6, 2.5, "funk pluck definition"),
                band ("Presence Pocket Dip", "peaking", 3400, 2.4, -2.8, "unmask vocal consonants"),
            };
            guitar.compressor = { -17.0, 3.5, 12.0, 75.0, 0.0, "fast FET for attack" };
            v.push_back (guitar);

            InstrumentPreset pad;
            pad.id = "inst_preset_synth_pad";
            pad.instrumentName = "Analog Polysynth & Lush Pad";
            pad.targetBus = B::adlib;
            pad.sonicGoal = "Wide, warm pad glued under the arrangement";
            pad.bodyResonanceHz = 400;
            pad.maskingDangerZoneHzLo = 180; pad.maskingDangerZoneHzHi = 500;
            pad.recommendedPanPosition = 0.45;
            pad.eqBands = {
                band ("Bass Lane High-Pass", "highpass", 115, 0.707, 0.0, "keep the low lane clear"),
                band ("Sawtooth Cloud Cut", "peaking", 400, 1.5, -4.0, "reduce low-mid density"),
                band ("Analog Top Contour", "lowpass", 14500, 0.8, 0.0, "warm analog roll-off"),
            };
            pad.compressor = { -20.0, 2.0, 35.0, 180.0, 0.0, "slow and gentle" };
            v.push_back (pad);

            return v;
        }
    }

    const std::vector<InstrumentPreset>& instrumentPresets()
    {
        static const std::vector<InstrumentPreset> v = buildPresets();
        return v;
    }

    const InstrumentPreset* findInstrumentPreset (const std::string& id)
    {
        for (const auto& p : instrumentPresets())
            if (p.id == id)
                return &p;
        return nullptr;
    }
}
