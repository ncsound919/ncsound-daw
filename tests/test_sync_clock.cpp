#include <catch2/catch_approx.hpp>

#include <cmath>
#include <catch2/catch_test_macros.hpp>

#include <nc/engine/SyncClock.hpp>

using namespace nc::engine;
using Catch::Approx;

// RED: the engine has isPlaying() and getBpm() but no musical position, so a
// hosted plugin would have to free-run. A VST3 host owes every plugin a
// block-start ProcessContext: project time in samples (wraps at the loop),
// continuous time (never wraps), quarter-note position, bar start, tempo,
// cycle range and the distance to the next 24-PPQN clock. Derive it, do not
// accumulate it.
TEST_CASE ("bars and quarter notes map to samples at the transport tempo")
{
    REQUIRE (secondsPerBar (120.0) == Approx (2.0));
    REQUIRE (samplesPerBar (120.0, 48000.0) == Approx (96000.0));
    REQUIRE (ppqAt (96000, 120.0, 48000.0) == Approx (4.0));
    REQUIRE (samplesAtPpq (4.0, 120.0, 48000.0) == 96000);

    // The engine clamps a non-finite or sub-1 BPM tempo instead of returning zero.
    REQUIRE (std::isfinite (secondsPerBar (0.0)));
    REQUIRE (secondsPerBar (0.0) > 0.0);
    REQUIRE (ppqAt (1000, 0.0, 48000.0) == Approx (0.0));
}

TEST_CASE ("the midi clock reports samples to the next tick, never a negative wait")
{
    REQUIRE (samplesToNextClock (0, 120.0, 48000.0) == 0);
    REQUIRE (samplesToNextClock (250, 120.0, 48000.0) == 750);
    REQUIRE (samplesToNextClock (1000, 120.0, 48000.0) == 0);
    REQUIRE (samplesToNextClock (1600, 120.0, 48000.0) == 400);
}

TEST_CASE ("a loop wrap is a jump in project time but not in continuous time")
{
    REQUIRE (projectSamplesAt (5000, 0, 96000, false) == 5000);
    REQUIRE (projectSamplesAt (5000, 0, 96000, true) == 5000);
    REQUIRE (projectSamplesAt (96000, 0, 96000, true) == 0);
    REQUIRE (projectSamplesAt (97000, 0, 96000, true) == 1000);
    REQUIRE (projectSamplesAt (200000, 0, 96000, true) == 8000);

    // Before the loop start the transport is not cycling yet.
    REQUIRE (projectSamplesAt (1000, 96000, 192000, true) == 1000);
    REQUIRE (projectSamplesAt (0, 0, 0, true) == 0);
}

TEST_CASE ("a block that crosses the loop end splits at the boundary sample")
{
    const auto crossing = splitAtLoop (95000, 1024, 0, 96000, true);
    REQUIRE (crossing.firstSegment == 1000);
    REQUIRE (crossing.secondSegment == 24);

    const auto roomy = splitAtLoop (100, 1024, 0, 96000, true);
    REQUIRE (roomy.firstSegment == 1024);
    REQUIRE (roomy.secondSegment == 0);

    const auto notLooping = splitAtLoop (95000, 1024, 0, 96000, false);
    REQUIRE (notLooping.firstSegment == 1024);
    REQUIRE (notLooping.secondSegment == 0);
}

TEST_CASE ("the block context carries every field a vst3 plugin asks for")
{
    const auto block = blockContext (96000, 512, 120.0, 48000.0, true, false, 0, 0);

    REQUIRE (block.projectTimeSamples == 96000);
    REQUIRE (block.continuousTimeSamples == 96000);
    REQUIRE (block.projectTimeMusic == Approx (4.0));
    REQUIRE (block.barPositionMusic == Approx (4.0));
    REQUIRE (block.tempo == Approx (120.0));
    REQUIRE (block.isPlaying);
    REQUIRE_FALSE (block.isLooping);

    const auto wrapped = blockContext (97000, 512, 120.0, 48000.0, true, true, 0, 96000);
    REQUIRE (wrapped.projectTimeSamples == 1000);
    REQUIRE (wrapped.continuousTimeSamples == 97000);
    REQUIRE (wrapped.cycleStartMusic == Approx (0.0));
    REQUIRE (wrapped.cycleEndMusic == Approx (4.0));
    REQUIRE (wrapped.isLooping);

    const auto stopped = blockContext (0, 512, 90.0, 44100.0, false, false, 0, 0);
    REQUIRE_FALSE (stopped.isPlaying);
    REQUIRE (stopped.projectTimeMusic == Approx (0.0));
}

// RED: the transport shows seconds only, so a producer migrating from Pro
// Tools or Live cannot read bar|beat|tick off the transport. The conversion is
// pure, so it is tested here and the LCD just formats the result.
TEST_CASE ("musical position reads as bar, beat and tick")
{
    const auto atBarTwo = barBeatAt (4.0);
    REQUIRE (atBarTwo.bar == 2);
    REQUIRE (atBarTwo.beat == 1);
    REQUIRE (atBarTwo.tick == 0);

    const auto midBar = barBeatAt (5.5);
    REQUIRE (midBar.bar == 2);
    REQUIRE (midBar.beat == 2);
    REQUIRE (midBar.tick == 480);

    const auto beforeZero = barBeatAt (-1.0);
    REQUIRE (beforeZero.bar == 1);
    REQUIRE (beforeZero.beat == 1);
    REQUIRE (beforeZero.tick == 0);
}

TEST_CASE ("bar beat display saturates before integer conversion can overflow")
{
    const auto position = barBeatAt (1.0e20);

    // The last valid position is the end of the final bar, not a bar past it.
    REQUIRE (position.bar == nc::limits::maxBars);
    REQUIRE (position.beat == 4);
    REQUIRE (position.tick > 0);
}
