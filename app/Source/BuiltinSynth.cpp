#include "BuiltinSynth.h"

#include <algorithm>
#include <cmath>

namespace nc
{
namespace
{
// PolyBLEP correction around the waveform discontinuity. A sawtooth built
// straight from the phase ramp carries harmonics past Nyquist, and everything
// above it folds back down as inharmonic hash -- the fizzy digital noise a
// naive oscillator makes on every note. Subtracting this residual band-limits
// the step without changing the pitch or the tone below Nyquist.
double polyBlep (double t, double dt) noexcept
{
    if (dt <= 0.0 || dt >= 1.0)
        return 0.0;

    if (t < dt)
    {
        const auto x = t / dt;
        return x + x - x * x - 1.0;
    }

    if (t > 1.0 - dt)
    {
        const auto x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }

    return 0.0;
}
} // namespace

BuiltinSynth::Preset BuiltinSynth::presetFor (PresetId id)
{
    Preset preset;

    switch (id)
    {
        case PresetId::bass:
            preset.wave = Wave::saw;
            preset.cutoffHz = 900.0f;
            preset.attackMs = 4.0f;
            preset.decayMs = 140.0f;
            preset.sustain = 0.65f;
            preset.releaseMs = 120.0f;
            preset.level = 0.30f;
            break;
        case PresetId::keys:
            preset.wave = Wave::triangle;
            preset.cutoffHz = 5200.0f;
            preset.attackMs = 3.0f;
            preset.decayMs = 220.0f;
            preset.sustain = 0.0f;
            preset.releaseMs = 260.0f;
            preset.level = 0.24f;
            break;
        case PresetId::pad:
            preset.wave = Wave::saw;
            preset.cutoffHz = 2400.0f;
            preset.attackMs = 320.0f;
            preset.decayMs = 400.0f;
            preset.sustain = 0.85f;
            preset.releaseMs = 900.0f;
            preset.level = 0.16f;
            break;
    }

    return preset;
}

BuiltinSynth::BuiltinSynth()
{
    preset = presetFor (presetId);

    for (int i = 0; i < 16; ++i)
    {
        auto* voice = new Voice();
        voice->preset = preset;
        synth.addVoice (voice);
    }

    synth.addSound (new Sound());
    synth.setNoteStealingEnabled (true);
}

void BuiltinSynth::applyPresetToVoices()
{
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<Voice*> (synth.getVoice (i)))
            voice->preset = preset;
}

void BuiltinSynth::setPreset (PresetId id)
{
    presetId = id;
    preset = presetFor (id);
    applyPresetToVoices();
}

void BuiltinSynth::prepare (double sampleRate, int blockSize)
{
    synth.setCurrentPlaybackSampleRate (juce::jmax (1.0, sampleRate));
    character.prepare (juce::jmax (1.0, sampleRate));
    (void) blockSize;
}

void BuiltinSynth::process (juce::AudioBuffer<float>& buffer, const nc::engine::TransportBlock&)
{
    if (bypassed || buffer.getNumSamples() == 0)
        return;

    juce::MidiBuffer empty;
    process (buffer, {}, empty);
}

void BuiltinSynth::process (juce::AudioBuffer<float>& buffer,
                            const nc::engine::TransportBlock&,
                            juce::MidiBuffer& midi)
{
    if (bypassed || buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return;

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    // The instrument prints through its character stage before anything else
    // sees it, which is the point of baking the preamp into the instrument
    // rather than leaving it as a separate insert.
    if (buffer.getNumChannels() > 1)
    {
        character.processBlock (buffer.getWritePointer (0), buffer.getWritePointer (1),
                                buffer.getNumSamples());
    }
    else if (buffer.getNumChannels() == 1)
    {
        auto* mono = buffer.getWritePointer (0);
        character.beginBlock();

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float left = mono[i];
            float right = mono[i];
            character.processSample (left, right);
            mono[i] = 0.5f * (left + right);
        }
    }

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (samples[i]))
                samples[i] = 0.0f;
    }
}

