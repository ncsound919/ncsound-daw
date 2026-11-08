#include "TechniqueLibrary.h"

namespace mixology
{
    const char* busName (BusTarget b) noexcept
    {
        switch (b)
        {
            case BusTarget::lead_vocal: return "lead_vocal";
            case BusTarget::bgv:        return "bgv";
            case BusTarget::adlib:      return "adlib";
            case BusTarget::bass808:    return "808_bass";
            case BusTarget::drum_bus:   return "drum_bus";
            case BusTarget::master:     return "master";
        }
        return "master";
    }

    const std::vector<BusTarget>& allBuses() noexcept
    {
        static const std::vector<BusTarget> b {
            BusTarget::lead_vocal, BusTarget::bgv, BusTarget::adlib,
            BusTarget::bass808, BusTarget::drum_bus, BusTarget::master };
        return b;
    }

    namespace
    {
        using B = BusTarget;

        Technique makeTechnique (const char* id, const char* name, const char* signature,
                                 const char* category, std::vector<BusTarget> buses,
                                 int orderWeight, double gainDelta, double trim,
                                 const char* phaseImpact, const char* approx,
                                 double freqHz, double gainDb, double qFactor,
                                 const char* pluginType, const char* preferred,
                                 const char* fallback)
        {
            Technique t;
            t.id = id; t.name = name; t.signature = signature; t.category = category;
            t.applicableTo = std::move (buses);
            t.orderWeight = orderWeight;
            t.expectedGainDeltaDb = gainDelta;
            t.autoTrimCompensationDb = trim;
            t.phaseImpact = phaseImpact;
            t.dspApproxType = approx;
            t.dspFreqHz = freqHz; t.dspGainDb = gainDb; t.dspQFactor = qFactor;
            t.pluginType = pluginType; t.preferredPlugin = preferred; t.fallbackType = fallback;

            if (freqHz > 0.0)
                t.parameters["frequency"] = { freqHz, freqHz * 0.5, freqHz * 2.0, "Hz", "fixed_anchor" };
            if (gainDb != 0.0)
                t.parameters["gain"] = { gainDb, gainDb < 0 ? gainDb * 2.0 : 0.5,
                                         gainDb < 0 ? -1.0 : gainDb * 2.0, "dB", "proportional" };
            if (qFactor > 0.0)
                t.parameters["q_factor"] = { qFactor, 0.7, 4.0, "Q", "fixed_anchor" };
            return t;
        }

