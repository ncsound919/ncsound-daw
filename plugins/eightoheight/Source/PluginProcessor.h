#pragma once

#include <nc/engine/AnalogCharacter.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace nc
{
/** Parameter ids, shared by the processor, the editor and the tests. */
namespace eightoheight
{
inline constexpr int numSubPartials = 5; // 2x..6x of the fundamental

inline constexpr const char* wave = "a_wave";
inline constexpr const char* pitchAttack = "a_pitch_atk";
inline constexpr const char* pitchDecay = "a_pitch_dec";
inline constexpr const char* pitchCurve = "a_pitch_curve";
inline constexpr const char* pitchTime = "a_pitch_time";
inline constexpr const char* ampDecay = "a_decay";
inline constexpr const char* sustain = "a_sustain";
inline constexpr const char* level = "a_level";

inline constexpr const char* subLevel = "c_level";
inline constexpr const char* subDamping = "c_damping";
inline constexpr const char* subTilt = "c_tilt";

inline constexpr const char* glideTime = "g_glide_time";
inline constexpr const char* glideMode = "g_glide_mode";
inline constexpr const char* voiceLimit = "g_voices";

inline constexpr const char* monoLock = "m_lock_on";
inline constexpr const char* monoCrossover = "m_xover";

inline constexpr const char* subsonicFilter = "out_sub_filter";
inline constexpr const char* clipCeiling = "out_clip";
inline constexpr const char* outputLevel = "out_level";

inline constexpr const char* characterOn = "pc_on";
inline constexpr const char* characterCircuit = "pc_circuit";
inline constexpr const char* characterDrive = "pc_drive";
inline constexpr const char* characterIron = "pc_iron";
inline constexpr const char* characterTone = "pc_tone";
inline constexpr const char* characterOutput = "pc_out";
} // namespace eightoheight

/** 808 Designer.

    Signal flow is the design pack's: per voice a pitch-enveloped synth layer
    and a sub-harmonic stack (2x..6x partials with a damping-weighted spectral
    envelope), summed, then the mono-lock, the subsonic filter, the clipper, and
    finally the analog character section every stock instrument carries.

    The voice pool is hand-rolled rather than a juce::Synthesiser because 808s
    want legato glide over an overlapping note and an amp decay that keeps
    running while the key is held.
*/
class EightOHeightProcessor final : public juce::AudioProcessor
{
public:
    enum class Wave : int { sine = 0, triangle = 1, saw = 2, square = 3, supersaw = 4 };

    EightOHeightProcessor();
    ~EightOHeightProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "808 Designer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 4; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Envelope settings latched once per block so the audio loop is branch-free
        on atomics. */
    struct BlockParams
    {
        Wave wave = Wave::sine;
        float pitchAttackSt = 12.0f;
        float pitchDecaySt = -24.0f;
        float pitchCurve = 0.7f;
        float pitchTimeMs = 350.0f;
        float ampDecayMs = 1200.0f;
        float sustain = 0.0f;
        float levelDb = 0.0f;

        float subLevel = 0.0f;
        float subDamping = 0.4f;
        float subTilt = 0.0f;

        float glideMs = 0.0f;
        bool glideConstantRate = false;

        bool monoLock = true;
        float monoCrossoverHz = 120.0f;

        float subsonicHz = 30.0f;
        float clipCeilingDb = -1.0f;
        float outputDb = 0.0f;
    };

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Exposed for the headless tests.
    int activeVoiceCount() const;
    BlockParams blockParamsForTest() const { return latched; }

private:
    struct Voice
    {
        bool active = false;
        bool held = false;
        int note = -1;
        float velocity = 1.0f;

        double frequency = 55.0;      // current, after glide
        double targetFrequency = 55.0;
        double phase = 0.0;
        double supersawPhase = 0.0;
        std::array<double, eightoheight::numSubPartials> subPhase {};

        float pitchOffset = 0.0f;     // semitones from the pitch envelope
        bool pitchAttackStage = true;
        float pitchElapsedMs = 0.0f;

        float amp = 0.0f;
        bool releasing = false;
        float releaseGain = 0.0f;
    };

    void startNote (int midiNote, float velocity, bool legato);
    void stopNote (int midiNote);
    void allNotesOff (bool allowTail);
    void renderVoices (juce::AudioBuffer<float>&, int startSample, int numSamples);
    void latchParams();
    std::atomic<float>* raw (const char* id) const;

    std::array<Voice, 8> voices {};
    engine::AnalogCharacter character;
    BlockParams latched;

    std::atomic<float>* waveParam = nullptr;
    std::atomic<float>* pitchAttackParam = nullptr;
    std::atomic<float>* pitchDecayParam = nullptr;
    std::atomic<float>* pitchCurveParam = nullptr;
    std::atomic<float>* pitchTimeParam = nullptr;
    std::atomic<float>* ampDecayParam = nullptr;
    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* levelParam = nullptr;
    std::atomic<float>* subLevelParam = nullptr;
    std::atomic<float>* subDampingParam = nullptr;
    std::atomic<float>* subTiltParam = nullptr;
    std::atomic<float>* glideTimeParam = nullptr;
    std::atomic<float>* glideModeParam = nullptr;
    std::atomic<float>* voicesParam = nullptr;
    std::atomic<float>* monoLockParam = nullptr;
    std::atomic<float>* monoCrossoverParam = nullptr;
    std::atomic<float>* subsonicParam = nullptr;
    std::atomic<float>* clipParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* characterOnParam = nullptr;
    std::atomic<float>* characterCircuitParam = nullptr;
    std::atomic<float>* characterDriveParam = nullptr;
    std::atomic<float>* characterIronParam = nullptr;
    std::atomic<float>* characterToneParam = nullptr;
    std::atomic<float>* characterOutputParam = nullptr;

    double sampleRate = 48000.0;
    int maxBlock = 512;
    int currentProgram = 0;
    int nextVoice = 0;

    // Filter state: subsonic high-pass per channel and the mono-lock low band.
    std::array<float, 2> highpassState {};
    std::array<float, 2> monoLowState {};
    float subsonicCoefficient = 0.99f;
    float monoLowCoefficient = 0.02f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EightOHeightProcessor)
};
} // namespace nc
