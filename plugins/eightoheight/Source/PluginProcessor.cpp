#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace nc
{
namespace
{
constexpr int maxVoiceCount = 8;

juce::StringArray waveNames()
{
    return { "Sine", "Triangle", "Saw", "Square", "Supersaw" };
}

juce::StringArray circuitNames()
{
    juce::StringArray names;

    for (int i = 0; i < engine::numCharacterCircuits; ++i)
        names.add (engine::characterCircuitName (static_cast<engine::CharacterCircuit> (i)));

    return names;
}

/** PolyBLEP correction for the discontinuous waveforms, so a saw or square 808
    does not fold aliases into the sub band where the mono-lock works. */
float polyBlep (double t, double dt) noexcept
{
    if (t < dt)
    {
        const auto x = t / dt;
        return static_cast<float> (x + x - x * x - 1.0);
    }

    if (t > 1.0 - dt)
    {
        const auto x = (t - 1.0) / dt;
        return static_cast<float> (x * x + x + x + 1.0);
    }

    return 0.0f;
}

float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }
} // namespace

EightOHeightProcessor::EightOHeightProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createLayout())
{
    waveParam = raw (eightoheight::wave);
    pitchAttackParam = raw (eightoheight::pitchAttack);
    pitchDecayParam = raw (eightoheight::pitchDecay);
    pitchCurveParam = raw (eightoheight::pitchCurve);
    pitchTimeParam = raw (eightoheight::pitchTime);
    ampDecayParam = raw (eightoheight::ampDecay);
    sustainParam = raw (eightoheight::sustain);
    levelParam = raw (eightoheight::level);
    subLevelParam = raw (eightoheight::subLevel);
    subDampingParam = raw (eightoheight::subDamping);
    subTiltParam = raw (eightoheight::subTilt);
    glideTimeParam = raw (eightoheight::glideTime);
    glideModeParam = raw (eightoheight::glideMode);
    voicesParam = raw (eightoheight::voiceLimit);
    monoLockParam = raw (eightoheight::monoLock);
    monoCrossoverParam = raw (eightoheight::monoCrossover);
    subsonicParam = raw (eightoheight::subsonicFilter);
    clipParam = raw (eightoheight::clipCeiling);
    outputParam = raw (eightoheight::outputLevel);
    characterOnParam = raw (eightoheight::characterOn);
    characterCircuitParam = raw (eightoheight::characterCircuit);
    characterDriveParam = raw (eightoheight::characterDrive);
    characterIronParam = raw (eightoheight::characterIron);
    characterToneParam = raw (eightoheight::characterTone);
    characterOutputParam = raw (eightoheight::characterOutput);
}

