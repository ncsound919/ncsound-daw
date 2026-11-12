#pragma once

#include <cstddef>

namespace nc::engine::studiofx
{
/** One selectable value of a choice parameter (ratio, mode, ...). */
struct FxParamChoice
{
    float value = 0.0f;
    const char* label = nullptr;
};

/** Range and default of one module parameter. Mirrors FxParamSpec. */
struct FxParamSpec
{
    const char* key = nullptr;
    const char* label = nullptr;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    float defaultValue = 0.0f;
    const char* unit = nullptr;
    const FxParamChoice* choices = nullptr;
    std::size_t numChoices = 0;
};

/** One insert module kind. Mirrors FxKindSpec. */
struct FxKindSpec
{
    const char* id = nullptr;
    const char* name = nullptr;
    const char* category = nullptr;
    const FxParamSpec* params = nullptr;
    std::size_t numParams = 0;
};

static constexpr FxParamChoice kChoices_TRIM_autoStage[] = {
    { 0.0f, "Off" },
    { 1.0f, "On" },
};
static constexpr FxParamSpec kParams_TRIM[] = {
    { "gainDb", "Gain", -24.0f, 24.0f, 0.0f, " dB", nullptr, 0 },
    { "autoStage", "Auto-stage to -18", 0.0f, 1.0f, 0.0f, "", kChoices_TRIM_autoStage, 2 },
};
static constexpr FxParamSpec kParams_FILTER[] = {
    { "hpHz", "HPF", 0.0f, 800.0f, 0.0f, " Hz", nullptr, 0 },
    { "lpHz", "LPF", 0.0f, 20000.0f, 0.0f, " Hz", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_LADDER_FILTER_mode[] = {
    { 0.0f, "Moog" },
    { 1.0f, "Juno" },
    { 2.0f, "Prophet" },
};
static constexpr FxParamSpec kParams_LADDER_FILTER[] = {
    { "cutoffHz", "Cutoff", 20.0f, 20000.0f, 1200.0f, " Hz", nullptr, 0 },
    { "resonance", "Resonance", 0.0f, 20.0f, 4.0f, "", nullptr, 0 },
    { "drive", "Drive", 0.0f, 10.0f, 0.0f, "", nullptr, 0 },
    { "mode", "Family", 0.0f, 2.0f, 0.0f, "", kChoices_LADDER_FILTER_mode, 3 },
};
static constexpr FxParamChoice kChoices_NEVE_1073_lfHz[] = {
    { 35.0f, "35 Hz" },
    { 60.0f, "60 Hz" },
    { 110.0f, "110 Hz" },
    { 220.0f, "220 Hz" },
};
static constexpr FxParamChoice kChoices_NEVE_1073_midHz[] = {
    { 360.0f, "360 Hz" },
    { 700.0f, "700 Hz" },
    { 1600.0f, "1.6 kHz" },
    { 3200.0f, "3.2 kHz" },
    { 4800.0f, "4.8 kHz" },
    { 7200.0f, "7.2 kHz" },
};
static constexpr FxParamSpec kParams_NEVE_1073[] = {
    { "driveDb", "Preamp drive", 0.0f, 24.0f, 3.0f, " dB", nullptr, 0 },
    { "hpfHz", "HPF", 50.0f, 300.0f, 50.0f, " Hz", nullptr, 0 },
    { "lfHz", "LF", 35.0f, 220.0f, 60.0f, " Hz", kChoices_NEVE_1073_lfHz, 4 },
    { "lfDb", "LF gain", -16.0f, 16.0f, 0.0f, " dB", nullptr, 0 },
    { "midHz", "Mid", 360.0f, 7200.0f, 1600.0f, " Hz", kChoices_NEVE_1073_midHz, 6 },
    { "midDb", "Mid gain", -18.0f, 18.0f, 0.0f, " dB", nullptr, 0 },
    { "hfDb", "HF 12 kHz", -16.0f, 16.0f, 0.0f, " dB", nullptr, 0 },
    { "outDb", "Output", -12.0f, 12.0f, 0.0f, " dB", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_SSL_CHANNEL_EQ_lfBell[] = {
    { 0.0f, "Shelf" },
    { 1.0f, "Bell" },
};
static constexpr FxParamChoice kChoices_SSL_CHANNEL_EQ_hfBell[] = {
    { 0.0f, "Shelf" },
    { 1.0f, "Bell" },
};
static constexpr FxParamSpec kParams_SSL_CHANNEL_EQ[] = {
    { "lfHz", "LF", 30.0f, 450.0f, 90.0f, " Hz", nullptr, 0 },
    { "lfDb", "LF gain", -18.0f, 18.0f, 0.0f, " dB", nullptr, 0 },
    { "lfBell", "LF shape", 0.0f, 1.0f, 0.0f, "", kChoices_SSL_CHANNEL_EQ_lfBell, 2 },
    { "lmfHz", "LMF", 200.0f, 2500.0f, 400.0f, " Hz", nullptr, 0 },
    { "lmfDb", "LMF gain", -18.0f, 18.0f, 0.0f, " dB", nullptr, 0 },
    { "lmfQ", "LMF Q", 0.5f, 4.0f, 1.0f, "", nullptr, 0 },
    { "hmfHz", "HMF", 600.0f, 7000.0f, 2500.0f, " Hz", nullptr, 0 },
    { "hmfDb", "HMF gain", -18.0f, 18.0f, 0.0f, " dB", nullptr, 0 },
    { "hmfQ", "HMF Q", 0.5f, 4.0f, 1.0f, "", nullptr, 0 },
    { "hfHz", "HF", 1500.0f, 16000.0f, 10000.0f, " Hz", nullptr, 0 },
    { "hfDb", "HF gain", -18.0f, 18.0f, 0.0f, " dB", nullptr, 0 },
    { "hfBell", "HF shape", 0.0f, 1.0f, 0.0f, "", kChoices_SSL_CHANNEL_EQ_hfBell, 2 },
    { "hpHz", "HPF 18 dB/oct", 0.0f, 300.0f, 0.0f, " Hz", nullptr, 0 },
    { "outDb", "Output", -18.0f, 18.0f, 0.0f, " dB", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_PULTEC_EQP1A_lowBoostHz[] = {
    { 20.0f, "20 Hz" },
    { 30.0f, "30 Hz" },
    { 60.0f, "60 Hz" },
    { 100.0f, "100 Hz" },
};
static constexpr FxParamChoice kChoices_PULTEC_EQP1A_lowAttenHz[] = {
    { 20.0f, "20 Hz" },
    { 30.0f, "30 Hz" },
    { 60.0f, "60 Hz" },
    { 100.0f, "100 Hz" },
};
static constexpr FxParamChoice kChoices_PULTEC_EQP1A_highBoostHz[] = {
    { 3000.0f, "3 kHz" },
    { 4000.0f, "4 kHz" },
    { 5000.0f, "5 kHz" },
    { 8000.0f, "8 kHz" },
    { 10000.0f, "10 kHz" },
    { 12000.0f, "12 kHz" },
    { 16000.0f, "16 kHz" },
};
static constexpr FxParamChoice kChoices_PULTEC_EQP1A_highAttenHz[] = {
    { 5000.0f, "5 kHz" },
    { 10000.0f, "10 kHz" },
    { 20000.0f, "20 kHz" },
};
static constexpr FxParamSpec kParams_PULTEC_EQP1A[] = {
    { "lowBoostHz", "Low boost", 20.0f, 100.0f, 60.0f, " Hz", kChoices_PULTEC_EQP1A_lowBoostHz, 4 },
    { "lowBoostDb", "Low boost amount", 0.0f, 13.5f, 0.0f, " dB", nullptr, 0 },
    { "lowAttenHz", "Low atten", 20.0f, 100.0f, 60.0f, " Hz", kChoices_PULTEC_EQP1A_lowAttenHz, 4 },
    { "lowAttenDb", "Low atten amount", 0.0f, 17.5f, 0.0f, " dB", nullptr, 0 },
    { "highBoostHz", "High boost", 3000.0f, 16000.0f, 10000.0f, " Hz", kChoices_PULTEC_EQP1A_highBoostHz, 7 },
    { "highBoostDb", "High boost amount", 0.0f, 18.0f, 0.0f, " dB", nullptr, 0 },
    { "highAttenHz", "High atten", 5000.0f, 20000.0f, 10000.0f, " Hz", kChoices_PULTEC_EQP1A_highAttenHz, 3 },
    { "highAttenDb", "High atten amount", 0.0f, 16.0f, 0.0f, " dB", nullptr, 0 },
    { "outDb", "Tube makeup", -6.0f, 12.0f, 0.0f, " dB", nullptr, 0 },
};
static constexpr FxParamSpec kParams_AIR_SHELF[] = {
    { "freqHz", "Frequency", 6000.0f, 16000.0f, 11000.0f, " Hz", nullptr, 0 },
    { "gainDb", "Air", -6.0f, 9.0f, 2.0f, " dB", nullptr, 0 },
};
static constexpr FxParamSpec kParams_MID_SIDE_EQ[] = {
    { "sideHpHz", "Side low cut", 0.0f, 400.0f, 120.0f, " Hz", nullptr, 0 },
    { "sideAirDb", "Side air", -6.0f, 6.0f, 0.0f, " dB", nullptr, 0 },
    { "width", "Width", 0.25f, 2.0f, 1.0f, "", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_FET_1176_ratio[] = {
    { 4.0f, "4:1" },
    { 8.0f, "8:1" },
    { 12.0f, "12:1" },
    { 20.0f, "20:1" },
};
static constexpr FxParamChoice kChoices_FET_1176_allButtons[] = {
    { 0.0f, "Off" },
    { 1.0f, "British" },
};
static constexpr FxParamSpec kParams_FET_1176[] = {
    { "inputDb", "Input drive", -6.0f, 24.0f, 6.0f, " dB", nullptr, 0 },
    { "attackUs", "Attack", 20.0f, 800.0f, 200.0f, " us", nullptr, 0 },
    { "releaseMs", "Release", 50.0f, 1100.0f, 250.0f, " ms", nullptr, 0 },
    { "ratio", "Ratio", 4.0f, 20.0f, 4.0f, ":1", kChoices_FET_1176_ratio, 4 },
    { "allButtons", "All buttons in", 0.0f, 1.0f, 0.0f, "", kChoices_FET_1176_allButtons, 2 },
    { "outputDb", "Output", -12.0f, 12.0f, 0.0f, " dB", nullptr, 0 },
    { "mix", "Mix", 0.0f, 1.0f, 1.0f, "", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_OPTO_LA2A_limitMode[] = {
    { 0.0f, "Compress" },
    { 1.0f, "Limit" },
};
static constexpr FxParamSpec kParams_OPTO_LA2A[] = {
    { "peakReduction", "Peak reduction", 0.0f, 10.0f, 4.0f, "", nullptr, 0 },
    { "gainDb", "Gain", 0.0f, 24.0f, 6.0f, " dB", nullptr, 0 },
    { "limitMode", "Mode", 0.0f, 1.0f, 0.0f, "", kChoices_OPTO_LA2A_limitMode, 2 },
    { "mix", "Mix", 0.0f, 1.0f, 1.0f, "", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_VCA_BUS_COMP_ratio[] = {
    { 2.0f, "2:1" },
    { 4.0f, "4:1" },
    { 10.0f, "10:1" },
};
static constexpr FxParamChoice kChoices_VCA_BUS_COMP_attackMs[] = {
    { 0.10000000149011612f, "0.1 ms" },
    { 0.30000001192092896f, "0.3 ms" },
    { 1.0f, "1 ms" },
    { 3.0f, "3 ms" },
    { 10.0f, "10 ms" },
    { 30.0f, "30 ms" },
};
static constexpr FxParamChoice kChoices_VCA_BUS_COMP_autoRelease[] = {
    { 0.0f, "Fixed" },
    { 1.0f, "Auto" },
};
static constexpr FxParamChoice kChoices_VCA_BUS_COMP_releaseMs[] = {
    { 100.0f, "100 ms" },
    { 300.0f, "300 ms" },
    { 600.0f, "600 ms" },
    { 1200.0f, "1.2 s" },
};
static constexpr FxParamSpec kParams_VCA_BUS_COMP[] = {
    { "thresholdDb", "Threshold", -30.0f, 0.0f, -10.0f, " dB", nullptr, 0 },
    { "ratio", "Ratio", 2.0f, 10.0f, 2.0f, ":1", kChoices_VCA_BUS_COMP_ratio, 3 },
    { "attackMs", "Attack", 0.10000000149011612f, 30.0f, 10.0f, " ms", kChoices_VCA_BUS_COMP_attackMs, 6 },
    { "autoRelease", "Auto release", 0.0f, 1.0f, 1.0f, "", kChoices_VCA_BUS_COMP_autoRelease, 2 },
    { "releaseMs", "Release", 100.0f, 1200.0f, 300.0f, " ms", kChoices_VCA_BUS_COMP_releaseMs, 4 },
    { "makeupDb", "Makeup", 0.0f, 12.0f, 0.0f, " dB", nullptr, 0 },
    { "mix", "Mix", 0.0f, 1.0f, 1.0f, "", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_VARI_MU_COMP_timeConstant[] = {
    { 0.0f, "TC1 0.2ms/0.3s" },
    { 1.0f, "TC2 0.2ms/0.8s" },
    { 2.0f, "TC3 0.4ms/2s" },
    { 3.0f, "TC4 0.8ms/5s" },
    { 4.0f, "TC5 prog 2/10s" },
    { 5.0f, "TC6 prog 0.3/25s" },
};
static constexpr FxParamSpec kParams_VARI_MU_COMP[] = {
    { "driveDb", "Input drive", 0.0f, 18.0f, 4.0f, " dB", nullptr, 0 },
    { "thresholdDb", "DC threshold", -30.0f, 0.0f, -12.0f, " dB", nullptr, 0 },
    { "ratio", "Ratio", 1.5f, 6.0f, 2.5f, ":1", nullptr, 0 },
    { "timeConstant", "Time constant", 0.0f, 5.0f, 2.0f, "", kChoices_VARI_MU_COMP_timeConstant, 6 },
    { "makeupDb", "Makeup", -12.0f, 12.0f, 0.0f, " dB", nullptr, 0 },
    { "mix", "Mix", 0.0f, 1.0f, 1.0f, "", nullptr, 0 },
};
static constexpr FxParamSpec kParams_PARALLEL_COMP[] = {
    { "thresholdDb", "Threshold", -40.0f, -4.0f, -22.0f, " dB", nullptr, 0 },
    { "ratio", "Ratio", 4.0f, 20.0f, 8.0f, ":1", nullptr, 0 },
    { "attackMs", "Attack", 1.0f, 30.0f, 5.0f, " ms", nullptr, 0 },
    { "releaseMs", "Release", 30.0f, 400.0f, 120.0f, " ms", nullptr, 0 },
    { "blend", "Blend", 0.0f, 1.0f, 0.3499999940395355f, "", nullptr, 0 },
    { "makeupDb", "Makeup", 0.0f, 18.0f, 6.0f, " dB", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_DE_ESSER_split[] = {
    { 0.0f, "Wideband" },
    { 1.0f, "Split band" },
};
static constexpr FxParamSpec kParams_DE_ESSER[] = {
    { "freqHz", "Frequency", 4000.0f, 12000.0f, 6000.0f, " Hz", nullptr, 0 },
    { "thresholdDb", "Threshold", -40.0f, 0.0f, -22.0f, " dB", nullptr, 0 },
    { "amountDb", "Max reduction", 0.0f, 12.0f, 3.0f, " dB", nullptr, 0 },
    { "split", "Mode", 0.0f, 1.0f, 1.0f, "", kChoices_DE_ESSER_split, 2 },
};
static constexpr FxParamSpec kParams_SIDECHAIN_DUCK[] = {
    { "thresholdDb", "Threshold", -40.0f, 0.0f, -20.0f, " dB", nullptr, 0 },
    { "ratio", "Ratio", 2.0f, 20.0f, 6.0f, ":1", nullptr, 0 },
    { "attackMs", "Attack", 1.0f, 50.0f, 5.0f, " ms", nullptr, 0 },
    { "releaseMs", "Release", 50.0f, 600.0f, 120.0f, " ms", nullptr, 0 },
    { "floorDb", "Duck floor", -24.0f, 0.0f, -12.0f, " dB", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_SATURATION_type[] = {
    { 0.0f, "Tape" },
    { 1.0f, "Tube" },
    { 2.0f, "Transformer" },
    { 3.0f, "Console" },
};
static constexpr FxParamChoice kChoices_SATURATION_oversample[] = {
    { 1.0f, "Off" },
    { 2.0f, "2x" },
    { 4.0f, "4x" },
};
static constexpr FxParamSpec kParams_SATURATION[] = {
    { "type", "Model", 0.0f, 3.0f, 0.0f, "", kChoices_SATURATION_type, 4 },
    { "driveDb", "Drive", 0.0f, 24.0f, 6.0f, " dB", nullptr, 0 },
    { "warmth", "Warmth", 0.0f, 1.0f, 0.4000000059604645f, "", nullptr, 0 },
    { "mix", "Mix", 0.0f, 1.0f, 1.0f, "", nullptr, 0 },
    { "oversample", "Oversample", 1.0f, 4.0f, 2.0f, "x", kChoices_SATURATION_oversample, 3 },
};
static constexpr FxParamChoice kChoices_TAPE_MACHINE_speedIps[] = {
    { 7.5f, "7.5 ips" },
    { 15.0f, "15 ips" },
    { 30.0f, "30 ips" },
};
static constexpr FxParamChoice kChoices_TAPE_MACHINE_bias[] = {
    { -1.0f, "Under" },
    { 0.0f, "Optimized" },
    { 1.0f, "Over" },
};
static constexpr FxParamChoice kChoices_TAPE_MACHINE_noise[] = {
    { 0.0f, "Off" },
    { 1.0f, "On" },
};
static constexpr FxParamSpec kParams_TAPE_MACHINE[] = {
    { "driveDb", "Record level", -6.0f, 12.0f, 0.0f, " dB", nullptr, 0 },
    { "speedIps", "Tape speed", 7.5f, 30.0f, 15.0f, " ips", kChoices_TAPE_MACHINE_speedIps, 3 },
    { "bias", "Bias", -1.0f, 1.0f, 0.0f, "", kChoices_TAPE_MACHINE_bias, 3 },
    { "wowFlutter", "Wow / flutter", 0.0f, 1.0f, 0.3499999940395355f, "", nullptr, 0 },
    { "noise", "Tape noise", 0.0f, 1.0f, 0.0f, "", kChoices_TAPE_MACHINE_noise, 2 },
    { "outDb", "Output", -6.0f, 6.0f, 0.0f, " dB", nullptr, 0 },
};
static constexpr FxParamSpec kParams_PLATE_REVERB[] = {
    { "decaySec", "Decay", 0.30000001192092896f, 6.0f, 1.399999976158142f, " s", nullptr, 0 },
    { "preDelayMs", "Pre-delay", 0.0f, 120.0f, 10.0f, " ms", nullptr, 0 },
    { "hpHz", "Low cut", 40.0f, 600.0f, 200.0f, " Hz", nullptr, 0 },
    { "lpHz", "High cut", 2000.0f, 16000.0f, 9000.0f, " Hz", nullptr, 0 },
    { "width", "Width", 0.0f, 1.0f, 0.8999999761581421f, "", nullptr, 0 },
    { "mix", "Mix", 0.0f, 1.0f, 0.25f, "", nullptr, 0 },
};
static constexpr FxParamSpec kParams_HALL_REVERB[] = {
    { "decaySec", "Decay", 0.30000001192092896f, 6.0f, 2.4000000953674316f, " s", nullptr, 0 },
    { "preDelayMs", "Pre-delay", 0.0f, 120.0f, 35.0f, " ms", nullptr, 0 },
    { "hpHz", "Low cut", 40.0f, 600.0f, 200.0f, " Hz", nullptr, 0 },
    { "lpHz", "High cut", 2000.0f, 16000.0f, 6500.0f, " Hz", nullptr, 0 },
    { "width", "Width", 0.0f, 1.0f, 0.8999999761581421f, "", nullptr, 0 },
    { "mix", "Mix", 0.0f, 1.0f, 0.25f, "", nullptr, 0 },
};
static constexpr FxParamSpec kParams_ROOM_REVERB[] = {
    { "decaySec", "Decay", 0.30000001192092896f, 6.0f, 0.800000011920929f, " s", nullptr, 0 },
    { "preDelayMs", "Pre-delay", 0.0f, 120.0f, 8.0f, " ms", nullptr, 0 },
    { "hpHz", "Low cut", 40.0f, 600.0f, 200.0f, " Hz", nullptr, 0 },
    { "lpHz", "High cut", 2000.0f, 16000.0f, 7500.0f, " Hz", nullptr, 0 },
    { "width", "Width", 0.0f, 1.0f, 0.8999999761581421f, "", nullptr, 0 },
    { "mix", "Mix", 0.0f, 1.0f, 0.25f, "", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_TEMPO_DELAY_division[] = {
    { 0.0f, "1/16" },
    { 1.0f, "1/8" },
    { 2.0f, "1/8 dot" },
    { 3.0f, "1/4" },
    { 4.0f, "1/4 dot" },
    { 5.0f, "1/2" },
    { 6.0f, "Slapback" },
};
static constexpr FxParamChoice kChoices_TEMPO_DELAY_ducking[] = {
    { 0.0f, "Off" },
    { 1.0f, "On" },
};
static constexpr FxParamChoice kChoices_TEMPO_DELAY_pingPong[] = {
    { 0.0f, "Stereo" },
    { 1.0f, "Ping-pong" },
};
static constexpr FxParamSpec kParams_TEMPO_DELAY[] = {
    { "division", "Division", 0.0f, 6.0f, 1.0f, "", kChoices_TEMPO_DELAY_division, 7 },
    { "timeMs", "Free time", 40.0f, 800.0f, 110.0f, " ms", nullptr, 0 },
    { "feedback", "Feedback", 0.0f, 0.8500000238418579f, 0.25f, "", nullptr, 0 },
    { "hpHz", "Feedback HPF", 60.0f, 800.0f, 200.0f, " Hz", nullptr, 0 },
    { "lpHz", "Feedback LPF", 1500.0f, 12000.0f, 4000.0f, " Hz", nullptr, 0 },
    { "ducking", "Ducking", 0.0f, 1.0f, 1.0f, "", kChoices_TEMPO_DELAY_ducking, 2 },
    { "pingPong", "Ping-pong", 0.0f, 1.0f, 0.0f, "", kChoices_TEMPO_DELAY_pingPong, 2 },
    { "mix", "Mix", 0.0f, 1.0f, 0.25f, "", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_CHORUS_WIDTH_mode[] = {
    { 0.0f, "Chorus I" },
    { 1.0f, "Chorus II" },
    { 2.0f, "Haas / Doubler" },
};
static constexpr FxParamSpec kParams_CHORUS_WIDTH[] = {
    { "mode", "Mode", 0.0f, 2.0f, 1.0f, "", kChoices_CHORUS_WIDTH_mode, 3 },
    { "rateHz", "Rate", 0.10000000149011612f, 6.0f, 0.800000011920929f, " Hz", nullptr, 0 },
    { "depthMs", "Depth", 0.5f, 12.0f, 3.5f, " ms", nullptr, 0 },
    { "width", "Width", 0.0f, 1.0f, 0.6000000238418579f, "", nullptr, 0 },
    { "mix", "Mix", 0.0f, 1.0f, 0.3499999940395355f, "", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_TRUE_PEAK_LIMITER_oversample[] = {
    { 1.0f, "Off" },
    { 2.0f, "2x" },
    { 4.0f, "4x" },
};
static constexpr FxParamSpec kParams_TRUE_PEAK_LIMITER[] = {
    { "ceilingDb", "Ceiling", -6.0f, 0.0f, -1.0f, " dBTP", nullptr, 0 },
    { "releaseMs", "Release", 10.0f, 1000.0f, 120.0f, " ms", nullptr, 0 },
    { "oversample", "Oversample", 1.0f, 4.0f, 4.0f, "x", kChoices_TRUE_PEAK_LIMITER_oversample, 3 },
};
static constexpr FxParamSpec kParams_CHANNEL_LIMITER[] = {
    { "ceilingDb", "Ceiling", -12.0f, 0.0f, -0.5f, " dB", nullptr, 0 },
    { "releaseMs", "Release", 5.0f, 300.0f, 30.0f, " ms", nullptr, 0 },
};
static constexpr FxParamChoice kChoices_PHONE_NOISE_REDUCTION_windDetection[] = {
    { 0.0f, "Off" },
    { 1.0f, "On" },
};
static constexpr FxParamChoice kChoices_PHONE_NOISE_REDUCTION_handlingDetection[] = {
    { 0.0f, "Off" },
    { 1.0f, "On" },
};
static constexpr FxParamSpec kParams_PHONE_NOISE_REDUCTION[] = {
    { "thresholdDb", "Threshold", -70.0f, -10.0f, -35.0f, " dB", nullptr, 0 },
    { "reductionDb", "Reduction", 0.0f, 24.0f, 12.0f, " dB", nullptr, 0 },
    { "smoothingMs", "Smoothing", 10.0f, 500.0f, 100.0f, " ms", nullptr, 0 },
    { "windDetection", "Wind detect", 0.0f, 1.0f, 1.0f, "", kChoices_PHONE_NOISE_REDUCTION_windDetection, 2 },
    { "handlingDetection", "Handling detect", 0.0f, 1.0f, 1.0f, "", kChoices_PHONE_NOISE_REDUCTION_handlingDetection, 2 },
};
static constexpr FxParamChoice kChoices_PHONE_MIC_EMULATION_targetMic[] = {
    { 0.0f, "U87" },
    { 1.0f, "SM7B" },
    { 2.0f, "C414" },
};
static constexpr FxParamSpec kParams_PHONE_MIC_EMULATION[] = {
    { "targetMic", "Target mic", 0.0f, 2.0f, 0.0f, "", kChoices_PHONE_MIC_EMULATION_targetMic, 3 },
    { "distanceMeters", "Distance", 0.10000000149011612f, 2.0f, 0.30000001192092896f, " m", nullptr, 0 },
    { "proximityAmount", "Proximity", 0.0f, 2.0f, 1.0f, "", nullptr, 0 },
    { "selfNoiseAmount", "Self-noise", 0.0f, 1.0f, 0.5f, "", nullptr, 0 },
    { "dynamicRangeExpansion", "Dyn range", 0.0f, 1.0f, 0.5f, "", nullptr, 0 },
};
static constexpr FxParamSpec kParams_VOCAL_RIDER[] = {
    { "targetLufs", "Target level", -30.0f, -8.0f, -18.0f, " LUFS", nullptr, 0 },
    { "maxGainDb", "Max boost", 0.0f, 12.0f, 5.0f, " dB", nullptr, 0 },
    { "minGainDb", "Max cut", -12.0f, 0.0f, -5.0f, " dB", nullptr, 0 },
    { "sensitivity", "Sensitivity", 0.0f, 1.0f, 0.550000011920929f, "", nullptr, 0 },
};
static constexpr FxParamSpec kParams_RESONANCE_SMOOTHER[] = {
    { "frequencyHz", "Frequency", 1000.0f, 8000.0f, 3200.0f, " Hz", nullptr, 0 },
    { "thresholdDb", "Threshold", -40.0f, 0.0f, -20.0f, " dB", nullptr, 0 },
    { "depthDb", "Max depth", 0.0f, 18.0f, 6.0f, " dB", nullptr, 0 },
    { "qFactor", "Q", 1.0f, 12.0f, 4.0f, "", nullptr, 0 },
};
static constexpr FxKindSpec kFxKinds[] = {
    { "TRIM", "Trim", "GAIN", kParams_TRIM, 2 },
    { "FILTER", "HPF / LPF", "FILTER", kParams_FILTER, 2 },
    { "LADDER_FILTER", "Ladder Filter", "FILTER", kParams_LADDER_FILTER, 4 },
    { "NEVE_1073", "Neve 1073 Pre/EQ", "FILTER", kParams_NEVE_1073, 8 },
    { "SSL_CHANNEL_EQ", "SSL 4000 E Channel EQ", "FILTER", kParams_SSL_CHANNEL_EQ, 14 },
    { "PULTEC_EQP1A", "Pultec EQP-1A", "FILTER", kParams_PULTEC_EQP1A, 9 },
    { "AIR_SHELF", "Air Shelf", "FILTER", kParams_AIR_SHELF, 2 },
    { "MID_SIDE_EQ", "Mid-Side EQ", "MODULATION", kParams_MID_SIDE_EQ, 3 },
    { "FET_1176", "1176 FET Compressor", "DYNAMICS", kParams_FET_1176, 7 },
    { "OPTO_LA2A", "LA-2A Opto Leveler", "DYNAMICS", kParams_OPTO_LA2A, 4 },
    { "VCA_BUS_COMP", "SSL Bus Compressor", "DYNAMICS", kParams_VCA_BUS_COMP, 7 },
    { "VARI_MU_COMP", "Vari-Mu Tube Compressor", "DYNAMICS", kParams_VARI_MU_COMP, 6 },
    { "PARALLEL_COMP", "Parallel Compressor", "DYNAMICS", kParams_PARALLEL_COMP, 6 },
    { "DE_ESSER", "De-Esser", "DYNAMICS", kParams_DE_ESSER, 4 },
    { "SIDECHAIN_DUCK", "Sidechain Duck", "DYNAMICS", kParams_SIDECHAIN_DUCK, 5 },
    { "SATURATION", "Saturation", "COLOR", kParams_SATURATION, 5 },
    { "TAPE_MACHINE", "Tape Machine", "COLOR", kParams_TAPE_MACHINE, 6 },
    { "PLATE_REVERB", "Plate Reverb", "SPACE", kParams_PLATE_REVERB, 6 },
    { "HALL_REVERB", "Hall Reverb", "SPACE", kParams_HALL_REVERB, 6 },
    { "ROOM_REVERB", "Room Reverb", "SPACE", kParams_ROOM_REVERB, 6 },
    { "TEMPO_DELAY", "Tempo Delay", "SPACE", kParams_TEMPO_DELAY, 8 },
    { "CHORUS_WIDTH", "Chorus / Width", "MODULATION", kParams_CHORUS_WIDTH, 5 },
    { "TRUE_PEAK_LIMITER", "True Peak Limiter", "LIMITER", kParams_TRUE_PEAK_LIMITER, 3 },
    { "CHANNEL_LIMITER", "Channel Limiter", "LIMITER", kParams_CHANNEL_LIMITER, 2 },
    { "PHONE_NOISE_REDUCTION", "Phone Noise Reduction", "PHONE", kParams_PHONE_NOISE_REDUCTION, 5 },
    { "PHONE_MIC_EMULATION", "Phone Mic Emulation", "PHONE", kParams_PHONE_MIC_EMULATION, 5 },
    { "VOCAL_RIDER", "Vocal Rider", "DYNAMICS", kParams_VOCAL_RIDER, 4 },
    { "RESONANCE_SMOOTHER", "Resonance Smoother", "DYNAMICS", kParams_RESONANCE_SMOOTHER, 4 },
};

inline constexpr std::size_t kNumFxKinds = sizeof (kFxKinds) / sizeof (kFxKinds[0]);

} // namespace nc::engine::studiofx
