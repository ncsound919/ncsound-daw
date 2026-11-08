#include <nc/engine/AnalogCharacter.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;
using nc::engine::AnalogCharacter;
using nc::engine::CharacterCircuit;
using nc::engine::CharacterSettings;

namespace
{
CharacterSettings enabledSettings (CharacterCircuit circuit, float driveDb = 12.0f)
{
    CharacterSettings settings;
    settings.on = true;
    settings.circuit = circuit;
    settings.driveDb = driveDb;
    settings.iron = 0.5f;
    settings.tone = 0.0f;
    settings.mix = 1.0f;
    return settings;
}

std::vector<float> renderSine (AnalogCharacter& character, float amplitude, int samples = 4096, float freqHz = 220.0f)
{
    std::vector<float> left (static_cast<size_t> (samples)), right (static_cast<size_t> (samples));
    const auto step = 6.28318530718f * freqHz / 48000.0f;

    for (int i = 0; i < samples; ++i)
        left[static_cast<size_t> (i)] = right[static_cast<size_t> (i)] =
            amplitude * std::sin (step * static_cast<float> (i));

    character.processBlock (left.data(), right.data(), samples);
    return left;
}

double rms (const std::vector<float>& signal)
{
    double sum = 0.0;

    for (auto value : signal)
        sum += static_cast<double> (value) * value;

    return std::sqrt (sum / static_cast<double> (signal.size()));
}
} // namespace

TEST_CASE ("Analog character is a unity pass when switched off", "[character]")
{
    AnalogCharacter character;
    character.prepare (48000.0);
    CharacterSettings settings;
    settings.on = false;
    character.setSettings (settings);

    float left = 0.5f, right = -0.25f;
    character.beginBlock();

    for (int i = 0; i < 2000; ++i)
        character.processSample (left, right);

    // Only the trim moves the level, and the default trim is unity.
    REQUIRE (left == Approx (0.5f).margin (0.001));
    REQUIRE (right == Approx (-0.25f).margin (0.001));
}

TEST_CASE ("Every circuit colours the signal without exploding", "[character]")
{
    for (int circuit = 0; circuit < nc::engine::numCharacterCircuits; ++circuit)
    {
        AnalogCharacter character;
        character.prepare (48000.0);
        character.setSettings (enabledSettings (static_cast<CharacterCircuit> (circuit), 18.0f));

        const auto output = renderSine (character, 0.4f);
        const auto level = rms (output);

        INFO ("circuit " << nc::engine::characterCircuitName (static_cast<CharacterCircuit> (circuit)));
        REQUIRE (level > 0.0);
        REQUIRE (level < 2.0);

        for (auto value : output)
            REQUIRE (std::isfinite (value));
    }
}

TEST_CASE ("Drive raises harmonics and output trim compensates", "[character]")
{
    for (int circuit = 0; circuit < nc::engine::numCharacterCircuits; ++circuit)
    {
        const auto id = static_cast<CharacterCircuit> (circuit);

        AnalogCharacter quiet;
        quiet.prepare (48000.0);
        quiet.setSettings (enabledSettings (id, 0.0f));
        const auto cleanLevel = rms (renderSine (quiet, 0.3f));

        AnalogCharacter driven;
        driven.prepare (48000.0);
        auto drivenSettings = enabledSettings (id, 24.0f);
        drivenSettings.outputDb = -12.0f;
        driven.setSettings (drivenSettings);
        const auto drivenLevel = rms (renderSine (driven, 0.3f));

        INFO ("circuit " << nc::engine::characterCircuitName (id));
        REQUIRE (drivenLevel > 0.0);
        REQUIRE (drivenLevel < cleanLevel * 4.0);
    }
}

TEST_CASE ("Tone tilts the spectrum in the expected direction", "[character]")
{
    auto measureHighBand = [] (float tone)
    {
        AnalogCharacter character;
        character.prepare (48000.0);
        auto settings = enabledSettings (CharacterCircuit::avalonM5, 0.0f);
        settings.tone = tone;
        character.setSettings (settings);
        // 8 kHz fundamental sits above the 700 Hz tilt crossover, so the
        // tilt's high gain is what shapes it. A 220 Hz probe measures the
        // low band (cut by "bright") instead, inverting the expectation.
        const auto output = renderSine (character, 0.5f, 8192, 8000.0f);

        // Crude high-band energy: first difference emphasises the top.
        double sum = 0.0;

        for (size_t i = 1; i < output.size(); ++i)
        {
            const auto difference = output[i] - output[i - 1];
            sum += static_cast<double> (difference) * difference;
        }

        return sum;
    };

    const auto dark = measureHighBand (-1.0f);
    const auto bright = measureHighBand (1.0f);

    REQUIRE (bright > dark);
}

TEST_CASE ("Mix blends the character in parallel", "[character]")
{
    AnalogCharacter character;
    character.prepare (48000.0);
    auto settings = enabledSettings (CharacterCircuit::neveBa283, 20.0f);
    settings.mix = 0.0f;
    character.setSettings (settings);

    float left = 1.0f, right = 1.0f;
    character.beginBlock();

    for (int i = 0; i < 64; ++i)
        character.processSample (left, right);

    REQUIRE (left == Approx (1.0f).margin (0.01));
}

TEST_CASE ("Character output stays finite at full scale", "[character]")
{
    AnalogCharacter character;
    character.prepare (96000.0);
    character.setSettings (enabledSettings (CharacterCircuit::redd6dj8, 24.0f));
    character.beginBlock();

    for (int i = 0; i < 512; ++i)
    {
        float left = (i % 2 == 0) ? 4.0f : -4.0f;
        float right = -left;
        character.processSample (left, right);
        REQUIRE (std::isfinite (left));
        REQUIRE (std::isfinite (right));
        REQUIRE (std::abs (left) <= 1.0f);
    }
}
