#include "BuiltinSynth.h"

#include <algorithm>
#include <cmath>

namespace nc
{
BuiltinSynth::BuiltinSynth()
{
    for (int i = 0; i < 8; ++i)
    {
        auto* voice = new Voice();
        voice->wave = wave;
        synth.addVoice (voice);
    }

    synth.addSound (new Sound());
    synth.setNoteStealingEnabled (true);
}

void BuiltinSynth::prepare (double sampleRate, int blockSize)
{
    synth.setCurrentPlaybackSampleRate (juce::jmax (1.0, sampleRate));
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

    juce::MemoryBlock memory;
    const auto waveInt = static_cast<int> (wave);
    memory.append (static_cast<const void*> (&waveInt), sizeof (waveInt));
    state.stateBase64 = memory.toBase64Encoding().toStdString();
    return true;
}

bool BuiltinSynth::setState (const nc::engine::PluginSlotState& state)
{
    bypassed = state.bypassed;

    if (! state.stateBase64.empty())
    {
        juce::MemoryBlock memory;

        if (memory.fromBase64Encoding (juce::String (state.stateBase64)) && memory.getSize() >= sizeof (int))
        {
            auto waveInt = 0;
            memory.copyTo (&waveInt, 0, sizeof (int));
            wave = static_cast<Wave> (juce::jlimit (0, 2, waveInt));

            for (int i = 0; i < synth.getNumVoices(); ++i)
                if (auto* voice = dynamic_cast<Voice*> (synth.getVoice (i)))
                    voice->wave = wave;
        }
    }

    return true;
}

//------------------------------------------------------------------------------

void BuiltinSynth::Voice::startNote (int midiNoteNumber, float velocity,
                                     juce::SynthesiserSound*,
                                     int)
{
    frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber, 440.0);
    targetLevel = juce::jlimit (0.0f, 1.0f, velocity) * 0.22f;
    level = 0.0f;
    releasing = false;
    phase = 0.0;
}

void BuiltinSynth::Voice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                                            int startSample, int numSamples)
{
    const auto rate = juce::jmax (1.0, getSampleRate());
    const auto channels = juce::jmin (outputBuffer.getNumChannels(), 2);
    const auto attackPerSample = (targetLevel > 0.0f) ? targetLevel / juce::jmax (1.0, 0.005 * rate) : 0.0f;
    const auto releaseFactor = static_cast<float> (std::exp (-1.0 / juce::jmax (1.0, 0.18 * rate)));

    for (int i = 0; i < numSamples; ++i)
    {
        if (releasing)
        {
            level *= releaseFactor;

            if (level <= 0.0001f)
            {
                clearCurrentNote();
                return;
            }
        }
        else
        {
            level = juce::jmin (targetLevel, level + static_cast<float> (attackPerSample));
        }

        const auto phaseStep = frequency / rate;
        phase += phaseStep;

        if (phase >= 1.0)
            phase -= std::floor (phase);

        float sample = 0.0f;

        switch (wave)
        {
            case Wave::sine:
                sample = std::sin (juce::MathConstants<double>::twoPi * phase);
                break;

            case Wave::triangle:
                sample = 1.0f - 4.0f * std::abs (phase - 0.5f);
                break;

            case Wave::saw:
                sample = static_cast<float> (2.0 * phase - 1.0) * 0.6f;
                break;
        }

        const auto value = sample * level;
        const auto actualSample = startSample + i;

        for (int channel = 0; channel < channels; ++channel)
            outputBuffer.addSample (channel, actualSample, value);
    }
}
} // namespace nc
