#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nc/engine/Autosave.hpp>

#include <cstdint>
#include <limits>

using namespace nc::engine::autosave;

TEST_CASE ("autosave fires once per edit after debounce and interval")
{
    const Policy policy { true, 120, 15, 5 };
    const int64_t save = 0;
    const int64_t edit = 1000;
    const int64_t last = 0;

    // Still inside the 15 s debounce quiet period.
    REQUIRE_FALSE (shouldAutosave (edit + 10'000, save, edit, last, policy));
    // Past debounce but inside the 120 s interval since the last save.
    REQUIRE_FALSE (shouldAutosave (edit + 60'000, save, edit, last, policy));
    // Past both: journal this edit.
    REQUIRE (shouldAutosave (edit + 125'000, save, edit, last, policy));
    // Already journaled: same edit, later autosave stamp -> no-op.
    REQUIRE_FALSE (shouldAutosave (edit + 250'000, save, edit, edit + 125'000, policy));
}

TEST_CASE ("autosave never fires with no fresh edit or when disabled")
{
    const Policy policy { true, 120, 15, 5 };
    REQUIRE_FALSE (shouldAutosave (10'000'000, 2000, 1000, 0, policy)); // edit before save

    const Policy disabled { false, 120, 15, 5 };
    REQUIRE_FALSE (shouldAutosave (10'000'000, 0, 1000, 0, disabled));
}

TEST_CASE ("autosave and backup filenames are stable siblings")
{
    REQUIRE (autosaveFileName ("song.ncsproject") == "song.autosave.ncsproject");
    REQUIRE (backupFileName ("song.ncsproject", 3) == "song.backup3.ncsproject");
    REQUIRE (isAutosaveFileName ("song.autosave.ncsproject"));
    REQUIRE_FALSE (isAutosaveFileName ("song.ncsproject"));
    REQUIRE (backupsToPrune (8, 5) == 3);
    REQUIRE (backupsToPrune (5, 5) == 0);
    REQUIRE (nextBackupIndex (-1, 5) == 0);
    REQUIRE (nextBackupIndex (4, 5) == 0);
    REQUIRE (nextBackupIndex (2, 0) == -1);
}

TEST_CASE ("autosave is throttled from the last save even when that save is old")
{
    // The interval anchor is max(lastAutosave, lastSave): with no save for
    // hours the first journal lands one debounce after the edit, not 2 min
    // after it. This test pins the documented behaviour.
    const Policy policy { true, 120, 15, 5 };
    const int64_t now = 10'000'000;
    REQUIRE (shouldAutosave (now, 0, now - 16'000, 0, policy));
}

TEST_CASE ("absurd policy values saturate instead of overflowing")
{
    const Policy huge { true, std::numeric_limits<int64_t>::max(),
                        std::numeric_limits<int64_t>::max(), 5 };
    REQUIRE_FALSE (shouldAutosave (10'000'000, 0, 1000, 0, huge));

    const Policy negative { true, -5, -5, 5 };
    REQUIRE (shouldAutosave (1000, 0, 1000, 0, negative));
}
