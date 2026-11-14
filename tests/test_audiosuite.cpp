#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nc/engine/AudioSuite.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace nc::engine::audiosuite;
using Catch::Approx;

namespace
{
struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;

    explicit Stereo (int frames, float value = 0.0f)
        : left (static_cast<size_t> (frames), value),
          right (static_cast<size_t> (frames), value) {}

    RenderView view (double sampleRate = 48000.0)
    {
        return { left.data(), right.data(), static_cast<int> (left.size()), sampleRate };
    }
};
} // namespace

// RED: clip effects lived only in the offline demo generator. An AudioSuite
// style workflow needs deterministic, destructive processing on a clip.
TEST_CASE ("lofi quantises to the requested bit depth")
{
    Stereo audio (8, 0.0f);

    for (int i = 0; i < 8; ++i)
        audio.left[static_cast<size_t> (i)] = audio.right[static_cast<size_t> (i)] = 0.3f * static_cast<float> (i + 1);

    auto view = audio.view();
    LoFiParams params;
    params.bits = 8.0f;
    params.crush = 1.0f;
    params.wet = 1.0f;
    processLoFi (view, params);

    for (int i = 0; i < 8; ++i)
    {
        const auto quantised = std::round (audio.left[static_cast<size_t> (i)] * 128.0f) / 128.0f;
        REQUIRE (audio.left[static_cast<size_t> (i)] == Approx (quantised).margin (1.0e-6));
    }
}

TEST_CASE ("lofi at zero wet passes the source through untouched")
{
    Stereo audio (4);
    audio.left = { 0.1f, -0.2f, 0.35f, -0.9f };
    audio.right = audio.left;

    auto view = audio.view();
    LoFiParams params;
    params.wet = 0.0f;
    processLoFi (view, params);

    REQUIRE (audio.left[2] == Approx (0.35f));
    REQUIRE (audio.right[3] == Approx (-0.9f));
}


TEST_CASE ("varifi at unity rate is identity and at double rate reads every other frame")
{
    Stereo unity (5);
    for (int i = 0; i < 5; ++i)
        unity.left[static_cast<size_t> (i)] = unity.right[static_cast<size_t> (i)] = static_cast<float> (i) * 0.1f;

    auto unityView = unity.view();
    VarFiParams hold;
    hold.startRate = 1.0;
    hold.endRate = 1.0;
    processVarFi (unityView, hold);

    for (int i = 0; i < 5; ++i)
        REQUIRE (unity.left[static_cast<size_t> (i)] == Approx (static_cast<float> (i) * 0.1f).margin (1.0e-6));

    Stereo fast (4);
    for (int i = 0; i < 12; ++i)
    {
        const auto value = static_cast<float> (i) * 0.05f;
        if (i < 4) fast.left[static_cast<size_t> (i)] = fast.right[static_cast<size_t> (i)] = value;
    }

    Stereo source (12);
    for (int i = 0; i < 12; ++i)
        source.left[static_cast<size_t> (i)] = source.right[static_cast<size_t> (i)] = static_cast<float> (i) * 0.05f;

    Stereo out (4);
    auto outView = out.view();
    VarFiParams doubleSpeed;
    doubleSpeed.startRate = 2.0;
    doubleSpeed.endRate = 2.0;
    processVarFiInto (outView, source.view(), doubleSpeed);

    for (int i = 0; i < 4; ++i)
        REQUIRE (out.left[static_cast<size_t> (i)] == Approx (source.left[static_cast<size_t> (i * 2)]).margin (1.0e-5));
}

TEST_CASE ("lofi clamps an extreme crush to a bounded hold")
{
    Stereo audio (8, 0.25f);
    auto view = audio.view();
    LoFiParams params;
    params.crush = std::numeric_limits<float>::max();

    processLoFi (view, params);
    applyGain (view, std::numeric_limits<float>::max());

    for (const auto sample : audio.left)
        REQUIRE (std::isfinite (sample));
    for (const auto sample : audio.right)
        REQUIRE (std::isfinite (sample));
}

TEST_CASE ("fast track chain is neutral at its default-neutral settings")
{
    Stereo audio (6);
    audio.left = { 0.0f, 0.25f, -0.5f, 0.75f, -0.25f, 0.1f };
    audio.right = audio.left;

    auto view = audio.view();
    FastTrackParams neutral;
    neutral.driveDb = 0.0f;
    neutral.highPassHz = 0.0f;
    neutral.presenceDb = 0.0f;
    processFastTrack (view, neutral);

    REQUIRE (audio.left[2] == Approx (-0.5f).margin (1.0e-6));
    REQUIRE (audio.left[3] == Approx (0.75f).margin (1.0e-6));
}

TEST_CASE ("normalize reaches the target peak without clipping")
{
    Stereo audio (4);
    audio.left = { 0.2f, -0.4f, 0.1f, 0.0f };
    audio.right = audio.left;

    auto view = audio.view();
    normalize (view, 0.9f);

    REQUIRE (peakOf (audio.view()) == Approx (0.9f).margin (1.0e-5));

    Stereo silent (4);
    auto silentView = silent.view();
    normalize (silentView, 0.9f);
    REQUIRE (peakOf (silent.view()) == 0.0f);
}

TEST_CASE ("reverse, invert and gain are exact")
{
    Stereo audio (4);
    audio.left = { 0.1f, 0.2f, 0.3f, 0.4f };
    audio.right = audio.left;

    auto view = audio.view();
    reverse (view);
    REQUIRE (audio.left[0] == Approx (0.4f));
    REQUIRE (audio.left[3] == Approx (0.1f));

    invert (view);
    REQUIRE (audio.left[0] == Approx (-0.4f));

    applyGain (view, -6.0f);
    REQUIRE (audio.left[0] == Approx (-0.4f * 0.501187f).margin (1.0e-4));
    REQUIRE (peakOf (audio.view()) == Approx (0.200475f).margin (1.0e-4));
}

TEST_CASE ("silence zeroes only the requested range")
{
    Stereo audio (6, 0.5f);
    auto view = audio.view();

    silenceRange (view, 2, 3);

    REQUIRE (audio.left[1] == Approx (0.5f));
    REQUIRE (audio.left[2] == 0.0f);
    REQUIRE (audio.right[4] == 0.0f);
    REQUIRE (audio.left[5] == Approx (0.5f));

    silenceRange (view, -5, 2);
    silenceRange (view, 10, 2);
    REQUIRE (audio.left[0] == 0.0f);
}

TEST_CASE ("strip silence reports a leading and trailing trim")
{
    Stereo audio (10, 0.0f);
    audio.left[3] = 0.4f;
    audio.left[4] = 0.5f;
    audio.right[3] = 0.4f;
    audio.right[4] = 0.5f;

    const auto trim = stripSilence (audio.view(), 0.01f);
    REQUIRE (trim.firstFrame == 3);
    REQUIRE (trim.frameCount == 2);

    Stereo silent (8);
    const auto empty = stripSilence (silent.view(), 0.01f);
    REQUIRE (empty.frameCount == 0);
}
