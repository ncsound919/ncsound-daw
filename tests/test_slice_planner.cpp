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

TEST_CASE ("a transient at sample zero is not lost")
{
    std::vector<float> audio (4096, 0.0f);

    for (std::int64_t i = 0; i < 64; ++i)
        audio[static_cast<size_t> (i)] = 0.8f;

    const float* channels[] = { audio.data() };
    const auto onsets = nc::detectTransients (channels, 1, 4096, 0.45);

    REQUIRE (! onsets.empty());
    REQUIRE (onsets.front() == 0);
}

TEST_CASE ("vibrato does not become a stream of false onsets")
{
    constexpr std::int64_t frames = 88200;
    constexpr double sampleRate = 44100.0;
    std::vector<float> audio (static_cast<size_t> (frames), 0.0f);
    double phase = 0.0;

    for (std::int64_t i = 0; i < frames; ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        const auto bend = std::pow (2.0, 40.0 * std::sin (2.0 * 3.14159265358979323846 * 5.0 * t) / 1200.0);
        phase += 2.0 * 3.14159265358979323846 * 440.0 * bend / sampleRate;
        audio[static_cast<size_t> (i)] = static_cast<float> (0.45 * std::sin (phase));
    }

    const float* channels[] = { audio.data() };
    const auto onsets = nc::detectTransients (channels, 1, frames, 0.45);

    REQUIRE (onsets.size() <= 1);
}

TEST_CASE ("higher sensitivity finds at least as many onsets")
{
    constexpr std::int64_t frames = 30000;
    std::vector<float> audio (static_cast<size_t> (frames), 0.0f);

    for (std::int64_t i = 0; i < 64; ++i)
        audio[static_cast<size_t> (5000 + i)] = 0.9f;

    // A deliberately quiet second hit: the low setting should reject it, the
    // high setting should keep it.
    for (std::int64_t i = 0; i < 64; ++i)
        audio[static_cast<size_t> (20000 + i)] = 0.02f;

    const float* channels[] = { audio.data() };
    const auto low = nc::detectTransients (channels, 1, frames, 0.0);
    const auto high = nc::detectTransients (channels, 1, frames, 1.0);

    CAPTURE (low.size(), high.size());
    REQUIRE (high.size() > low.size());
}

TEST_CASE ("a quiet hit after a loud one is still detected")
{
    constexpr std::int64_t frames = 30000;
    std::vector<float> audio (static_cast<size_t> (frames), 0.0f);

    for (std::int64_t i = 0; i < 64; ++i)
        audio[static_cast<size_t> (5000 + i)] = 0.9f;

    for (std::int64_t i = 0; i < 64; ++i)
        audio[static_cast<size_t> (20000 + i)] = 0.04f;

    const float* channels[] = { audio.data() };
    const auto onsets = nc::detectTransients (channels, 1, frames, 0.45);
    const auto quiet = std::any_of (onsets.begin(), onsets.end(), [] (auto onset)
                                    { return std::llabs (onset - 20000) <= 512; });

    REQUIRE (quiet);
}

TEST_CASE ("stereo transients are detected from the mixed spectrum")
{
    constexpr std::int64_t frames = 16384;
    std::vector<float> left (static_cast<size_t> (frames), 0.0f);
    std::vector<float> right (static_cast<size_t> (frames), 0.0f);

    for (std::int64_t i = 0; i < 64; ++i)
    {
        left[static_cast<size_t> (4000 + i)] = 0.7f;
        right[static_cast<size_t> (11000 + i)] = -0.7f;
    }

    const float* channels[] = { left.data(), right.data() };
    const auto onsets = nc::detectTransients (channels, 2, frames, 0.45);

    for (const auto expected : { 4000, 11000 })
    {
        const auto found = std::any_of (onsets.begin(), onsets.end(), [expected] (auto onset)
                                        { return std::llabs (onset - expected) <= 512; });
        REQUIRE (found);
    }
}

TEST_CASE ("planned slices cover the sample without tiny fragments")
{
    const std::vector<std::int64_t> transients { 10, 50, 390, 410, 500, 900, 990 };
    const auto slices = nc::planSlices (1000, transients, 4);

    REQUIRE (! slices.empty());
    REQUIRE (slices.front().start == 0);
    REQUIRE (slices.back().end == 1000);

    for (size_t i = 0; i < slices.size(); ++i)
    {
        REQUIRE (slices[i].end > slices[i].start);
        REQUIRE (slices[i].end - slices[i].start >= 32);

        if (i + 1 < slices.size())
            REQUIRE (slices[i].end == slices[i + 1].start);
    }
}