juce::AudioProcessorValueTreeState::ParameterLayout EightOHeightProcessor::createLayout()
{
    using namespace eightoheight;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto floatParam = [&layout] (const char* id, const char* name,
                                 juce::NormalisableRange<float> range, float defaultValue,
                                 const char* suffix = "")
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes().withLabel (suffix)));
    };

    auto boolParam = [&layout] (const char* id, const char* name, bool defaultValue)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id, 1 }, name, defaultValue));
    };

    // Layer A — the synth voice.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { wave, 1 }, "Wave", waveNames(), 0));

    floatParam (pitchAttack, "Pitch Attack", { -24.0f, 24.0f, 0.01f }, 12.0f, "st");
    floatParam (pitchDecay, "Pitch Decay", { -24.0f, 24.0f, 0.01f }, -24.0f, "st");
    floatParam (pitchCurve, "Pitch Curve", { 0.05f, 1.0f, 0.001f }, 0.7f);
    floatParam (pitchTime, "Pitch Time", { 5.0f, 4000.0f, 0.1f }, 350.0f, "ms");
    floatParam (ampDecay, "Amp Decay", { 10.0f, 8000.0f, 0.1f }, 1200.0f, "ms");
    floatParam (sustain, "Sustain", { 0.0f, 1.0f, 0.001f }, 0.0f);
    floatParam (level, "Level", { -60.0f, 0.0f, 0.01f }, 0.0f, "dB");

    // Layer C — the sub-harmonic stack.
    floatParam (subLevel, "Sub Level", { 0.0f, 1.0f, 0.001f }, 0.0f);
    floatParam (subDamping, "Sub Damping", { 0.0f, 1.0f, 0.001f }, 0.4f);
    floatParam (subTilt, "Sub Tilt", { -100.0f, 100.0f, 0.1f }, 0.0f, "%");

    // Glide.
    floatParam (glideTime, "Glide Time", { 0.0f, 2000.0f, 0.1f }, 80.0f, "ms");
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { glideMode, 1 }, "Glide Mode",
        juce::StringArray { "Const Time", "Const Rate" }, 0));
    floatParam (voiceLimit, "Voices", { 1.0f, static_cast<float> (maxVoiceCount), 1.0f },
                static_cast<float> (maxVoiceCount));

    // Mono-lock.
    boolParam (monoLock, "Mono Lock", true);
    floatParam (monoCrossover, "Mono Crossover", { 40.0f, 300.0f, 0.1f }, 120.0f, "Hz");

    // Output.
    floatParam (subsonicFilter, "Subsonic", { 20.0f, 80.0f, 0.1f }, 30.0f, "Hz");
    floatParam (clipCeiling, "Clipper", { -6.0f, 0.0f, 0.01f }, -1.0f, "dBTP");
    floatParam (outputLevel, "Output", { -24.0f, 12.0f, 0.01f }, 0.0f, "dB");

    // The analog character section every stock instrument carries.
    boolParam (characterOn, "Character", false);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { characterCircuit, 1 }, "Circuit", circuitNames(), 0));
    floatParam (characterDrive, "Char Drive", { 0.0f, 24.0f, 0.01f }, 6.0f, "dB");
    floatParam (characterIron, "Char Iron", { 0.0f, 1.0f, 0.001f }, 0.3f);
    floatParam (characterTone, "Char Tone", { -1.0f, 1.0f, 0.001f }, 0.0f);
    floatParam (characterOutput, "Char Out", { -24.0f, 12.0f, 0.01f }, 0.0f, "dB");

    return layout;
}

std::atomic<float>* EightOHeightProcessor::raw (const char* id) const
{
    return apvts.getRawParameterValue (id);
}

void EightOHeightProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxBlock = juce::jmax (1, samplesPerBlock);

    voices.fill ({});
    nextVoice = 0;

    highpassState.fill (0.0f);
    monoLowState.fill (0.0f);

    const auto subsonic = subsonicParam != nullptr ? subsonicParam->load() : 30.0f;
    // One-pole DC blocker: the pole coefficient is exp (-2*pi*fc/fs).
    // 1 - exp (...) is the complementary *lowpass* coefficient, and putting it
    // in this recursion turns the stage into a differentiator that mutes the
    // voice: a 55 Hz tone came out at -90 dB before this was corrected.
    subsonicCoefficient = static_cast<float> (
        std::exp (-2.0 * juce::MathConstants<double>::pi * subsonic / sampleRate));

    const auto crossover = monoCrossoverParam != nullptr ? monoCrossoverParam->load() : 120.0f;
    monoLowCoefficient = 1.0f - static_cast<float> (
        std::exp (-2.0 * juce::MathConstants<double>::pi * crossover / sampleRate));

    character.prepare (sampleRate);
    latchParams();
    character.setSettings ({ characterOnParam != nullptr && characterOnParam->load() > 0.5f,
                             static_cast<engine::CharacterCircuit> (
                                 static_cast<int> (characterCircuitParam != nullptr ? characterCircuitParam->load() : 0.0f)),
                             characterDriveParam != nullptr ? characterDriveParam->load() : 6.0f,
                             characterIronParam != nullptr ? characterIronParam->load() : 0.3f,
                             characterToneParam != nullptr ? characterToneParam->load() : 0.0f,
                             characterOutputParam != nullptr ? characterOutputParam->load() : 0.0f,
                             1.0f });
}

bool EightOHeightProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void EightOHeightProcessor::releaseResources()
{
    // The device is being torn down: drop every voice and filter state so
    // no envelope survives into the next prepareToPlay. No allocation and
    // no locking, so it stays callable from the audio thread.
    voices.fill ({});
    nextVoice = 0;
    highpassState.fill (0.0f);
    monoLowState.fill (0.0f);
}

