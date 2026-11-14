// setTimelineClips publishes a clear plus one add per clip through the bounded
// command rail. It used to clear the message-thread mirror before that clear
// command went out, so a full queue left an empty timeline painted over a
// schedule the engine still had (DAW-16).
#include "AudioEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <nc/engine/ProjectLimits.hpp>

#include <vector>

namespace
{
std::vector<nc::AudioEngine::TimelineClip> fullTimeline (int asset)
{
    std::vector<nc::AudioEngine::TimelineClip> clips;
    clips.reserve (static_cast<size_t> (nc::limits::maxClips));

    for (int i = 0; i < nc::limits::maxClips; ++i)
    {
        nc::AudioEngine::TimelineClip clip;
        clip.assetId = asset;
        clip.track = 0;
        clip.startBar = 1.0 + i;
        clip.lengthBars = 0.5;
        clips.push_back (clip);
    }

    return clips;
}

/** Without a callback nothing drains the rail, so the queue fills at exactly
    the rate the publishes push into it. */
void detachDevice (nc::AudioEngine& engine)
{
    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();
}
} // namespace

TEST_CASE ("a timeline publish that cannot fit the queue keeps the old schedule", "[timeline]")
{
    nc::AudioEngine engine;
    detachDevice (engine);

    const auto asset = engine.demoAssetId (0);
    REQUIRE (asset >= 0);

    const auto clips = fullTimeline (asset);

    REQUIRE (engine.setTimelineClips (clips) == 0);
    REQUIRE (engine.timelineClips().size() == clips.size());
    REQUIRE (engine.lastTimelineError().isEmpty());

    // 4096 command slots and 1025 commands per publish: the fourth publish
    // cannot fit while the suspended callback holds everything already queued.
    int refused = 0;

    for (int attempt = 0; attempt < 6 && refused == 0; ++attempt)
    {
        const auto before = engine.timelineClips().size();
        const auto dropped = engine.setTimelineClips (clips);

        if (dropped == 0)
            continue;

        ++refused;

        INFO ("refused on attempt " << attempt << " with " << dropped << " clips reported");
        REQUIRE (dropped == static_cast<int> (clips.size()));
        REQUIRE (engine.lastTimelineError().isNotEmpty());
        // The failure is reported and the mirror still describes the schedule
        // the engine kept, instead of showing an empty timeline.
        REQUIRE (engine.timelineClips().size() == before);
        REQUIRE (before == clips.size());
    }

    REQUIRE (refused == 1);
}

TEST_CASE ("a refused publish does not wedge the ones after it", "[timeline]")
{
    nc::AudioEngine engine;
    detachDevice (engine);

    const auto asset = engine.demoAssetId (0);
    REQUIRE (asset >= 0);

    const auto clips = fullTimeline (asset);

    for (int attempt = 0; attempt < 8 && engine.setTimelineClips (clips) == 0; ++attempt)
    {
    }

    REQUIRE (engine.lastTimelineError().isNotEmpty());
    REQUIRE (engine.timelineClips().size() == clips.size());

    nc::AudioEngine::TimelineClip only;
    only.assetId = asset;
    only.track = 0;
    only.startBar = 1.0;
    only.lengthBars = 1.0;

    // A refusal leaves the leftover space untouched, so a publish small enough
    // to fit still goes through and the mirror follows it — the failed one did
    // not half-apply or block the rail.
    REQUIRE (engine.setTimelineClips ({ only }) == 0);
    REQUIRE (engine.lastTimelineError().isEmpty());
    REQUIRE (engine.timelineClips().size() == 1);
    REQUIRE (engine.timelineClips()[0].lengthBars == 1.0);
}
