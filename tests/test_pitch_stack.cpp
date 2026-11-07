#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <nc/engine/MidiPitch.hpp>
#include <nc/engine/MidiSchedule.hpp>
#include <nc/engine/PitchStack.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace nc::engine;
using Catch::Approx;

namespace
{
MidiEvent noteOn (int pitch, int offset = 0)
{
    MidiEvent e;
    e.kind = MidiEvent::Kind::noteOn;
    e.pitch = pitch;
    e.velocity = 100;
    e.channel = 0;
    e.sampleOffset = offset;
    return e;
}

MidiEvent bend (int value, int offset = 0)
{
    MidiEvent e;
    e.kind = MidiEvent::Kind::bend;
    e.value = value;
    e.channel = 0;
    e.sampleOffset = offset;
    return e;
}
} // namespace

TEST_CASE ("pitchstack clamps to valid ranges")
{
    PitchStack p;
    p.transpose = 7; p.pitchCents = 50.0f; p.tuningHz = 440.0f; p.clamp();
    REQUIRE (p.transpose == 7);
    REQUIRE (std::abs (p.pitchCents - 50.0f) < 1e-6f);
    REQUIRE (std::abs (p.cents() - 750.0f) < 1e-4f);

    PitchStack bad;
    bad.transpose = 100; bad.pitchCents = 500.0f; bad.tuningHz = 900.0f; bad.clamp();
    REQUIRE (bad.transpose == PitchStack::kTransposeMax);
    REQUIRE (std::abs (bad.pitchCents - PitchStack::kPitchMaxCents) < 1e-6f);
    REQUIRE (std::abs (bad.tuningHz - PitchStack::kHzMax) < 1e-6f);
}

TEST_CASE ("pitchstack Hz to cents is 1200*log2(Hz/440)")
{
    REQUIRE (PitchStack::hzToCents (440.0f) == Catch::Approx (0.0f).margin (1e-5f));
    REQUIRE (PitchStack::hzToCents (432.0f) == Catch::Approx (-31.77f).margin (0.1f));
    REQUIRE (PitchStack::hzToCents (466.0f) == Catch::Approx (99.5f).margin (0.5f));
    REQUIRE (PitchStack::hzToCents (0.0f) == Catch::Approx (0.0f));
}

TEST_CASE ("pitchstack NaN and Inf reset to defaults")
{
    PitchStack p;
    p.pitchCents = std::nan ("");
    p.tuningHz = std::numeric_limits<float>::infinity();
    p.clamp();
    REQUIRE (p.pitchCents == 0.0f);
    REQUIRE (p.tuningHz == PitchStack::kHzDefault);
}

TEST_CASE ("pitchstack scopes sum additively")
{
    PitchStack g{ 12, 0, 440 }, t{ -12, 0, 440 }, i{ 0, 50, 440 };
    REQUIRE (sumCents (g, t, i) == Catch::Approx (50.0f).margin (1e-4f));
    REQUIRE (pitchRatio (1200.0f) == Catch::Approx (2.0f).margin (1e-4f));
}

TEST_CASE ("applyPitch transposes note on and off together")
{
    std::vector<MidiEvent> events{ noteOn (60), noteOn (60) };
    events[1].kind = MidiEvent::Kind::noteOff;
    events[1].velocity = 0;

    PitchStack p;
    p.transpose = 12;
    applyPitch (events, p);
    REQUIRE (events.size() == 2);
    REQUIRE (events[0].pitch == 72);
    REQUIRE (events[1].pitch == 72);
}

TEST_CASE ("applyPitch drops out-of-range notes")
{
    std::vector<MidiEvent> events{ noteOn (2), noteOn (60) };
    PitchStack p;
    p.transpose = -12;
    applyPitch (events, p);
    REQUIRE (events.size() == 1);
    REQUIRE (events[0].pitch == 48);
}

TEST_CASE ("applyPitch injects bend for fine cents")
{
    std::vector<MidiEvent> events{ noteOn (60) };
    PitchStack p;
    p.pitchCents = 50.0f; // +50 cents -> wholeSt=1 (rounded), remainder -50c -> bend below centre
    applyPitch (events, p);
    REQUIRE (events.size() == 2);
    bool foundBend = false;
    int notePitch = -1;
    for (const auto& e : events)
    {
        if (e.kind == MidiEvent::Kind::bend)
            foundBend = true;
        if (e.kind == MidiEvent::Kind::noteOn)
            notePitch = e.pitch;
    }
    REQUIRE (foundBend);
    REQUIRE (notePitch == 61); // +50c rounds to +1 st + -50c bend remainder
}

TEST_CASE ("applyPitch merges existing bend")
{
    std::vector<MidiEvent> events{ noteOn (60), bend (8192) };
    PitchStack p;
    p.pitchCents = 50.0f;
    applyPitch (events, p);
    int bendCount = 0;
    for (const auto& e : events)
        if (e.kind == MidiEvent::Kind::bend)
        {
            ++bendCount;
            REQUIRE (e.value < 8192); // remainder is -50c after rounding to +1 st
        }
    REQUIRE (bendCount == 1);
}

TEST_CASE ("applyPitch is identity for default stack")
{
    std::vector<MidiEvent> events{ noteOn (60) };
    PitchStack p;
    applyPitch (events, p);
    REQUIRE (events.size() == 1);
    REQUIRE (events[0].pitch == 60);
}
