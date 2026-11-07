#pragma once

#include "HostedInsert.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace nc
{
inline constexpr const char* builtinSynthIdentifier = "builtin-polysynth";

/** The built-in fallback instrument so MIDI clips make sound with no VST3
    installed. A JUCE Synthesiser with 8 voices; the waveform (sine /
    triangle / saw) is the only parameter and is stored as the slot state,
    so a project reopens silently without a scanner. */
class BuiltinSynth final : public InsertProcessor
{
public:
    enum class Wave : int { sine = 0, triangle = 1, saw = 2 };

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

    void setWave (Wave newWave) { wave = newWave; }
    Wave getWave() const { return wave; }

private:
    struct Sound final : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    struct Voice final : public juce::SynthesiserVoice
    {
        Wave wave = Wave::sine;
        double phase = 0.0;
        double frequency = 440.0;
        float level = 0.0f;      // current envelope value
        float targetLevel = 0.0f;
        bool releasing = false;

        bool canPlaySound (juce::SynthesiserSound*) override { return true; }
        void startNote (int midiNoteNumber, float velocity,
                        juce::SynthesiserSound*, int) override;
        void stopNote (float, bool) override { releasing = true; }
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;
    };

    juce::Synthesiser synth;
    Wave wave = Wave::sine;
    bool bypassed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltinSynth)
};
} // namespace nc
