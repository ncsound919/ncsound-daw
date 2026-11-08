#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nc/engine/ClipTimeline.hpp>
#include <nc/engine/Comping.hpp>

using namespace nc::engine;
using namespace nc::engine::comping;
using Catch::Approx;

TEST_CASE ("equal-power halves sit at 0.707, linear halves at 0.5")
{
    const auto [outEq, inEq] = crossfadeGains (0.5, Curve::equalPower);
    REQUIRE (outEq == Approx (std::cos (3.141592653589793 / 4.0)));
    REQUIRE (inEq == Approx (std::sin (3.141592653589793 / 4.0)));

    const auto [outLin, inLin] = crossfadeGains (0.5, Curve::linear);
    REQUIRE (outLin == Approx (0.5));
    REQUIRE (inLin == Approx (0.5));

    REQUIRE (incomingGain (1.0, Curve::equalPower) == Approx (1.0));
    REQUIRE (outgoingGain (0.0, Curve::equalPower) == Approx (1.0));
}

TEST_CASE ("crossfade overlap clamps to 45% of the shorter neighbour")
{
    REQUIRE (clampCrossfadeBars (2.0, 4.0, 8.0) == Approx (1.8));
    REQUIRE (clampCrossfadeBars (0.5, 4.0, 8.0) == Approx (0.5));
    REQUIRE (clampCrossfadeBars (-1.0, 4.0, 8.0) == Approx (0.0));
    REQUIRE (overlapBars (1.0, 4.0, 4.0, 4.0) == Approx (1.0));
    REQUIRE (overlapBars (1.0, 2.0, 5.0, 2.0) == Approx (0.0));
}

TEST_CASE ("equal-power clip gain holds loudness where linear dips")
{
    // Mid-fade: linear is 0.5, equal-power is ~0.707 (-3 dB vs -6 dB).
    REQUIRE (clipGainAt (0.25, 2.0, 0.5, 0.5) == Approx (0.5));
    REQUIRE (clipGainCrossfadeAt (0.25, 2.0, 0.5, 0.5) == Approx (std::sin (3.141592653589793 / 4.0)).margin (1e-9));

    // Edges are still hard cuts for both curves.
    REQUIRE (clipGainCrossfadeAt (0.0, 2.0, 0.5, 0.5) == Approx (0.0));
    REQUIRE (clipGainCrossfadeAt (1.0, 2.0, 0.0, 0.0) == Approx (1.0));
    REQUIRE (clipGainCrossfadeAt (-1.0, 2.0, 0.5, 0.5) == Approx (0.0));
}

TEST_CASE ("comp lane keeps every take and selects non-destructively")
{
    CompLane lane;
    REQUIRE (lane.addTake ("Take 1") == 0);
    REQUIRE (lane.addTake ("Take 2", 1.5) == 1);
    REQUIRE (lane.takes.size() == 2); // nothing destroyed
    REQUIRE (lane.activeTake()->index == 0);

    REQUIRE (lane.selectTake (1));
    REQUIRE (lane.activeTake()->name == "Take 2");

    REQUIRE (lane.cycleTake (1)); // wraps to take 0
    REQUIRE (lane.activeTake()->index == 0);
    REQUIRE_FALSE (lane.selectTake (9));
}

TEST_CASE ("a comp lane caps its take list and rejects non-finite gain")
{
    CompLane lane;

    for (std::size_t i = 0; i < CompLane::maxTakes; ++i)
        REQUIRE (lane.addTake ("take " + std::to_string (i)) == static_cast<int> (i));

    REQUIRE (lane.addTake ("overflow") == -1);
    REQUIRE (lane.takes.size() == CompLane::maxTakes);

    CompLane quiet;
    REQUIRE (quiet.addTake ("nan", std::nan ("")) == 0);
    REQUIRE (quiet.takes[0].gainDb == 0.0);
}

TEST_CASE ("take selection goes by lane position, not the stored index field")
{
    CompLane lane;
    lane.takes.push_back (Take { 7, "first", 0.0, true });
    lane.takes.push_back (Take { 9, "second", 0.0, false });

    REQUIRE (lane.selectTake (1));
    REQUIRE (lane.activeIndex == 1);
    REQUIRE (lane.activeTake()->name == "second");
    REQUIRE_FALSE (lane.takes[0].active);
}