        std::vector<Technique> buildTechniques()
        {
            std::vector<Technique> v;
            v.push_back (makeTechnique ("vocal_deess_before_boost", "Pre-Boost Wideband De-Essing",
                "Vocal Clarity", "vocal_chain", { B::lead_vocal, B::bgv, B::adlib },
                1, -1.2, 0.0, "minimum_phase", "biquad_peaking", 7000, -4.0, 3.2,
                "de-esser", "Waves DeEsser", "any_deesser"));

            v.push_back (makeTechnique ("subtractive_eq_vocal", "Lower-Mid Boxiness Evacuation",
                "Vocal Clarity", "vocal_chain", { B::lead_vocal, B::bgv },
                2, -1.8, 1.5, "minimum_phase", "biquad_peaking", 320, -3.5, 1.8,
                "eq", "FabFilter Pro-Q 3", "parametric_eq"));

            v.push_back (makeTechnique ("presence_boost", "Air & Upper-Mid Shelf Presence",
                "Vocal Clarity", "vocal_chain", { B::lead_vocal },
                5, 2.2, -2.0, "minimum_phase", "biquad_highshelf", 3800, 3.0, 0.0,
                "eq", "Maag EQ4", "high_shelf_eq"));

            v.push_back (makeTechnique ("delay_flow_timed", "Timed Flow Delay",
                "Vocal Clarity", "spatial_routing", { B::lead_vocal, B::adlib },
                6, 0.4, 0.0, "none", "delay_feedback", 3500, -12.0, 0.0,
                "spatial", "Waves H-Delay", "stereo_delay"));

            v.push_back (makeTechnique ("loair_sub_enhance", "Sub-Harmonic Enhancement",
                "Vocal Clarity", "bass_808", { B::bass808 },
                4, 2.8, -2.5, "allpass_dispersion", "biquad_peaking", 45, 3.8, 1.2,
                "sub-harmonic", "Waves LoAir", "sub_harmonic_synth"));

            v.push_back (makeTechnique ("nls_8x_stack", "NLS 8x Console Stack",
                "Low-End Weight", "bass_808", { B::drum_bus, B::bass808 },
                4, 3.6, -3.2, "minimum_phase", "waveshaper_saturation", 0, 0.0, 0.0,
                "saturator", "Waves NLS", "analog_console_saturator_stack"));

            v.push_back (makeTechnique ("low_pass_rounded", "Rounded Master Low-Pass",
                "Low-End Weight", "master_bus", { B::master },
                7, -0.5, 0.5, "minimum_phase", "biquad_lowpass", 16800, 0.0, 0.85,
                "eq", "Pro-Q 3", "gentle_lowpass_eq"));

            v.push_back (makeTechnique ("uad_voice_of_god", "Voice Of God Resonant High-Pass",
                "Drum Anchor", "bass_808", { B::bass808, B::drum_bus },
                5, 3.1, -2.8, "allpass_dispersion", "biquad_peaking", 62, 4.2, 2.2,
                "eq", "UAD Voice Of God", "resonant_highpass_eq"));

            v.push_back (makeTechnique ("mono_sum_check", "Mono Sum Check",
                "Drum Anchor", "master_bus", { B::master, B::drum_bus },
                7, 0.6, -0.5, "none", "stereo_pan_width", 115, 1.2, 0.0,
                "utility", "Insight", "mono_sum_utility"));

            v.push_back (makeTechnique ("drum_first_balance", "Drum-First Balance",
                "Drum Anchor", "drum_bus", { B::drum_bus },
                3, 1.5, -1.5, "none", "dynamics_comp", 0, 2.0, 0.0,
                "compressor", "SSL Bus Comp", "vca_bus_compressor"));

            v.push_back (makeTechnique ("vocal_stack_compress", "Vocal Stack Compression",
                "Drum Anchor", "vocal_chain", { B::lead_vocal, B::bgv },
                3, 1.2, -1.0, "none", "dynamics_comp", 0, 1.5, 0.0,
                "compressor", "LA-2A", "optical_compressor"));

            v.push_back (makeTechnique ("clip_gain_unity", "Clip Gain Unity",
                "Subtractive Precision", "vocal_chain", { B::lead_vocal, B::bgv, B::adlib, B::bass808, B::drum_bus, B::master },
                1, 0.0, 0.0, "none", "dynamics_comp", 0, 0.8, 0.0,
                "utility", "Clip Gain", "gain_rider_utility"));

            v.push_back (makeTechnique ("surgical_f6_cuts", "Surgical F6 Notches",
                "Subtractive Precision", "vocal_chain", { B::lead_vocal, B::drum_bus },
                2, -2.0, 1.8, "minimum_phase", "biquad_peaking", 195, -4.5, 3.5,
                "eq", "Waves F6", "dynamic_eq"));

            v.push_back (makeTechnique ("vocal_compression_after_eq", "1176/LA-2A Vocal Compression",
                "Subtractive Precision", "vocal_chain", { B::lead_vocal },
                3, 1.8, -1.5, "none", "dynamics_comp", 0, 1.8, 0.0,
                "compressor", "1176 / LA-2A", "fet_into_opto_compressor"));

            v.push_back (makeTechnique ("aggressive_subtractive_eq", "Aggressive Subtractive EQ",
                "Vocal Emotion", "vocal_chain", { B::lead_vocal, B::bgv, B::adlib },
                2, -2.2, 2.0, "minimum_phase", "biquad_highpass_resonant", 110, -4.0, 0.9,
                "eq", "SSL Channel Strip", "channel_strip_eq"));

            v.push_back (makeTechnique ("waves_tune_realtime", "Real-Time Pitch Lock",
                "Vocal Emotion", "vocal_chain", { B::lead_vocal, B::bgv, B::adlib },
                1, 0.0, 0.0, "none", "biquad_peaking", 1200, 0.5, 2.0,
                "pitch", "Waves Tune", "realtime_pitch_corrector"));

            v.push_back (makeTechnique ("c6_multiband_compression", "C6 Multiband Compression",
                "Vocal Emotion", "vocal_chain", { B::lead_vocal },
                3, -1.4, 1.4, "allpass_dispersion", "biquad_peaking", 9500, 1.5, 1.0,
                "multiband", "Waves C6", "multiband_compressor"));

            v.push_back (makeTechnique ("bgv_pan_away", "BGV Pan Away",
                "Vocal Emotion", "spatial_routing", { B::bgv, B::adlib },
                6, 0.0, 0.0, "minimum_phase", "stereo_pan_width", 3000, -2.5, 0.0,
                "spatial", "M/S EQ", "mid_side_eq"));

            v.push_back (makeTechnique ("spl_deverb_wind_trick", "De-Verb Wind Trick",
                "Parallel Air", "instrument_keys_guitar", { B::bass808 },
                1, 1.4, -1.2, "none", "biquad_peaking", 750, 2.5, 1.6,
                "transient", "SPL Transient Designer", "envelope_tail_shaper"));

            v.push_back (makeTechnique ("parallel_air_exciter", "Parallel Air Exciter",
                "Parallel Air", "vocal_chain", { B::lead_vocal },
                5, 2.4, -2.0, "none", "biquad_highshelf", 10500, 3.2, 0.0,
                "saturator", "Aphex Exciter", "high_frequency_exciter"));

            v.push_back (makeTechnique ("quad_reverb_throw", "Quad Reverb Throw",
                "Spatial Depth", "spatial_routing", { B::lead_vocal, B::bgv },
                6, 0.6, -0.5, "none", "delay_feedback", 5200, -10.0, 0.0,
                "spatial", "Reverb Matrix", "multi_bus_reverb_matrix"));

            v.push_back (makeTechnique ("ssl_quad_bus_glue", "SSL Quad Bus Glue",
                "Spatial Depth", "drum_bus", { B::drum_bus },
                3, 2.1, -2.0, "minimum_phase", "biquad_peaking", 110, 2.4, 1.3,
                "compressor", "SSL Bus Comp", "transformer_vca_bus_chain"));

            v.push_back (makeTechnique ("parallel_harmonic_crush", "Parallel Harmonic Crush",
                "Harmonic Drive", "vocal_chain", { B::lead_vocal, B::bass808 },
                4, 2.5, -2.2, "minimum_phase", "waveshaper_saturation", 1800, 2.2, 0.0,
                "saturator", "Parallel FET Crush", "parallel_fet_crush"));

            return v;
        }
    }

