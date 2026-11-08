#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>

using Catch::Approx;
using nc::EightOHeightProcessor;
using nc::eightoheight::numSubPartials;

namespace
{
void prepare (EightOHeightProcessor& processor, double rate = 48000.0)
{
    processor.prepareToPlay (rate, 512);
}

juce::MidiBuffer noteOn (int note, float velocity = 0.9f)
{
    juce::MidiBuffer buffer;
    buffer.addEvent (juce::MidiMessage::noteOn (1, note, velocity), 0);
    return buffer;
}

float peakOf (const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax (peak, buffer.getMagnitude (channel, 0, buffer.getNumSamples()));

    return peak;
}

void setParam (EightOHeightProcessor& processor, const char* id, float plainValue)
{
    if (auto* parameter = processor.apvts.getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
}
} // namespace

TEST_CASE ("808 Designer exposes a complete automatable parameter set", "[eightoheight]")
{
    EightOHeightProcessor processor;

    std::set<juce::String> ids;

    for (auto* parameter : processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        REQUIRE (ranged != nullptr);

        ids.insert (ranged->paramID);

        // Every parameter the host enumerates must be reachable through the
        // APVTS, or an editor attachment would silently fail to bind.
        REQUIRE (processor.apvts.getParameter (ranged->paramID) != nullptr);
    }

    REQUIRE (processor.getParameters().size() >= 24);

    for (const auto* id : { nc::eightoheight::wave, nc::eightoheight::pitchAttack,
                            nc::eightoheight::ampDecay, nc::eightoheight::subLevel,
                            nc::eightoheight::glideTime, nc::eightoheight::monoLock,
                            nc::eightoheight::clipCeiling, nc::eightoheight::characterOn,
                            nc::eightoheight::characterCircuit })
        REQUIRE (ids.count (id) == 1);
}

TEST_CASE ("A note on produces audio and silence before it", "[eightoheight]")
{
    EightOHeightProcessor processor;
    prepare (processor);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer quiet;
    processor.processBlock (buffer, quiet);
    REQUIRE (peakOf (buffer) < 1.0e-6f);

    auto on = noteOn (36, 1.0f);
    processor.processBlock (buffer, on);

    REQUIRE (processor.activeVoiceCount() == 1);
    REQUIRE (peakOf (buffer) > 0.01f);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (channel, i)));
}

TEST_CASE ("The clipper never passes its ceiling", "[eightoheight]")
{
    EightOHeightProcessor processor;
    prepare (processor);

    setParam (processor, nc::eightoheight::level, 0.0f);
    setParam (processor, nc::eightoheight::clipCeiling, -6.0f);
    setParam (processor, nc::eightoheight::outputLevel, 12.0f);
    setParam (processor, nc::eightoheight::ampDecay, 8000.0f);
    setParam (processor, nc::eightoheight::sustain, 1.0f);

    auto on = noteOn (31, 1.0f);
    juce::AudioBuffer<float> buffer (2, 512);
    processor.processBlock (buffer, on);

    const auto ceiling = std::pow (10.0f, -6.0f / 20.0f);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::abs (buffer.getSample (channel, i)) <= ceiling + 1.0e-4f);
}

TEST_CASE ("Mono lock collapses the sub band to one signal", "[eightoheight]")
{
    EightOHeightProcessor processor;
    prepare (processor);

    setParam (processor, nc::eightoheight::monoLock, 1.0f);
    setParam (processor, nc::eightoheight::monoCrossover, 300.0f);

    auto on = noteOn (28, 1.0f);
    juce::AudioBuffer<float> buffer (2, 512);
    processor.processBlock (buffer, on);

    // With the crossover above the note, no width exists in the low band, so
    // both channels must carry the same samples.
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        REQUIRE (buffer.getSample (0, i) == Approx (buffer.getSample (1, i)).margin (1.0e-6));
}

TEST_CASE ("The pitch envelope sweeps without stopping the note", "[eightoheight]")
{
    EightOHeightProcessor processor;
    prepare (processor);

    setParam (processor, nc::eightoheight::pitchAttack, 24.0f);
    setParam (processor, nc::eightoheight::pitchDecay, -24.0f);
    setParam (processor, nc::eightoheight::pitchTime, 50.0f);
    setParam (processor, nc::eightoheight::ampDecay, 4000.0f);
    setParam (processor, nc::eightoheight::sustain, 1.0f);

    auto on = noteOn (33, 1.0f);
    juce::AudioBuffer<float> buffer (2, 512);
    processor.processBlock (buffer, on);
    REQUIRE (peakOf (buffer) > 0.0f);

    juce::MidiBuffer none;
    juce::AudioBuffer<float> later (2, 512);

    for (int block = 0; block < 8; ++block)
        processor.processBlock (later, none);

    REQUIRE (processor.activeVoiceCount() == 1);
    REQUIRE (peakOf (later) > 0.0f);
}

TEST_CASE ("The sub-harmonic layer adds energy", "[eightoheight]")
{
    EightOHeightProcessor processor;
    prepare (processor);
    setParam (processor, nc::eightoheight::subLevel, 1.0f);

    auto on = noteOn (36, 1.0f);
    juce::AudioBuffer<float> withSub (2, 512);
    processor.processBlock (withSub, on);

    REQUIRE (numSubPartials == 5);
    REQUIRE (peakOf (withSub) > 0.0f);
}

TEST_CASE ("State survives a save and reload", "[eightoheight]")
{
    EightOHeightProcessor source;
    setParam (source, nc::eightoheight::ampDecay, 4321.0f);
    setParam (source, nc::eightoheight::subLevel, 0.66f);
    setParam (source, nc::eightoheight::characterOn, 1.0f);
    setParam (source, nc::eightoheight::characterCircuit, 4.0f);

    juce::MemoryBlock memory;
    source.getStateInformation (memory);
    REQUIRE (memory.getSize() > 0);

    EightOHeightProcessor destination;
    destination.setStateInformation (memory.getData(), static_cast<int> (memory.getSize()));

    REQUIRE (destination.apvts.getRawParameterValue (nc::eightoheight::ampDecay)->load()
             == Approx (4321.0f).margin (0.5));
    REQUIRE (destination.apvts.getRawParameterValue (nc::eightoheight::subLevel)->load()
             == Approx (0.66f).margin (0.001));
    REQUIRE (destination.apvts.getRawParameterValue (nc::eightoheight::characterCircuit)->load()
             == Approx (4.0f).margin (0.01));
}

TEST_CASE ("All notes off silences the voice pool", "[eightoheight]")
{
    EightOHeightProcessor processor;
    prepare (processor);

    for (int note = 30; note < 36; ++note)
    {
        auto on = noteOn (note);
        juce::AudioBuffer<float> buffer (2, 128);
        processor.processBlock (buffer, on);
    }

    REQUIRE (processor.activeVoiceCount() > 0);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer allOff;
    allOff.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    processor.processBlock (buffer, allOff);

    REQUIRE (processor.activeVoiceCount() == 0);
    REQUIRE (peakOf (buffer) < 1.0e-6f);
}
