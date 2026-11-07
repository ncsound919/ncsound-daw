#pragma once

#include "HostedInsert.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace nc
{
inline constexpr const char* builtinSynthIdentifier = "builtin-polysynth";

/** The built-in fallback instrument so MIDI clips make sound with no VST3
    installed. A JUCE Synthesiser with 16 voices; waveform, filter and ADSR
    come from a preset (Bass / Keys / Pad) stored in the slot state, so a
    project reopens silently without a scanner. Legacy state (a bare wave
    int) still loads. */
class BuiltinSynth final : public InsertProcessor
{
public:
    enum class Wave : int { sine = 0, triangle = 1, saw = 2 };

    struct Preset
    {
        Wave wave = Wave::saw;
        float cutoffHz = 4000.0f;    // lowpass, key-tracked from note pitch
        float attackMs = 5.0f;
        float decayMs = 120.0f;
        float sustain = 0.7f;        // 0..1 of peak
        float releaseMs = 180.0f;
        float level = 0.22f;
    };

    enum class PresetId : int { bass = 0, keys = 1, pad = 2 };

    BuiltinSynth();

    void prepare (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, const nc::engine::TransportBlock&) override;
    void process (juce::AudioBuffer<float>&, const nc::engine::TransportBlock&,
                  juce::MidiBuffer&) override;
    int latencySamples() const override { return 0; }
    void setBypassed (bool shouldBeBypassed) override { bypassed = shouldBeBypassed; }
    bool isBypassed() const override { return bypassed; }
    juce::String name() const override { return "PolySynth (Built-in)"; }
    bool getState (nc::engine::PluginSlotState&) const override;
    bool setState (const nc::engine::PluginSlotState&) override;

    void setPreset (PresetId id);
    void setWave (Wave newWave) { preset.wave = newWave; applyPresetToVoices(); }
    Wave getWave() const { return preset.wave; }
    Preset getPreset() const { return preset; }
    void setPresetParams (const Preset& params) { preset = params; applyPresetToVoices(); }

    static Preset presetFor (PresetId id);

private:
    struct Sound final : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    struct Voice final : public juce::SynthesiserVoice
    {
        Preset preset;
        double phase = 0.0;
        double baseFrequency = 440.0;
        double bendSemitones = 0.0;
        float envLevel = 0.0f;
        float filterState = 0.0f;
        int envStage = 0; // 0 attack, 1 decay, 2 sustain, 3 release
        bool noteHeld = false;

        bool canPlaySound (juce::SynthesiserSound* sound) override;
        void startNote (int midiNoteNumber, float velocity,
                        juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
        void stopNote (float velocity, bool allowTailOff) override;
        void pitchWheelMoved (int newPitchWheelValue) override;
        void controllerMoved (int controllerNumber, int newControllerValue) override;
        void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample,
                              int numSamples) override;
    };

    void applyPresetToVoices();

    juce::Synthesiser synth;
    Preset preset;
    PresetId presetId = PresetId::bass;
    bool bypassed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltinSynth)
};
} // namespace nc