void EightOHeightProcessor::latchParams()
{
    auto read = [] (const std::atomic<float>* parameter, float fallback)
    {
        return parameter != nullptr ? parameter->load() : fallback;
    };

    latched.wave = static_cast<Wave> (juce::jlimit (0, 4, static_cast<int> (read (waveParam, 0.0f))));
    latched.pitchAttackSt = read (pitchAttackParam, 12.0f);
    latched.pitchDecaySt = read (pitchDecayParam, -24.0f);
    latched.pitchCurve = read (pitchCurveParam, 0.7f);
    latched.pitchTimeMs = juce::jmax (5.0f, read (pitchTimeParam, 350.0f));
    latched.ampDecayMs = juce::jmax (10.0f, read (ampDecayParam, 1200.0f));
    latched.sustain = juce::jlimit (0.0f, 1.0f, read (sustainParam, 0.0f));
    latched.levelDb = read (levelParam, 0.0f);

    latched.subLevel = juce::jlimit (0.0f, 1.0f, read (subLevelParam, 0.0f));
    latched.subDamping = juce::jlimit (0.0f, 1.0f, read (subDampingParam, 0.4f));
    latched.subTilt = juce::jlimit (-100.0f, 100.0f, read (subTiltParam, 0.0f)) / 100.0f;

    latched.glideMs = juce::jmax (0.0f, read (glideTimeParam, 80.0f));
    latched.glideConstantRate = read (glideModeParam, 0.0f) > 0.5f;

    latched.monoLock = read (monoLockParam, 1.0f) > 0.5f;
    latched.monoCrossoverHz = read (monoCrossoverParam, 120.0f);

    latched.subsonicHz = read (subsonicParam, 30.0f);
    latched.clipCeilingDb = read (clipParam, -1.0f);
    latched.outputDb = read (outputParam, 0.0f);
}

void EightOHeightProcessor::startNote (int midiNote, float velocity, bool legato)
{
    const auto limit = juce::jlimit (1, maxVoiceCount,
                                     static_cast<int> (voicesParam != nullptr ? voicesParam->load() : maxVoiceCount));

    // Legato with glide: retarget the voice that is already sounding so the
    // 808 slides instead of restarting, which is the whole point of the glide.
    if (legato && latched.glideMs > 0.0f)
    {
        for (auto& voice : voices)
        {
            if (! voice.active)
                continue;

            voice.note = midiNote;
            voice.held = true;
            voice.targetFrequency = juce::MidiMessage::getMidiNoteInHertz (midiNote);
            return;
        }
    }

    auto* voice = &voices[static_cast<size_t> (nextVoice)];

    // Prefer a free voice; steal round-robin otherwise.
    for (int i = 0; i < limit; ++i)
    {
        const auto index = (nextVoice + i) % limit;
        auto& candidate = voices[static_cast<size_t> (index)];

        if (! candidate.active)
        {
            voice = &candidate;
            nextVoice = (index + 1) % limit;
            break;
        }

        voice = &candidate;
    }

    const auto frequency = juce::MidiMessage::getMidiNoteInHertz (midiNote);

    // A stolen voice glides from wherever it was; a fresh one starts on pitch.
    if (! voice->active)
        voice->frequency = latched.glideMs > 0.0f ? voice->frequency : frequency;

    if (voice->frequency <= 0.0)
        voice->frequency = frequency;

    voice->targetFrequency = frequency;
    voice->active = true;
    voice->held = true;
    voice->releasing = false;
    voice->releaseGain = 1.0f;
    voice->amp = 1.0f;
    voice->note = midiNote;
    voice->velocity = velocity > 0.0f ? velocity : 1.0f;
    voice->pitchOffset = latched.pitchAttackSt;
    voice->pitchAttackStage = true;
    voice->pitchElapsedMs = 0.0f;
}

void EightOHeightProcessor::stopNote (int midiNote)
{
    for (auto& voice : voices)
    {
        if (voice.active && voice.note == midiNote && ! voice.releasing)
        {
            voice.held = false;
            voice.releasing = true;
        }
    }
}