    const std::vector<Technique>& techniques()
    {
        static const std::vector<Technique> v = buildTechniques();
        return v;
    }

    const Technique* findTechnique (const std::string& id)
    {
        for (const auto& t : techniques())
            if (t.id == id)
                return &t;
        return nullptr;
    }

    //==============================================================================
    const std::vector<TechniqueModule>& techniqueModules()
    {
        static const std::vector<TechniqueModule> m {
            { "lowend_harmonic_stack", "808 Weight & Harmonic Stack", "Low-End Weight", "808 weight and harmonic growl",
              B::bass808, { "spl_deverb_wind_trick", "nls_8x_stack", "uad_voice_of_god", "loair_sub_enhance" }, {}, 0.85 },
            { "vocal_clarity_chain", "Vocal Presence Chain", "Vocal Clarity", "forward, clean lead vocal",
              B::lead_vocal, { "vocal_deess_before_boost", "subtractive_eq_vocal", "presence_boost", "delay_flow_timed" }, {}, 0.8 },
            { "vocal_emotion_chain", "Vocal Emotion & Dynamic Sheen", "Vocal Emotion", "pitch-locked emotive vocal",
              B::lead_vocal, { "waves_tune_realtime", "aggressive_subtractive_eq", "c6_multiband_compression", "bgv_pan_away" }, {}, 0.75 },
            { "subtractive_foundation", "Subtractive Foundation", "Subtractive Precision", "surgical subtractive-first vocal",
              B::lead_vocal, { "clip_gain_unity", "surgical_f6_cuts", "vocal_compression_after_eq" }, {}, 0.9 },
            { "mono_drum_anchor", "Mono Weight & Drum Anchor", "Drum Anchor", "drum-first mono-forward balance",
              B::drum_bus, { "drum_first_balance", "vocal_stack_compress", "mono_sum_check" }, {}, 0.85 },
            { "master_contour", "Rounded Master & Drum Knock", "Master Contour", "warm rounded master contour",
              B::master, { "low_pass_rounded" }, {}, 0.7 },
            { "parallel_air_gloss", "Parallel Vocal Gloss", "Parallel Air", "parallel air gloss",
              B::lead_vocal, { "vocal_deess_before_boost", "parallel_air_exciter" }, {}, 0.65 },
            { "spatial_depth_chain", "3D Space & Drum Punch", "Spatial Depth", "spatial depth and glue",
              B::lead_vocal, { "ssl_quad_bus_glue", "quad_reverb_throw" }, {}, 0.75 },
            { "harmonic_authority", "Parallel Harmonic Crush", "Harmonic Drive", "parallel harmonic authority",
              B::lead_vocal, { "clip_gain_unity", "parallel_harmonic_crush" }, {}, 0.7 },
        };
        return m;
    }

    const TechniqueModule* findModule (const std::string& id)
    {
        for (const auto& mod : techniqueModules())
            if (mod.id == id)
                return &mod;
        return nullptr;
    }
}
