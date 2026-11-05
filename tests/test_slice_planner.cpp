#include <catch2/catch_test_macros.hpp>

#include <nc/engine/SlicePlanner.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

TEST_CASE ("even split produces contiguous slices")
{
    const auto slices = nc::planSlices (1000, {}, 4);

    REQUIRE (slices.size() == 4);
    REQUIRE (slices.front().start == 0);
    REQUIRE (slices.back().end == 1000);

    for (size_t i = 0; i + 1 < slices.size(); ++i)
        REQUIRE (slices[i].end == slices[i + 1].start);
}

TEST_CASE ("boundaries snap to nearby transients")
{
    const std::vector<std::int64_t> transients { 300, 520 };
    const auto slices = nc::planSlices (1000, transients, 4);

    REQUIRE (slices.size() == 4);
    REQUIRE (slices[1].start == 300);
    REQUIRE (slices[2].start == 520);
}

TEST_CASE ("transients far from the grid are ignored")
{
    const std::vector<std::int64_t> transients { 100 };
    const auto slices = nc::planSlices (1000, transients, 2);

    REQUIRE (slices.size() == 2);
    REQUIRE (slices[1].start == 500);
}

TEST_CASE ("degenerate inputs return empty")
{
    REQUIRE (nc::planSlices (0, {}, 4).empty());
    REQUIRE (nc::planSlices (1000, {}, 0).empty());
    REQUIRE (nc::planSlices (-5, {}, 4).empty());
}

TEST_CASE ("single slice covers the whole sample")
{
    const auto slices = nc::planSlices (1000, {}, 1);

    REQUIRE (slices.size() == 1);
    REQUIRE (slices[0].start == 0);
    REQUIRE (slices[0].end == 1000);
}

TEST_CASE ("transient detection finds synthetic hits")
{
    constexpr std::int64_t frames = 44100;
    std::vector<float> audio (static_cast<size_t> (frames), 0.0f);

    for (const auto hit : { 10000, 20000, 30000 })
        for (std::int64_t i = 0; i < 64; ++i)
            audio[static_cast<size_t> (hit + i)] = 0.9f;

    const float* channels[] = { audio.data() };
    const auto onsets = nc::detectTransients (channels, 1, frames, 0.45);

    REQUIRE (onsets.size() >= 2);

    for (const auto expected : { 10000, 20000, 30000 })
    {
        const auto found = std::any_of (onsets.begin(), onsets.end(), [expected] (auto o)
                                        { return std::llabs (o - expected) <= 512; });
        REQUIRE (found);
    }
}

TEST_CASE ("silence produces no transients")
{
    std::vector<float> audio (4096, 0.0f);
    const float* channels[] = { audio.data() };
    REQUIRE (nc::detectTransients (channels, 1, 4096, 0.45).empty());
}