bool BuiltinSynth::getState (nc::engine::PluginSlotState& state) const
{
    state.identifier = builtinSynthIdentifier;
    state.format = "builtin";
    state.descriptionXml.clear();
    state.bypassed = bypassed;

    // Versioned blob: int version, int presetId, then the preset fields, then
    // the character section (v3). A 4-byte blob is the legacy wave-only state
    // and still loads.
    juce::MemoryBlock memory;
    const auto version = 3;
    const auto presetInt = static_cast<int> (presetId);
    const auto waveInt = static_cast<int> (preset.wave);
    memory.append (static_cast<const void*> (&version), sizeof (version));
    memory.append (static_cast<const void*> (&presetInt), sizeof (presetInt));
    memory.append (static_cast<const void*> (&waveInt), sizeof (waveInt));
    memory.append (static_cast<const void*> (&preset.cutoffHz), sizeof (preset.cutoffHz));
    memory.append (static_cast<const void*> (&preset.attackMs), sizeof (preset.attackMs));
    memory.append (static_cast<const void*> (&preset.decayMs), sizeof (preset.decayMs));
    memory.append (static_cast<const void*> (&preset.sustain), sizeof (preset.sustain));
    memory.append (static_cast<const void*> (&preset.releaseMs), sizeof (preset.releaseMs));
    memory.append (static_cast<const void*> (&preset.level), sizeof (preset.level));

    const auto settings = character.currentSettings();
    const auto characterOnInt = settings.on ? 1 : 0;
    const auto circuitInt = static_cast<int> (settings.circuit);
    memory.append (static_cast<const void*> (&characterOnInt), sizeof (characterOnInt));
    memory.append (static_cast<const void*> (&circuitInt), sizeof (circuitInt));
    memory.append (static_cast<const void*> (&settings.driveDb), sizeof (settings.driveDb));
    memory.append (static_cast<const void*> (&settings.iron), sizeof (settings.iron));
    memory.append (static_cast<const void*> (&settings.tone), sizeof (settings.tone));
    memory.append (static_cast<const void*> (&settings.outputDb), sizeof (settings.outputDb));

    state.stateBase64 = memory.toBase64Encoding().toStdString();
    return true;
}

bool BuiltinSynth::setState (const nc::engine::PluginSlotState& state)
{
    bypassed = state.bypassed;

    if (state.stateBase64.empty())
        return true;

    juce::MemoryBlock memory;

    if (! memory.fromBase64Encoding (juce::String (state.stateBase64)))
        return false;

    if (memory.getSize() == sizeof (int))
    {
        // Legacy wave-only blob.
        auto waveInt = 0;
        memory.copyTo (&waveInt, 0, sizeof (int));
        preset.wave = static_cast<Wave> (juce::jlimit (0, 2, waveInt));
        applyPresetToVoices();
        return true;
    }

    // The legacy blobs are shorter than the current one, so the size is
    // validated against the version that was actually written instead of a
    // single worst-case length (which rejected every v2 session).
    if (memory.getSize() < 3 * sizeof (int))
        return false;

    int version = 0;
    memory.copyTo (&version, 0, sizeof (int));

    if (version != 2 && version != 3)
        return false;

    const auto requiredBytes = version >= 3
                                   ? 5 * sizeof (int) + 10 * sizeof (float)
                                   : 3 * sizeof (int) + 6 * sizeof (float);

    if (memory.getSize() < requiredBytes)
        return false;

    int presetInt = 0;
    int waveInt = 0;
    float cutoffHz = 0.0f;
    float attackMs = 0.0f;
    float decayMs = 0.0f;
    float sustain = 0.0f;
    float releaseMs = 0.0f;
    float level = 0.0f;
    size_t offset = 0;
    auto readInt = [&memory, &offset] (int& out)
    {
        memory.copyTo (&out, offset, sizeof (int));
        offset += sizeof (int);
    };
    auto readFloat = [&memory, &offset] (float& out)
    {
        memory.copyTo (&out, offset, sizeof (float));
        offset += sizeof (float);
    };

    readInt (version);

    readInt (presetInt);
    readInt (waveInt);
    readFloat (cutoffHz);
    readFloat (attackMs);
    readFloat (decayMs);
    readFloat (sustain);
    readFloat (releaseMs);
    readFloat (level);

    if (version >= 3)
    {
        int characterOnInt = 0;
        int circuitInt = 0;
        float driveDb = 6.0f, iron = 0.3f, tone = 0.0f, outputDb = 0.0f;
        readInt (characterOnInt);
        readInt (circuitInt);
        readFloat (driveDb);
        readFloat (iron);
        readFloat (tone);
        readFloat (outputDb);

        engine::CharacterSettings settings;
        settings.on = characterOnInt > 0;
        settings.circuit = static_cast<engine::CharacterCircuit> (juce::jlimit (0, engine::numCharacterCircuits - 1, circuitInt));
        settings.driveDb = driveDb;
        settings.iron = iron;
        settings.tone = tone;
        settings.outputDb = outputDb;
        character.setSettings (settings);
    }

    presetId = static_cast<PresetId> (juce::jlimit (0, 2, presetInt));
    preset.wave = static_cast<Wave> (juce::jlimit (0, 2, waveInt));
    preset.cutoffHz = juce::jlimit (40.0f, 18000.0f, cutoffHz);
    preset.attackMs = juce::jlimit (0.0f, 4000.0f, attackMs);
    preset.decayMs = juce::jlimit (0.0f, 4000.0f, decayMs);
    preset.sustain = juce::jlimit (0.0f, 1.0f, sustain);
    preset.releaseMs = juce::jlimit (1.0f, 8000.0f, releaseMs);
    preset.level = juce::jlimit (0.01f, 1.0f, level);
    applyPresetToVoices();
    return true;
}

