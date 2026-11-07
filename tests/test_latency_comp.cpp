#include <catch2/catch_test_macros.hpp>

#include <nc/engine/LatencyComp.hpp>

#include <array>

using namespace nc::engine;

// RED: nothing in the engine reports or compensates plugin latency, so the
// moment a latent plugin exists every other track is early against it. These
// are the graph rules JUCE, Tracktion and Ardour share: a node's latency is
// the worst input plus its own, connections into a summing point are delayed
// by the difference, and the graph's latency is the longest terminal path.
TEST_CASE ("a series chain sums latency and a parallel branch is delayed by the difference")
{
    REQUIRE (nodeLatency (0, 0) == 0);
    REQUIRE (nodeLatency (0, 30) == 30);
    REQUIRE (nodeLatency (30, 20) == 50);
    REQUIRE (nodeLatency (-10, -20) == 0);

    REQUIRE (connectionDelay (30, 30) == 0);
    REQUIRE (connectionDelay (30, 10) == 20);
    REQUIRE (connectionDelay (10, 30) == 0);
}

TEST_CASE ("graph latency is the longest terminal path, not the sum of every plugin")
{
    const std::array<int, 3> terminals { 30, 5, 12 };
    REQUIRE (totalLatency (terminals.data(), terminals.size()) == 30);
    REQUIRE (totalLatency (nullptr, 0) == 0);
}

TEST_CASE ("a track is delayed by the difference to the worst track")
{
    REQUIRE (compensationDelay (30, 30) == 0);
    REQUIRE (compensationDelay (30, 5) == 25);
    REQUIRE (compensationDelay (5, 30) == 0);
    REQUIRE (compensationDelay (-1, 5) == 0);
}

TEST_CASE ("offline render pre-rolls and drops exactly the chain latency")
{
    const auto plan = planRender (480, 960);
    REQUIRE (plan.prerollFrames == 480);
    REQUIRE (plan.dropFrames == 480);
    REQUIRE (plan.tailFrames == 960);

    const auto none = planRender (0, 0);
    REQUIRE (none.prerollFrames == 0);
    REQUIRE (none.dropFrames == 0);
    REQUIRE (none.tailFrames == 0);

    const auto negative = planRender (-5, -5);
    REQUIRE (negative.prerollFrames == 0);
    REQUIRE (negative.tailFrames == 0);
}
