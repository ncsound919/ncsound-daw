#include <catch2/catch_test_macros.hpp>

#include <nc/engine/StreamWindow.hpp>

using namespace nc::engine;

// RED: long imports are held whole in RAM against a fixed cap. Streaming needs
// the window math first, so the audio callback and the reader agree on which
// frames must already be resident before a clip can sound.
TEST_CASE ("remaining frames clamp at the asset end")
{
    REQUIRE (framesRemaining (0, 1000) == 1000);
    REQUIRE (framesRemaining (400, 1000) == 600);
    REQUIRE (framesRemaining (1000, 1000) == 0);
    REQUIRE (framesRemaining (1500, 1000) == 0);
    REQUIRE (framesRemaining (-50, 1000) == 1000);
    REQUIRE (framesRemaining (0, 0) == 0);
    REQUIRE (framesRemaining (0, -5) == 0);
}

TEST_CASE ("read-ahead window never runs past the asset")
{
    auto window = planWindow (0, 1000, 400);
    REQUIRE (window.startFrame == 0);
    REQUIRE (window.lengthFrames == 400);

    window = planWindow (800, 1000, 400);
    REQUIRE (window.startFrame == 800);
    REQUIRE (window.lengthFrames == 200);

    window = planWindow (1000, 1000, 400);
    REQUIRE (window.lengthFrames == 0);

    window = planWindow (-20, 1000, 400);
    REQUIRE (window.startFrame == 0);

    window = planWindow (0, 1000, 0);
    REQUIRE (window.lengthFrames == 0);
}

TEST_CASE ("resident window covers a range or asks for a read")
{
    const StreamWindow resident { 800, 200 };

    REQUIRE (windowCovers (resident, 900, 64));
    REQUIRE_FALSE (windowCovers (resident, 700, 64));
    REQUIRE_FALSE (windowCovers (resident, 990, 32));
    REQUIRE (windowCovers (StreamWindow {}, 0, 0));

    const auto read = planRead (resident, StreamWindow { 0, 400 });
    REQUIRE (read.startFrame == 0);
    REQUIRE (read.lengthFrames == 400);

    const auto covered = planRead (resident, StreamWindow { 850, 100 });
    REQUIRE (covered.lengthFrames == 0);
}
