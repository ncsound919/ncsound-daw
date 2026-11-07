#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nc/engine/Markers.hpp>
#include <nc/engine/ProjectJson.hpp>
#include <nc/engine/ProjectState.hpp>
#include <nc/engine/TempoMap.hpp>

#include <cmath>

using namespace nc::engine;
using Catch::Approx;

TEST_CASE ("a constant tempo map matches the legacy single-bpm math")
{
    const auto map = constantTempoMap (120.0);
    REQUIRE (map.size() == 1);
    REQUIRE (bpmAt (map, 0.0) == Approx (120.0));
    REQUIRE (bpmAt (map, 100.0) == Approx (120.0));
    REQUIRE (ppqToSeconds (map, 4.0) == Approx (2.0)); // one bar at 120 BPM
    REQUIRE (secondsToPpq (map, 2.0) == Approx (4.0));
    REQUIRE (samplesAtPpqWithMap (map, 4.0, 48000.0) == Approx (96000.0));
    REQUIRE (ppqAtSamplesWithMap (map, 96000, 48000.0) == Approx (4.0));
}

TEST_CASE ("a tempo change bends ppq to seconds correctly")
{
    TempoMap map;
    map.points = { { 0.0, 120.0 }, { 4.0, 60.0 } }; // bar 2 onwards at half speed
    sanitizeTempoMap (map);

    REQUIRE (bpmAt (map, 0.0) == Approx (120.0));
    REQUIRE (bpmAt (map, 3.99) == Approx (120.0));
    REQUIRE (bpmAt (map, 4.0) == Approx (60.0));
    REQUIRE (bpmAt (map, 8.0) == Approx (60.0));

    // First bar: 2s. Second bar at 60 BPM: 4s. Total 6s at ppq 8.
    REQUIRE (ppqToSeconds (map, 4.0) == Approx (2.0));
    REQUIRE (ppqToSeconds (map, 8.0) == Approx (6.0));
    REQUIRE (secondsToPpq (map, 6.0) == Approx (8.0));
    REQUIRE (secondsToPpq (map, 2.0) == Approx (4.0));

    // Round trip across the change.
    REQUIRE (secondsToPpq (map, ppqToSeconds (map, 6.5)) == Approx (6.5));
}

TEST_CASE ("sanitizeTempoMap sorts, dedupes, clamps and anchors at zero")
{
    TempoMap map;
    map.points = { { 8.0, 999.0 }, { 4.0, std::nan ("") }, { 4.0, 90.0 }, { 4.0, 100.0 } };
    sanitizeTempoMap (map);

    REQUIRE (map.size() == 3); // anchored 0 + ppq 4 (last wins) + ppq 8
    REQUIRE (map.points[0].ppq == Approx (0.0));
    REQUIRE (map.points[1].ppq == Approx (4.0));
    REQUIRE (map.points[1].bpm == Approx (100.0));
    REQUIRE (map.points[2].bpm == Approx (300.0)); // clamped to maxBpm

    // Idempotent.
    const auto once = map.points;
    sanitizeTempoMap (map);
    REQUIRE (map.points.size() == once.size());
    REQUIRE (map.points[1].bpm == Approx (once[1].bpm));
}

TEST_CASE ("markers sort, clamp and report the next section")
{
    std::vector<Marker> markers { { 16.0, "HOOK" }, { 0.0, "INTRO" }, { std::nan (""), "BAD" } };
    sanitizeMarkers (markers);

    REQUIRE (markers.size() == 2);
    REQUIRE (markers[0].name == "INTRO");
    REQUIRE (markers[1].name == "HOOK");

    const auto* next = markerAtOrAfter (markers, 4.0);
    REQUIRE (next != nullptr);
    REQUIRE (next->name == "HOOK");
    REQUIRE (markerAtOrAfter (markers, 99.0) == nullptr);
    REQUIRE (markerAtOrAfter (markers, std::nan ("")) == nullptr);
}

TEST_CASE ("tempo map and markers survive a project round trip")
{
    ProjectDocument doc;
    doc.bpm = 120.0;
    doc.tempoMap.points = { { 0.0, 120.0 }, { 8.0, 90.0 } };
    doc.markers = { { 0.0, "INTRO" }, { 16.0, "HOOK" } };

    const auto json = toJson (sanitize (doc));
    const auto parsed = fromJson (json);
    REQUIRE (parsed.has_value());

    const auto clean = sanitize (*parsed);
    REQUIRE (clean.tempoMap.size() == 2);
    REQUIRE (clean.tempoMap.points[1].bpm == Approx (90.0));
    REQUIRE (clean.markers.size() == 2);
    REQUIRE (clean.markers[1].name == "HOOK");
}

TEST_CASE ("v3 projects without tempo fields load as a constant map from bpm")
{
    const std::string legacy = "{"
        "\"version\":3,\"bpm\":90,\"activeTrackCount\":1,"
        "\"clips\":[]}";
    const auto parsed = fromJson (legacy);
    REQUIRE (parsed.has_value());

    const auto clean = sanitize (*parsed);
    REQUIRE (clean.tempoMap.size() == 1);
    REQUIRE (clean.tempoMap.points[0].bpm == Approx (90.0));
    REQUIRE (clean.markers.empty());
}
