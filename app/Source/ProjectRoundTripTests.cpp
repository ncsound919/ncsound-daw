#include "ProjectFile.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

TEST_CASE ("a project document survives a save and load", "[project]")
{
    nc::engine::ProjectDocument doc;
    doc.bpm = 128.0;
    doc.activeTrackCount = 2;

    nc::engine::TrackState drums;
    drums.name = "Drums";
    drums.gainDb = -6.0;
    doc.tracks.push_back (drums);

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("nc-project-test");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto file = dir.getChildFile ("session.ncproj");

    juce::String error;
    REQUIRE (saveProjectDocument (file, doc, error));
    const auto loaded = loadProjectDocument (file, error);
    REQUIRE (loaded.has_value());
    CHECK (loaded->bpm == Catch::Approx (128.0));
    CHECK (loaded->activeTrackCount == 2);
    REQUIRE (loaded->tracks.size() == 1);
    CHECK (loaded->tracks[0].name == "Drums");
    CHECK (loaded->tracks[0].gainDb == Catch::Approx (-6.0));

    dir.deleteRecursively();
}

TEST_CASE ("a save that cannot replace the target keeps the old file", "[project]")
{
    nc::engine::ProjectDocument doc;
    doc.bpm = 120.0;
    doc.activeTrackCount = 1;

    nc::engine::ProjectDocument otherDoc;
    otherDoc.bpm = 140.0;
    otherDoc.activeTrackCount = 4;

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("nc-project-test-atomic");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto file = dir.getChildFile ("session.ncproj");

    juce::String error;
    REQUIRE (saveProjectDocument (file, doc, error));
    const auto before = file.loadFileAsString();

    {
        // Hold the target open for writing so the replace step fails on Windows
        juce::FileOutputStream hold (file);
        REQUIRE (hold.openedOk());

        const auto ok = saveProjectDocument (file, otherDoc, error);
        CHECK_FALSE (ok);
        CHECK (error.isNotEmpty());
    }

    CHECK (file.loadFileAsString() == before);

    dir.deleteRecursively();
}

TEST_CASE ("refuses to open a project saved by a newer schema version", "[project]")
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("nc-project-test-version");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto file = dir.getChildFile ("future.ncproj");

    file.replaceWithText (R"({"version": 999, "bpm": 120.0, "activeTrackCount": 1})");

    juce::String error;
    const auto loaded = loadProjectDocument (file, error);
    CHECK_FALSE (loaded.has_value());
    CHECK (error.containsIgnoreCase ("newer version"));

    dir.deleteRecursively();
}
