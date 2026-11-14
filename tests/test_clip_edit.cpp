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

TEST_CASE ("undo depth is configurable and drops the oldest entries")
{
    UndoStack<int> undo (3);
    REQUIRE (undo.maxDepthLimit() == 3);

    undo.push (1);
    undo.push (2);
    undo.push (3);
    undo.push (4);
    REQUIRE (undo.undoDepth() == 3);

    const auto back = undo.undo (99);
    REQUIRE (back.value() == 4); // newest survives; 1 was dropped when 4 arrived
}

TEST_CASE ("crossfade sets clamp to 45% and splits clear the seam")
{
    std::vector<ClipState> clips { makeClip (1.0, 4.0) };
    clips[0].crossfadeBars = 0.5;

    REQUIRE (setClipCrossfade (clips, 0, 99.0));
    REQUIRE (clips[0].crossfadeBars == Approx (1.8));

    REQUIRE (splitClipAt (clips, 0, 3.0, 120.0));
    REQUIRE (clips[0].crossfadeBars == Approx (0.0));
    REQUIRE (clips[1].crossfadeBars == Approx (0.0));
}

TEST_CASE ("comp take cycles within the take count")
{
    std::vector<ClipState> clips { makeClip (1.0, 2.0) };
    clips[0].compTake = 1;

    REQUIRE (cycleCompTake (clips, 0, 1, 3));
    REQUIRE (clips[0].compTake == 2);
    REQUIRE (cycleCompTake (clips, 0, 1, 3));
    REQUIRE (clips[0].compTake == 0);
    REQUIRE (selectCompTake (clips, 0, -5));
    REQUIRE (clips[0].compTake == 0);
}

TEST_CASE ("comp take cycling wraps within the clamped maximum lane count")
{
    std::vector<ClipState> clips { makeClip (1.0, 2.0) };
    constexpr int count = static_cast<int> (comping::CompLane::maxTakes);
    clips[0].compTake = count - 1;

    REQUIRE (cycleCompTake (clips, 0, 1, count + 1));
    REQUIRE (clips[0].compTake == 0);
}

TEST_CASE ("right trim growth is limited by the project bar ceiling")
{
    std::vector<ClipState> clips { makeClip (1.0, 4.0) };

    REQUIRE (trimClipRight (clips, 0, -5000.0));
    REQUIRE (clips[0].lengthBars == Approx (nc::limits::maxBars));
}

TEST_CASE ("splitting a MIDI clip divides notes instead of copying them")
{
    ClipState midi = makeClip (1.0, 8.0);
    midi.isMidi = true;
    midi.assetPath.clear();

    midi.notes.push_back ({ 1.0, 2.0, 60, 100, 0 });  // left half only
    midi.notes.push_back ({ 15.0, 4.0, 62, 100, 0 }); // straddles the split at beat 16
    midi.notes.push_back ({ 20.0, 1.0, 64, 100, 0 }); // right half only
    midi.ccs.push_back ({ 2.0, 0, 64, 127 });
    midi.ccs.push_back ({ 20.0, 0, 64, 0 });
    midi.bends.push_back ({ 24.0, 0, 12000 });

    std::vector<ClipState> clips { midi };
    REQUIRE (splitClipAt (clips, 0, 5.0, 120.0)); // 4 bars = 16 beats

    const auto& left = clips[0];
    const auto& right = clips[1];

    REQUIRE (left.notes.size() == 2);
    REQUIRE (left.notes[0].pitch == 60);
    REQUIRE (left.notes[0].startBeat == Approx (1.0));
    REQUIRE (left.notes[1].pitch == 62);
    REQUIRE (left.notes[1].lengthBeats == Approx (1.0)); // truncated at the split

    REQUIRE (right.notes.size() == 2);
    REQUIRE (right.notes[0].pitch == 62);
    REQUIRE (right.notes[0].startBeat == Approx (0.0)); // rebased to the new start
    REQUIRE (right.notes[0].lengthBeats == Approx (3.0));
    REQUIRE (right.notes[1].pitch == 64);
    REQUIRE (right.notes[1].startBeat == Approx (4.0));

    REQUIRE (left.ccs.size() == 1);
    REQUIRE (left.ccs[0].value == 127);
    REQUIRE (right.ccs.size() == 1);
    REQUIRE (right.ccs[0].beat == Approx (4.0));

    REQUIRE (left.bends.empty());
    REQUIRE (right.bends.size() == 1);
    REQUIRE (right.bends[0].beat == Approx (8.0));
}

TEST_CASE ("trimming clamps fades and crossfade to the new length")
{
    std::vector<ClipState> clips { makeClip (1.0, 4.0) };
    clips[0].crossfadeBars = 1.8;
    clips[0].fadeInBars = 1.0;
    clips[0].fadeOutBars = 1.5;

    REQUIRE (trimClipRight (clips, 0, 3.0));
    REQUIRE (clips[0].lengthBars == Approx (1.0));
    REQUIRE (clips[0].fadeInBars <= 0.45 + 1e-9);
    REQUIRE (clips[0].fadeOutBars <= 0.45 + 1e-9);
    REQUIRE (clips[0].crossfadeBars <= 0.45 + 1e-9);
}

TEST_CASE ("a negative crossfade is rejected instead of silently clearing")
{
    std::vector<ClipState> clips { makeClip (1.0, 4.0) };
    clips[0].crossfadeBars = 0.5;

    REQUIRE_FALSE (setClipCrossfade (clips, 0, -1.0));
    REQUIRE (clips[0].crossfadeBars == Approx (0.5));
}

TEST_CASE ("comp take selection clamps to the comping ceiling")
{
    std::vector<ClipState> clips { makeClip (1.0, 2.0) };

    REQUIRE (selectCompTake (clips, 0, 9999));
    REQUIRE (clips[0].compTake == static_cast<int> (comping::CompLane::maxTakes) - 1);
}
