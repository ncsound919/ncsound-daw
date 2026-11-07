#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <nc/engine/ProjectJson.hpp>
#include <nc/engine/ProjectState.hpp>

#include <cmath>
#include <limits>
#include <string>

using namespace nc::engine;
using Catch::Approx;

TEST_CASE ("project pitch round-trips global track and instrument")
{
    ProjectDocument doc;
    doc.bpm = 120.0;
    doc.globalPitch = { 5, -25.0f, 445.0f };
    TrackState track;
    track.name = "Lead";
    track.pitch = { -7, 30.0f, 432.0f };
    track.instrumentPitch = { 3, -10.0f, 442.0f };
    doc.tracks = { track };

    const auto json = toJson (sanitize (doc));
    const auto restored = fromJson (json);
    REQUIRE (restored.has_value());
    REQUIRE (restored->version == projectSchemaVersion);
    REQUIRE (restored->globalPitch.transpose == 5);
    REQUIRE (restored->globalPitch.pitchCents == Catch::Approx (-25.0f).margin (1e-4f));
    REQUIRE (restored->globalPitch.tuningHz == Catch::Approx (445.0f).margin (1e-4f));
    REQUIRE (restored->tracks.size() == 1);
    REQUIRE (restored->tracks[0].pitch.transpose == -7);
    REQUIRE (restored->tracks[0].pitch.pitchCents == Catch::Approx (30.0f).margin (1e-4f));
    REQUIRE (restored->tracks[0].pitch.tuningHz == Catch::Approx (432.0f).margin (1e-4f));
    REQUIRE (restored->tracks[0].instrumentPitch.transpose == 3);
    REQUIRE (restored->tracks[0].instrumentPitch.pitchCents == Catch::Approx (-10.0f).margin (1e-4f));
    REQUIRE (restored->tracks[0].instrumentPitch.tuningHz == Catch::Approx (442.0f).margin (1e-4f));
}

TEST_CASE ("pitch clamps hostile values on sanitize")
{
    ProjectDocument doc;
    doc.globalPitch = { 1000, 500.0f, 900.0f };
    doc.tracks.resize (1);
    doc.tracks[0].pitch.transpose = -1000;
    doc.tracks[0].pitch.pitchCents = std::nan ("");
    doc.tracks[0].pitch.tuningHz = std::numeric_limits<float>::infinity();

    const auto clean = sanitize (doc);
    REQUIRE (clean.globalPitch.transpose == PitchStack::kTransposeMax);
    REQUIRE (clean.globalPitch.pitchCents == Catch::Approx (PitchStack::kPitchMaxCents));
    REQUIRE (clean.globalPitch.tuningHz == Catch::Approx (PitchStack::kHzMax));
    REQUIRE (clean.tracks[0].pitch.transpose == PitchStack::kTransposeMin);
    REQUIRE (clean.tracks[0].pitch.pitchCents == Catch::Approx (0.0f));
    REQUIRE (clean.tracks[0].pitch.tuningHz == Catch::Approx (PitchStack::kHzDefault));
}

TEST_CASE ("schema bumps to 6 and old files load with defaults")
{
    REQUIRE (projectSchemaVersion == 6);
    const auto doc = fromJson ("{\"version\":5,\"bpm\":120}");
    REQUIRE (doc.has_value());
    REQUIRE (doc->globalPitch.transpose == 0);
    REQUIRE (doc->globalPitch.tuningHz == Catch::Approx (PitchStack::kHzDefault));
}

TEST_CASE ("toJson emits pitch fields")
{
    ProjectDocument doc;
    doc.tracks.resize (1);
    const auto json = toJson (sanitize (doc));
    REQUIRE (json.find ("\"pitch\"") != std::string::npos);
    REQUIRE (json.find ("\"instrumentPitch\"") != std::string::npos);
    REQUIRE (json.find ("\"version\":" + std::to_string (projectSchemaVersion)) != std::string::npos);
}
