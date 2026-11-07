#include <catch2/catch_test_macros.hpp>

#include <nc/engine/ProjectJson.hpp>
#include <nc/engine/ProjectState.hpp>

#include <cmath>
#include <string>

using namespace nc::engine;

namespace
{
ProjectDocument richDocument()
{
    ProjectDocument doc;
    doc.bpm = 128.5;
    doc.activeTrackCount = 24;
    doc.loopEnabled = true;
    doc.loopStartBar = 3.0;
    doc.loopEndBar = 11.0;

    TrackState lead;
    lead.name = "Lead \"Vox\" \ take 2\nnext";
    lead.gainDb = -3.5;
    lead.pan = 0.25;
    lead.muted = true;
    lead.inserts[0].identifier = "VST3-track-one";
    lead.inserts[0].descriptionXml = "<PLUGIN name=\"Lead EQ\" />";
    lead.inserts[0].format = "VST3";
    lead.inserts[0].stateBase64 = "AAECAwQ=";
    lead.inserts[0].bypassed = true;
    TrackState drums;
    drums.name = "Drums";
    drums.soloed = true;
    doc.tracks = { lead, drums };

    ClipState clip;
    clip.assetPath = "Media/kick.wav";
    clip.name = "Kick";
    clip.track = 2;
    clip.startBar = 3.0;
    clip.lengthBars = 2.0;
    clip.sourceOffsetSeconds = 0.5;
    clip.gainDb = -6.0;
    clip.fadeInBars = 0.1;
    clip.fadeOutBars = 0.2;
    doc.clips = { clip };

    doc.masterInsert.identifier = "VST3-abc123";
    doc.masterInsert.format = "VST3";
    doc.masterInsert.stateBase64 = "AAECAwQ=";
    return doc;
}
} // namespace

TEST_CASE ("a full project survives a write and read unchanged")
{
    const auto original = richDocument();
    const auto restored = fromJson (toJson (original));

    REQUIRE (restored.has_value());
    const auto& doc = *restored;

    REQUIRE (doc.bpm == 128.5);
    REQUIRE (doc.activeTrackCount == 24);
    REQUIRE (doc.loopEnabled);
    REQUIRE (doc.loopStartBar == 3.0);
    REQUIRE (doc.loopEndBar == 11.0);
    REQUIRE (doc.tracks.size() == 2);
    REQUIRE (doc.tracks[1].name == "Drums");
    REQUIRE (doc.tracks[1].soloed);
    REQUIRE (std::abs (doc.tracks[0].pan - 0.25) < 1e-9);
    REQUIRE (doc.tracks[0].inserts[0].identifier == "VST3-track-one");
    REQUIRE (doc.tracks[0].inserts[0].descriptionXml == "<PLUGIN name=\"Lead EQ\" />");
    REQUIRE (doc.tracks[0].inserts[0].format == "VST3");
    REQUIRE (doc.tracks[0].inserts[0].stateBase64 == "AAECAwQ=");
    REQUIRE (doc.tracks[0].inserts[0].bypassed);
    REQUIRE (doc.tracks[0].inserts[1].isEmpty());
    REQUIRE (doc.clips.size() == 1);
    REQUIRE (doc.clips[0].assetPath == "Media/kick.wav");
    REQUIRE (doc.clips[0].sourceOffsetSeconds == 0.5);
    REQUIRE (std::abs (doc.clips[0].fadeInBars - 0.1) < 1e-9);
    REQUIRE (doc.masterInsert.stateBase64 == "AAECAwQ=");
}

TEST_CASE ("quotes, backslashes and newlines in names come back verbatim")
{
    const auto original = richDocument();
    const auto restored = fromJson (toJson (original));

    REQUIRE (restored.has_value());
    REQUIRE (restored->tracks[0].name == original.tracks[0].name);
}

