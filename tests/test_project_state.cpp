#include <catch2/catch_test_macros.hpp>

#include <nc/engine/ProjectLimits.hpp>
#include <nc/engine/ProjectState.hpp>

#include <cmath>
#include <string>

using namespace nc::engine;

// RED: a session cannot be saved, so every edit since the last build dies with
// the process. The document model comes first, and it must never load a value
// the engine would refuse to run.
namespace
{
ClipState makeClip (const char* path, int track = 0)
{
    ClipState clip;
    clip.assetPath = path;
    clip.track = track;
    clip.startBar = 1.0;
    clip.lengthBars = 4.0;
    return clip;
}
} // namespace

TEST_CASE ("a saved tempo outside the central limits clamps on load")
{
    ProjectDocument doc;
    doc.bpm = 5.0;
    REQUIRE (sanitize (doc).bpm >= nc::limits::minBpm);

    doc.bpm = 9999.0;
    REQUIRE (sanitize (doc).bpm <= nc::limits::maxBpm);

    doc.bpm = std::nan ("");
    REQUIRE (std::isfinite (sanitize (doc).bpm));
}

TEST_CASE ("the visible track count clamps to the session ceiling")
{
    ProjectDocument doc;
    doc.activeTrackCount = 500;
    REQUIRE (sanitize (doc).activeTrackCount <= nc::limits::maxTracks);

    doc.activeTrackCount = 0;
    REQUIRE (sanitize (doc).activeTrackCount >= 1);
}

TEST_CASE ("an oversize clip list truncates to capacity and keeps its order")
{
    ProjectDocument doc;

    for (int i = 0; i < nc::limits::maxClips + 5; ++i)
        doc.clips.push_back (makeClip (("clip" + std::to_string (i)).c_str()));

    const auto clean = sanitize (doc);
    REQUIRE (static_cast<int> (clean.clips.size()) == nc::limits::maxClips);
    REQUIRE (clean.clips.front().assetPath == "clip0");
}

TEST_CASE ("clips with a non-finite or non-positive length are dropped, not clamped")
{
    ProjectDocument doc;
    auto silent = makeClip ("silent");
    silent.lengthBars = 0.0;
    auto negative = makeClip ("negative");
    negative.lengthBars = -2.0;
    auto broken = makeClip ("broken");
    broken.lengthBars = std::nan ("");
    doc.clips = { silent, negative, broken, makeClip ("keeper") };

    const auto clean = sanitize (doc);
    REQUIRE (clean.clips.size() == 1);
    REQUIRE (clean.clips.front().assetPath == "keeper");
}

TEST_CASE ("clip bars clamp to the central ceiling instead of running off the timeline")
{
    ProjectDocument doc;
    auto clip = makeClip ("far");
    clip.startBar = 1.0e9;
    clip.lengthBars = 1.0e9;
    doc.clips = { clip };

    const auto clean = sanitize (doc);
    REQUIRE (clean.clips.front().startBar <= static_cast<double> (nc::limits::maxBars));
    REQUIRE (clean.clips.front().lengthBars <= static_cast<double> (nc::limits::maxBars));
}

TEST_CASE ("clips without a source are dropped and their track is clamped into the session")
{
    ProjectDocument doc;
    doc.activeTrackCount = 8;

    auto sourceLess = makeClip ("");
    auto lowTrack = makeClip ("low", -5);
    auto highTrack = makeClip ("high", 999);
    doc.clips = { sourceLess, lowTrack, highTrack };

    const auto clean = sanitize (doc);
    REQUIRE (clean.clips.size() == 2);
    REQUIRE (clean.clips[0].track == 0);
    REQUIRE (clean.clips[1].track <= 7);
}

TEST_CASE ("a project written by a newer schema is refused rather than half-read")
{
    ProjectDocument doc;
    REQUIRE (isLoadable (doc));

    doc.version = projectSchemaVersion + 1;
    REQUIRE_FALSE (isLoadable (doc));
}

TEST_CASE ("track insert blobs are capped and empty slots are cleared")
{
    ProjectDocument doc;
    doc.tracks.resize (1);

    auto& valid = doc.tracks[0].inserts[0];
    valid.identifier = "slot-zero";
    valid.stateBase64 = "AAECAwQ=";

    auto& oversized = doc.tracks[0].inserts[1];
    oversized.identifier = "slot-one";
    oversized.descriptionXml = std::string (maxPluginBlobBytes + 1, 'x');
    oversized.stateBase64 = "AAECAwQ=";

    auto& metadataOnly = doc.tracks[0].inserts[2];
    metadataOnly.descriptionXml = "<PLUGIN name=\"Missing\" />";

    auto& empty = doc.tracks[0].inserts[3];
    empty.stateBase64 = "AAECAwQ=";

    const auto clean = sanitize (doc);

    REQUIRE (clean.tracks[0].inserts[0].identifier == "slot-zero");
    REQUIRE (clean.tracks[0].inserts[1].descriptionXml.empty());
    REQUIRE (clean.tracks[0].inserts[1].stateBase64 == "AAECAwQ=");
    REQUIRE_FALSE (clean.tracks[0].inserts[2].isEmpty());
    REQUIRE (clean.tracks[0].inserts[2].identifier.empty());
    REQUIRE (clean.tracks[0].inserts[3].isEmpty());
    REQUIRE (clean.tracks[0].inserts[3].stateBase64.empty());
}

TEST_CASE ("sanitising a document twice changes nothing")
{
    ProjectDocument doc;
    doc.bpm = 5.0;
    doc.activeTrackCount = 500;
    doc.clips = { makeClip ("a"), makeClip ("b", 999) };
    doc.tracks.resize (1);
    doc.tracks[0].inserts[0].identifier = "stable";
    doc.tracks[0].inserts[1].stateBase64 = "discarded";

    const auto once = sanitize (doc);
    const auto twice = sanitize (once);

    REQUIRE (once.bpm == twice.bpm);
    REQUIRE (once.activeTrackCount == twice.activeTrackCount);
    REQUIRE (once.clips.size() == twice.clips.size());
    REQUIRE (once.clips.back().track == twice.clips.back().track);
    REQUIRE (once.tracks[0].inserts[0].identifier == twice.tracks[0].inserts[0].identifier);
    REQUIRE (once.tracks[0].inserts[1].isEmpty());
}

TEST_CASE ("absolute and parent-traversing asset paths are dropped (CWE-22)")
{
    ProjectDocument doc;
    doc.clips = {
        makeClip ("Media/kick.wav"),
        makeClip ("sub/snare.wav"),
        makeClip ("/etc/passwd"),
        makeClip ("C:/Windows/win.wav"),
        makeClip ("\\\\server\\share\\evil.wav"),
        makeClip ("../secret.wav"),
        makeClip ("Media/../../secret.wav"),
        makeClip ("Media/..\\secret.wav"),
    };

    const auto clean = sanitize (doc);

    REQUIRE (clean.clips.size() == 2);
    REQUIRE (clean.clips[0].assetPath == "Media/kick.wav");
    REQUIRE (clean.clips[1].assetPath == "sub/snare.wav");
}
