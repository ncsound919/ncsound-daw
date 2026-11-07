#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nc/engine/ClipEdit.hpp>
#include <nc/engine/UndoStack.hpp>

using namespace nc::engine;
using Catch::Approx;

namespace
{
ClipState makeClip (double startBar, double lengthBars)
{
    ClipState clip;
    clip.assetPath = "Media/a.wav";
    clip.name = "A";
    clip.track = 0;
    clip.startBar = startBar;
    clip.lengthBars = lengthBars;
    return clip;
}
} // namespace

TEST_CASE ("split divides a clip and advances the right source offset")
{
    std::vector<ClipState> clips { makeClip (1.0, 8.0) };

    REQUIRE (splitClipAt (clips, 0, 5.0));
    REQUIRE (clips.size() == 2);
    REQUIRE (clips[0].lengthBars == Approx (4.0));
    REQUIRE (clips[1].startBar == Approx (5.0));
    REQUIRE (clips[1].lengthBars == Approx (4.0));
    REQUIRE (clips[1].sourceOffsetSeconds == Approx (8.0)); // 4 bars * 2 s/bar at 120 BPM
    REQUIRE (clips[0].fadeOutBars == Approx (0.0));         // split edge loses its fade
    REQUIRE (clips[1].fadeInBars == Approx (0.0));
}

TEST_CASE ("split refuses edges and bad indices")
{
    std::vector<ClipState> clips { makeClip (1.0, 4.0) };

    REQUIRE_FALSE (splitClipAt (clips, 0, 1.0)); // at start
    REQUIRE_FALSE (splitClipAt (clips, 0, 5.0)); // at end
    REQUIRE_FALSE (splitClipAt (clips, 0, 9.0)); // past end
    REQUIRE_FALSE (splitClipAt (clips, 7, 2.0)); // bad index
    REQUIRE (clips.size() == 1);
}

TEST_CASE ("move clamps to the session and trim keeps source alignment")
{
    std::vector<ClipState> clips { makeClip (1.0, 4.0) };
    clips[0].sourceOffsetSeconds = 2.0;

    REQUIRE (moveClip (clips, 0, 99999.0, 999, 16));
    REQUIRE (clips[0].startBar == Approx (1024.0));
    REQUIRE (clips[0].track == 15);

    // Fresh clip for source-alignment check (move clamp above saturates start).
    std::vector<ClipState> fresh { makeClip (5.0, 4.0) };
    fresh[0].sourceOffsetSeconds = 2.0;
    REQUIRE (trimClipLeft (fresh, 0, 1.0));
    REQUIRE (fresh[0].startBar == Approx (6.0));
    REQUIRE (fresh[0].lengthBars == Approx (3.0));
    REQUIRE (fresh[0].sourceOffsetSeconds == Approx (4.0)); // 2.0 + 1 bar * 2 s/bar

    REQUIRE (trimClipRight (fresh, 0, 1.5));
    REQUIRE (fresh[0].lengthBars == Approx (1.5));
    REQUIRE_FALSE (trimClipRight (fresh, 0, 99.0) == false); // clamped, still applies a trim
}

TEST_CASE ("delete removes exactly one clip")
{
    std::vector<ClipState> clips { makeClip (1.0, 2.0), makeClip (5.0, 2.0) };
    REQUIRE (deleteClipAt (clips, 0));
    REQUIRE (clips.size() == 1);
    REQUIRE (clips[0].startBar == Approx (5.0));
    REQUIRE_FALSE (deleteClipAt (clips, 9));
}

TEST_CASE ("undo stack walks back and forward and clears redo on new edit")
{
    UndoStack<std::vector<ClipState>> undo;
    const std::vector<ClipState> v0 { makeClip (1.0, 2.0) };
    const std::vector<ClipState> v1 { makeClip (1.0, 2.0), makeClip (5.0, 2.0) };
    const std::vector<ClipState> v2 { makeClip (9.0, 2.0) };

    REQUIRE_FALSE (undo.canUndo());
    undo.push (v0);
    undo.push (v1);
    REQUIRE (undo.undoDepth() == 2);

    const auto back1 = undo.undo (v2);
    REQUIRE (back1.has_value());
    REQUIRE (back1->size() == 2);
    REQUIRE (undo.canRedo());

    const auto fwd = undo.redo (*back1);
    REQUIRE (fwd.has_value());
    REQUIRE (fwd->size() == 1);
    REQUIRE (fwd->front().startBar == Approx (9.0));

    // A new edit after undo clears the redo branch.
    undo.undo (*fwd);
    undo.push (v0);
    REQUIRE_FALSE (undo.canRedo());
}
