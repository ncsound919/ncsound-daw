#include <catch2/catch_test_macros.hpp>

#include <nc/engine/ProjectLimits.hpp>

// RED: the preview build caps sessions at 8 tracks, 256 clips, 128 bars and
// a 10-minute RAM asset. A real song needs headroom without changing the
// real-time guarantees, so these ceilings move up in one versioned place.
TEST_CASE("project limits leave preview ceilings behind")
{
    REQUIRE(nc::limits::maxTracks >= 32);
    REQUIRE(nc::limits::maxClips >= 1024);
    REQUIRE(nc::limits::commandCapacity >= nc::limits::maxClips + 16);
    REQUIRE(nc::limits::maxBars >= 512);
    REQUIRE(nc::limits::maxAssetSeconds >= 30 * 60);
    REQUIRE(nc::limits::minBpm <= 20.0);
    REQUIRE(nc::limits::maxBpm >= 300.0);
}

TEST_CASE("bpm clamps to central limits")
{
    REQUIRE(nc::limits::clampBpm (10.0) == nc::limits::minBpm);
    REQUIRE(nc::limits::clampBpm (400.0) == nc::limits::maxBpm);
    REQUIRE(nc::limits::clampBpm (120.0) == 120.0);
}

TEST_CASE("track index clamps to session size")
{
    REQUIRE(nc::limits::clampTrack ( -1, 16) == 0);
    REQUIRE(nc::limits::clampTrack (0, 16) == 0);
    REQUIRE(nc::limits::clampTrack (15, 16) == 15);
    REQUIRE(nc::limits::clampTrack (99, 16) == 15);
    REQUIRE(nc::limits::isTrackValid (7, 16));
    REQUIRE_FALSE(nc::limits::isTrackValid (16, 16));
}

TEST_CASE("asset frames clamp to central 30-minute ceiling")
{
    constexpr auto rate = 48000.0;
    // 30 minutes at 48 kHz, not the preview 10-minute cap.
    REQUIRE(nc::limits::maxAssetFrames (rate) == static_cast<int64_t> (rate * nc::limits::maxAssetSeconds));
    REQUIRE(nc::limits::clampAssetFrames (1000, rate) == 1000);
    REQUIRE(nc::limits::clampAssetFrames (48000LL * 60 * 60 * 10, rate) == nc::limits::maxAssetFrames (rate));
    REQUIRE(nc::limits::clampAssetFrames (-5, rate) == 0);
    REQUIRE(nc::limits::clampAssetFrames (1000, 0.0) == 1000);
}

TEST_CASE("active track count clamps and steps within the ceiling")
{
    REQUIRE(nc::limits::clampActiveTracks (0) == 1);
    REQUIRE(nc::limits::clampActiveTracks (16) == 16);
    REQUIRE(nc::limits::clampActiveTracks (64) == 64);
    REQUIRE(nc::limits::clampActiveTracks (999) == nc::limits::maxTracks);
    REQUIRE(nc::limits::nextActiveTracks (16, 1) == 17);
    REQUIRE(nc::limits::nextActiveTracks (63, 4) == 64);
    REQUIRE(nc::limits::nextActiveTracks (16, -1) == 15);
    REQUIRE(nc::limits::nextActiveTracks (1, -5) == 1);
}

TEST_CASE("registry scan size is bounded so a huge file cannot hang startup")
{
    REQUIRE_FALSE(nc::limits::registrySizeAcceptable (0));
    REQUIRE(nc::limits::registrySizeAcceptable (1024));
    REQUIRE(nc::limits::registrySizeAcceptable (nc::limits::maxRegistryBytes));
    REQUIRE_FALSE(nc::limits::registrySizeAcceptable (nc::limits::maxRegistryBytes + 1));
}