void EightOHeightProcessor::allNotesOff (bool allowTail)
{
    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        voice.held = false;

        if (allowTail)
            voice.releasing = true;
        else
            voice = Voice {};
    }
}

int EightOHeightProcessor::activeVoiceCount() const
{
    int count = 0;

    for (const auto& voice : voices)
        if (voice.active)
            ++count;

    return count;
}

void EightOHeightProcessor::renderVoices (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    const auto channels = juce::jmin (2, buffer.getNumChannels());

    if (channels <= 0)
        return;

    auto* left = buffer.getWritePointer (0);
    auto* right = channels > 1 ? buffer.getWritePointer (1) : nullptr;

    const auto levelGain = dbToGain (latched.levelDb);
    const auto pitchTimeMs = juce::jmax (1.0f, latched.pitchTimeMs);
    const auto pitchExponent = juce::jmap (latched.pitchCurve, 0.05f, 1.0f, 0.4f, 3.0f);
    const auto msPerSample = 1000.0f / static_cast<float> (sampleRate);
    const auto decaySamples = latched.ampDecayMs * 0.001f * static_cast<float> (sampleRate);
    const auto sustainGain = latched.sustain;
    const auto releaseCoefficient = static_cast<float> (
        std::exp (-1.0 / (0.008 * sampleRate)));
    const auto glideSamples = latched.glideMs * 0.001f * static_cast<float> (sampleRate);

    // Sub-harmonic partial weights: a decaying spectral envelope over 2x..6x.
    std::array<float, eightoheight::numSubPartials> subWeights {};

    if (latched.subLevel > 0.0f)
    {
        for (int i = 0; i < eightoheight::numSubPartials; ++i)
        {
            const auto harmonic = static_cast<float> (i + 2);
            const auto damping = std::pow (1.0f / harmonic, 1.0f + latched.subDamping * 2.0f);
            const auto tilt = 1.0f + latched.subTilt * ((harmonic - 2.0f) / 4.0f) * 0.8f;
            subWeights[static_cast<size_t> (i)] = latched.subLevel * damping * tilt;
        }
    }

    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            // Glide toward the target in constant time (linear in log frequency
            // keeps the slide musical) or constant rate.
            if (voice.frequency != voice.targetFrequency)
            {
                if (glideSamples <= 1.0f || latched.glideConstantRate)
                {
                    const auto step = latched.glideConstantRate
                                          ? std::pow (2.0, 1.0 / 12.0 / static_cast<double> (sampleRate) * 12.0)
                                          : 1.0;
                    voice.frequency = voice.frequency < voice.targetFrequency
                                          ? juce::jmin (voice.targetFrequency, voice.frequency * step)
                                          : juce::jmax (voice.targetFrequency, voice.frequency / step);
                }
                else
                {
                    const auto ratio = std::pow (voice.targetFrequency / voice.frequency, 1.0 / glideSamples);
                    voice.frequency *= ratio;

                    if (std::abs (voice.frequency - voice.targetFrequency) < 0.01)
                        voice.frequency = voice.targetFrequency;
                }
            }

            // Pitch envelope: attack pitch decaying toward the decay pitch.
            voice.pitchElapsedMs += msPerSample;
            const auto progress = juce::jlimit (0.0f, 1.0f, voice.pitchElapsedMs / pitchTimeMs);
            const auto shaped = std::pow (progress, pitchExponent);
            voice.pitchOffset = latched.pitchAttackSt
                              + (latched.pitchDecaySt - latched.pitchAttackSt) * shaped;

            const auto frequency = voice.frequency * std::pow (2.0, static_cast<double> (voice.pitchOffset) / 12.0);
            const auto increment = frequency / sampleRate;

            voice.phase += increment;

            if (voice.phase >= 1.0)
                voice.phase -= std::floor (voice.phase);

            float sample = 0.0f;

            switch (latched.wave)
            {
                case Wave::sine:
                    sample = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * voice.phase));
                    break;

                case Wave::triangle:
                    sample = 1.0f - 4.0f * std::abs (static_cast<float> (voice.phase) - 0.5f);
                    break;

                case Wave::saw:
                    sample = static_cast<float> (2.0 * voice.phase - 1.0)
                           - polyBlep (voice.phase, increment);
                    break;

                case Wave::square:
                    sample = voice.phase < 0.5 ? 1.0f : -1.0f;
                    sample += polyBlep (voice.phase, increment)
                            - polyBlep (std::fmod (voice.phase + 0.5, 1.0), increment);
                    break;

                case Wave::supersaw:
                {
                    voice.supersawPhase += increment * 1.004;

                    if (voice.supersawPhase >= 1.0)
                        voice.supersawPhase -= std::floor (voice.supersawPhase);

                    const auto centre = static_cast<float> (2.0 * voice.phase - 1.0)
                                      - polyBlep (voice.phase, increment);
                    const auto detuned = static_cast<float> (2.0 * voice.supersawPhase - 1.0)
                                       - polyBlep (voice.supersawPhase, increment * 1.004);
                    sample = (centre * 0.6f + detuned * 0.4f);
                    break;
                }
            }

            if (latched.subLevel > 0.0f)
            {
                float sub = 0.0f;

                for (int p = 0; p < eightoheight::numSubPartials; ++p)
                {
                    const auto harmonic = static_cast<double> (p + 2);
                    auto& phase = voice.subPhase[static_cast<size_t> (p)];
                    phase += increment * harmonic;

                    if (phase >= 1.0)
                        phase -= std::floor (phase);

                    sub += subWeights[static_cast<size_t> (p)]
                         * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * phase));
                }

                sample += sub;
            }

            // Amp envelope: decay toward sustain while held, fast release after.
            if (voice.releasing)
            {
                voice.releaseGain *= releaseCoefficient;

                if (voice.releaseGain < 1.0e-4f)
                {
                    voice = Voice {};
                    break;
                }
            }
            else
            {
                const auto decayPerSample = decaySamples > 1.0f
                                                ? (1.0f - sustainGain) / decaySamples
                                                : 1.0f;
                voice.amp = juce::jmax (sustainGain, voice.amp - decayPerSample);
            }

            // A fully decayed 808 with no sustain has nothing left to play.
            if (voice.amp <= 1.0e-4f && sustainGain <= 1.0e-4f && ! voice.held)
            {
                voice = Voice {};
                break;
            }

            const auto value = sample * voice.amp * voice.releaseGain
                             * voice.velocity * levelGain;

            const auto index = startSample + i;
            left[index] += value;

            if (right != nullptr)
                right[index] += value;
        }
    }
}

void EightOHeightProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto channels = buffer.getNumChannels();

    for (int channel = 0; channel < channels; ++channel)
        buffer.clear (channel, 0, numSamples);

    if (numSamples == 0 || channels == 0)
        return;

    latchParams();

    const auto glideOn = latched.glideMs > 0.0f;

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            const auto legato = glideOn && activeVoiceCount() > 0;
            startNote (message.getNoteNumber(), message.getFloatVelocity(), legato);
        }
        else if (message.isNoteOff())
        {
            stopNote (message.getNoteNumber());
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            allNotesOff (message.isAllNotesOff());
        }
    }

    renderVoices (buffer, 0, numSamples);

    auto* left = buffer.getWritePointer (0);
    auto* right = channels > 1 ? buffer.getWritePointer (1) : nullptr;

    // Mono-lock: everything under the crossover collapses to a single mono low
    // band, which is what keeps an 808 solid on club systems.
    if (latched.monoLock && right != nullptr)
    {
        monoLowCoefficient = 1.0f - static_cast<float> (
            std::exp (-2.0 * juce::MathConstants<double>::pi * latched.monoCrossoverHz / sampleRate));

        for (int i = 0; i < numSamples; ++i)
        {
            const auto lowL = monoLowState[0] += monoLowCoefficient * (left[i] - monoLowState[0]);
            const auto lowR = monoLowState[1] += monoLowCoefficient * (right[i] - monoLowState[1]);
            const auto monoLow = 0.5f * (lowL + lowR);

            left[i] = (left[i] - lowL) + monoLow;
            right[i] = (right[i] - lowR) + monoLow;
        }
    }

    // Subsonic filter so nothing below the speaker's job reaches the clipper.
    subsonicCoefficient = static_cast<float> (
        std::exp (-2.0 * juce::MathConstants<double>::pi * latched.subsonicHz / sampleRate));

    for (int channel = 0; channel < juce::jmin (2, channels); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);
        float previousInput = 0.0f;
        float state = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto input = data[i];
            state = subsonicCoefficient * (state + input - previousInput);
            previousInput = input;
            data[i] = state;
        }
    }

    // Character section before the clipper, like a printed preamp into a
    // converter: the iron is part of what the ceiling sees.
    character.setSettings ({ characterOnParam != nullptr && characterOnParam->load() > 0.5f,
                             static_cast<engine::CharacterCircuit> (juce::jlimit (
                                 0, engine::numCharacterCircuits - 1,
                                 static_cast<int> (characterCircuitParam != nullptr ? characterCircuitParam->load() : 0.0f))),
                             characterDriveParam != nullptr ? characterDriveParam->load() : 6.0f,
                             characterIronParam != nullptr ? characterIronParam->load() : 0.3f,
                             characterToneParam != nullptr ? characterToneParam->load() : 0.0f,
                             characterOutputParam != nullptr ? characterOutputParam->load() : 0.0f,
                             1.0f });
    if (right != nullptr)
    {
        character.processBlock (left, right, numSamples);
    }
    else
    {
        // Mono layout: run the stereo section on a mirrored sample so the two
        // channels cannot alias each other's gain staging.
        character.beginBlock();

        for (int i = 0; i < numSamples; ++i)
        {
            float monoLeft = left[i];
            float monoRight = left[i];
            character.processSample (monoLeft, monoRight);
            left[i] = 0.5f * (monoLeft + monoRight);
        }
    }

    const auto ceiling = dbToGain (latched.clipCeilingDb);
    const auto outputGain = dbToGain (latched.outputDb);

    for (int channel = 0; channel < juce::jmin (2, channels); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
        {
            auto value = data[i] * outputGain;

            if (! std::isfinite (value))
                value = 0.0f;

            // tanh into the ceiling: soft, and it cannot pass it.
            data[i] = ceiling * std::tanh (value / ceiling);
        }
    }
}