TEST_CASE ("the saved text is one readable JSON object carrying the version")
{
    const auto text = toJson (richDocument());

    REQUIRE (text.front() == '{');
    REQUIRE (text.back() == '}');
    REQUIRE (text.find ("\"version\":" + std::to_string (projectSchemaVersion)) != std::string::npos);
    REQUIRE (text.find ("\"inserts\"") != std::string::npos);
    REQUIRE (text.find ("\"clips\"") != std::string::npos);
}

TEST_CASE ("a truncated or malformed file is refused instead of half-read")
{
    REQUIRE_FALSE (fromJson ("{\"bpm\":120").has_value());
    REQUIRE_FALSE (fromJson ("not json at all").has_value());
    REQUIRE_FALSE (fromJson ("").has_value());
    REQUIRE_FALSE (fromJson ("[1,2,3]").has_value());
}

TEST_CASE ("deeply nested objects and arrays are refused, not recursed into")
{
    const auto nestedArray = [] (int depth)
    {
        std::string text (static_cast<size_t> (depth), '[');
        text += '1';
        text.append (static_cast<size_t> (depth), ']');
        return text;
    };

    const auto nestedObject = [] (int depth)
    {
        std::string text;
        for (int i = 0; i < depth; ++i)
            text += "{\"a\":";
        text += '1';
        text.append (static_cast<size_t> (depth), '}');
        return text;
    };

    REQUIRE (json::Parser::parse (nestedArray (nc::limits::maxJsonNestingDepth)).has_value());
    REQUIRE (json::Parser::parse (nestedObject (nc::limits::maxJsonNestingDepth)).has_value());
    REQUIRE_FALSE (json::Parser::parse (nestedArray (nc::limits::maxJsonNestingDepth + 1)).has_value());
    REQUIRE_FALSE (json::Parser::parse (nestedObject (nc::limits::maxJsonNestingDepth + 1)).has_value());
}

TEST_CASE ("a file with too many values is refused before it can exhaust memory")
{
    const auto count = static_cast<size_t> (nc::limits::maxJsonValues) + 1;

    std::string text = "[";
    text.reserve (count * 2 + 2);

    for (size_t i = 0; i < count; ++i)
        text += "0,";

    text.back() = ']';

    REQUIRE_FALSE (json::Parser::parse (text).has_value());
}

TEST_CASE ("missing fields fall back to a playable default")
{
    const auto doc = fromJson ("{}");

    REQUIRE (doc.has_value());
    REQUIRE (doc->version == projectSchemaVersion);
    REQUIRE (doc->bpm == 120.0);
    REQUIRE (doc->clips.empty());
}

TEST_CASE ("a version 1 project loads with empty track inserts")
{
    const auto doc = fromJson ("{\"version\":1,\"bpm\":100,\"tracks\":[{\"name\":\"Old\"}]}");

    REQUIRE (doc.has_value());
    REQUIRE (doc->version == 1);
    REQUIRE (doc->tracks.size() == 1);

    for (const auto& slot : doc->tracks[0].inserts)
        REQUIRE (slot.isEmpty());
}

TEST_CASE ("fields written by a newer build are ignored, not fatal")
{
    const auto doc = fromJson ("{\"version\":1,\"bpm\":100,\"shinyNewThing\":{\"a\":[1,2]}}");

    REQUIRE (doc.has_value());
    REQUIRE (doc->bpm == 100.0);
}

TEST_CASE ("a newer schema number survives parsing so the caller can refuse it")
{
    const auto doc = fromJson ("{\"version\":99,\"bpm\":100}");

    REQUIRE (doc.has_value());
    REQUIRE_FALSE (isLoadable (*doc));
}

TEST_CASE ("an empty project round trips")
{
    const auto restored = fromJson (toJson (ProjectDocument {}));

    REQUIRE (restored.has_value());
    REQUIRE (restored->clips.empty());
    REQUIRE (restored->tracks.empty());
    REQUIRE (restored->bpm == 120.0);
}
