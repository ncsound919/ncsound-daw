#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nc/engine/ClipTimeline.hpp>

#include <cmath>
#include <limits>

using namespace nc::engine;
using Catch::Approx;

TEST_CASE ("bars map to seconds at the transport tempo")
{
    REQUIRE (secondsPerBar (120.0) == Approx (2.0));
    REQUIRE (barToSeconds (1.0, 120.0) == Approx (0.0));
    REQUIRE (barToSeconds (5.0, 120.0) == Approx (8.0));
    REQUIRE (barToSeconds (5.0, 60.0) == Approx (16.0));
}

TEST_CASE ("clips are active on a half-open timeline range")
{
    REQUIRE_FALSE (clipActiveAt (0.99, 1.0, 2.0));
    REQUIRE (clipActiveAt (1.0, 1.0, 2.0));
    REQUIRE (clipActiveAt (2.99, 1.0, 2.0));
    REQUIRE_FALSE (clipActiveAt (3.0, 1.0, 2.0));
    REQUIRE_FALSE (clipActiveAt (1.0, 1.0, 0.0));
}

TEST_CASE ("source offset follows the timeline cursor")
{
    REQUIRE (sourceSecondsAt (10.0, 4.0, 0.0) == Approx (6.0));
    REQUIRE (sourceSecondsAt (10.0, 4.0, 2.5) == Approx (8.5));
    REQUIRE (sourceSecondsAt (1.0, 4.0, 2.5) == Approx (2.5));
}

TEST_CASE ("clip gain applies equal-power-free linear fades")
{
    REQUIRE (clipGainAt (-0.1, 2.0, 0.5, 0.5) == Approx (0.0));
    REQUIRE (clipGainAt (0.0, 2.0, 0.5, 0.5) == Approx (0.0));
    REQUIRE (clipGainAt (0.25, 2.0, 0.5, 0.5) == Approx (0.5));
    REQUIRE (clipGainAt (1.0, 2.0, 0.5, 0.5) == Approx (1.0));
    REQUIRE (clipGainAt (1.75, 2.0, 0.5, 0.5) == Approx (0.5));
    REQUIRE (clipGainAt (2.0, 2.0, 0.5, 0.5) == Approx (0.0));
    REQUIRE (clipGainAt (1.0, 2.0, 0.0, 0.0) == Approx (1.0));
}

TEST_CASE ("non-finite clip positions contain silence, not full gain")
{
    const auto quiet = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    // A NaN cursor inside a clip must not unmute it; non-finite fades fall back to a hard cut.
    REQUIRE (clipGainAt (quiet, 2.0, 0.5, 0.5) == 0.0);
    REQUIRE (clipGainAt (1.0, 2.0, quiet, 0.5) == 1.0);
    REQUIRE (clipGainAt (1.0, 2.0, 0.5, quiet) == 1.0);
    REQUIRE_FALSE (clipActiveAt (quiet, 0.0, 2.0));
    REQUIRE (sourceSecondsAt (quiet, 4.0, 2.5) == Catch::Approx (2.5));
    REQUIRE_FALSE (std::isnan (secondsPerBar (quiet)));
    REQUIRE (secondsPerBar (inf) >= 0.0);
}

TEST_CASE ("oversize timelines truncate to capacity instead of overflowing")
{
    // A project with more clips than the engine holds must drop the tail
    // explicitly so the UI can say so, never wrap or allocate on the callback.
    REQUIRE (truncateClipCount (10, 1024) == 10);
    REQUIRE (truncateClipCount (1024, 1024) == 1024);
    REQUIRE (truncateClipCount (1025, 1024) == 1024);
    REQUIRE (truncateClipCount (5000, 1024) == 1024);
    REQUIRE (truncateClipCount (-3, 1024) == 0);
    REQUIRE (truncateClipCount (10, 0) == 0);
}