juce::AudioProcessorEditor* EightOHeightProcessor::createEditor()
{
    return new EightOHeightEditor (*this);
}

void EightOHeightProcessor::setCurrentProgram (int index)
{
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, index);

    struct Preset
    {
        float pitchTime, ampDecay, sustain, subLevel, subDamping, glide;
        int wave;
    };

    static constexpr Preset presets[] {
        { 350.0f, 1200.0f, 0.0f, 0.0f, 0.4f, 0.0f, 0 },  // Sub 808
        { 200.0f, 3200.0f, 0.55f, 0.35f, 0.55f, 0.0f, 0 }, // Long Boom
        { 90.0f, 700.0f, 0.0f, 0.15f, 0.3f, 0.0f, 1 },    // Punch 808
        { 260.0f, 1800.0f, 0.2f, 0.5f, 0.7f, 120.0f, 2 }  // Distorted Slide
    };

    const auto preset = presets[static_cast<size_t> (currentProgram)];

    auto setParam = [this] (const char* id, float value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    setParam (eightoheight::pitchTime, preset.pitchTime);
    setParam (eightoheight::ampDecay, preset.ampDecay);
    setParam (eightoheight::sustain, preset.sustain);
    setParam (eightoheight::subLevel, preset.subLevel);
    setParam (eightoheight::subDamping, preset.subDamping);
    setParam (eightoheight::glideTime, preset.glide);
    setParam (eightoheight::wave, static_cast<float> (preset.wave));
}

const juce::String EightOHeightProcessor::getProgramName (int index)
{
    static const juce::StringArray names { "Sub 808", "Long Boom", "Punch 808", "Distorted Slide" };
    return names[juce::jlimit (0, names.size() - 1, index)];
}

void EightOHeightProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destination);
}

void EightOHeightProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);

        if (state.isValid())
            apvts.replaceState (state);
    }
}
} // namespace nc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nc::EightOHeightProcessor();
}