//------------------------------------------------------------------------------

bool BuiltinSynth::Voice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<Sound*> (sound) != nullptr;
}

void BuiltinSynth::Voice::startNote (int midiNoteNumber, float velocity,
                                     juce::SynthesiserSound*,
                                     int)
{
    baseFrequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber, 440.0);
    envLevel = 0.0f;
    filterState = 0.0f;
    filterState2 = 0.0f;
    envStage = 0;
    noteHeld = true;
    phase = 0.0;
    (void) velocity;
}

void BuiltinSynth::Voice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff && ! isSustainPedalDown())
    {
        noteHeld = false;
        envStage = 3;
    }
    else if (! allowTailOff)
    {
        clearCurrentNote();
    }
    // With the sustain pedal down the Synthesiser defers this call until
    // pedal-up, but guard anyway so a routed CC64 can never kill the note.
}

void BuiltinSynth::Voice::pitchWheelMoved (int newPitchWheelValue)
{
    bendSemitones = (static_cast<double> (newPitchWheelValue) - 8192.0) / 8192.0 * 2.0;
}

void BuiltinSynth::Voice::controllerMoved (int controllerNumber, int newControllerValue)
{
    if (controllerNumber == 64)
        setSustainPedalDown (newControllerValue >= 64);
}

void BuiltinSynth::Voice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                                            int startSample, int numSamples)
{
    const auto rate = juce::jmax (1.0, getSampleRate());
    const auto channels = juce::jmin (outputBuffer.getNumChannels(), 2);

    // Envelope peak is the preset level itself. Doubling it here gave every
    // voice 0.6 of full scale, so a three-note chord left the instrument at
    // +3.4 dBFS and the master maximizer flattened it on every note -- the
    // grinding distortion a held chord was heard as.
    const auto peak = preset.level;
    const auto attackRate = preset.attackMs > 0.0f
                                ? peak / (preset.attackMs * 0.001f * static_cast<float> (rate))
                                : peak;
    const auto decayTarget = peak * preset.sustain;
    const auto decayRate = preset.decayMs > 0.0f
                               ? (peak - decayTarget) / (preset.decayMs * 0.001f * static_cast<float> (rate))
                               : (peak - decayTarget);
    const auto releaseFactor = static_cast<float> (
        std::exp (-4.0 / juce::jmax (1.0, preset.releaseMs * 0.001f * rate)));

    // Key-tracked lowpass: higher notes stay bright. Two cascaded one-poles
    // give 12 dB/oct, which keeps the tone under control below Nyquist instead
    // of leaving a single 6 dB/oct slope to smear the top octave.
    const auto keyCutoff = juce::jlimit (40.0f, static_cast<float> (rate * 0.45),
                                         preset.cutoffHz * (0.5f + 0.5f * static_cast<float> (baseFrequency / 440.0)));
    const auto filterCoef = static_cast<float> (
        1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * static_cast<double> (keyCutoff) / rate));

    const auto frequency = baseFrequency * std::pow (2.0, bendSemitones / 12.0);
    const auto phaseStep = frequency / rate;

    for (int i = 0; i < numSamples; ++i)
    {
        switch (envStage)
        {
            case 0: // attack
                envLevel = std::min (peak, envLevel + attackRate);
                if (envLevel >= peak)
                    envStage = 1;
                break;
            case 1: // decay
                envLevel = std::max (decayTarget, envLevel - decayRate);
                if (envLevel <= decayTarget)
                    envStage = 2;
                break;
            case 2: // sustain
                if (! noteHeld && ! isSustainPedalDown())
                    envStage = 3;
                break;
            default: // release
                envLevel *= releaseFactor;
                if (envLevel <= 0.0001f)
                {
                    clearCurrentNote();
                    return;
                }
                break;
        }

        phase += phaseStep;

        if (phase >= 1.0)
            phase -= std::floor (phase);

        float sample = 0.0f;

        switch (preset.wave)
        {
            case Wave::sine:
                sample = std::sin (juce::MathConstants<double>::twoPi * phase);
                break;
            case Wave::triangle:
                sample = 1.0f - 4.0f * std::abs (phase - 0.5f);
                break;
            case Wave::saw:
                // Band-limited ramp: the polyBLEP residual only exists in the
                // sample either side of the wrap, so the aliasing that used to
                // ride on every note is cancelled at the source.
                sample = static_cast<float> (2.0 * phase - 1.0 - polyBlep (phase, phaseStep)) * 0.6f;
                break;
        }

        filterState += filterCoef * (sample - filterState);
        filterState2 += filterCoef * (filterState - filterState2);
        const auto value = filterState2 * envLevel;
        const auto actualSample = startSample + i;

        for (int channel = 0; channel < channels; ++channel)
            outputBuffer.addSample (channel, actualSample, value);
    }
}
} // namespace nc
