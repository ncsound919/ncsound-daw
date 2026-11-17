#include "AudioEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

namespace
{
/** Polls until the retained lists drain, so the test does not depend on how
    many audio callbacks happen to run on the machine. With a device open a
    snapshot is freed two blocks after it was replaced; with no device open the
    first poll frees everything. */
bool drainRetiredSnapshots (nc::AudioEngine& engine)
{
    for (int i = 0; i < 400; ++i)
    {
        engine.reclaimRetiredSnapshots();

        if (engine.retainedSnapshotCount() == 0)
            return true;

        juce::Thread::sleep (5);
    }

    return false;
}
} // namespace

TEST_CASE ("replaced tempo and MIDI snapshots are reclaimed", "[engine][snapshots]")
{
    nc::AudioEngine engine;

    for (int i = 0; i < 10000; ++i)
        engine.setBpm (60.0 + static_cast<double> (i % 120));

    for (int i = 0; i < 2000; ++i)
        engine.setMidiClips ({});

    REQUIRE (drainRetiredSnapshots (engine));
    REQUIRE (engine.retainedSnapshotCount() == 0);
    REQUIRE (engine.getBpm() >= 60.0);
}

/*  The test above replaces snapshots and lets a device callback pick them up
    whenever it happens to run, so the publish and the render never actually
    overlap. The shipped pattern is the opposite: the message thread swaps a
    map or a lane while a block is mid-flight, and the old pointer must survive
    until a later block has started. Driving one thread through the block path
    and publishing on this one is the closest a test can get to that without a
    driver, and it fails loudly (rather than intermittently) if the retirement
    rule is ever loosened. */
TEST_CASE ("publishing while blocks run keeps snapshots alive until a later block",
           "[engine][snapshots]")
{
    nc::AudioEngine engine;
    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    std::atomic<bool> stop { false };
    std::atomic<int> blocksRun { 0 };

    std::thread audio ([&]
    {
        std::vector<float> storage (2 * 512, 0.0f);
        float* channels[2] { storage.data(), storage.data() + 512 };

        while (! stop.load (std::memory_order_relaxed))
        {
            engine.processBlockForTesting (nullptr, 0, channels, 2, 512);
            blocksRun.fetch_add (1, std::memory_order_relaxed);
        }
    });

    // The publish loop below is short enough that it can finish before the block
    // thread is scheduled at all, which would test nothing. Wait for it to be
    // running first, then keep yielding so the two really do overlap.
    for (int i = 0; i < 2000 && blocksRun.load() == 0; ++i)
        juce::Thread::sleep (1);

    REQUIRE (blocksRun.load() > 0);

    nc::AutomationLane lane;
    REQUIRE (lane.addPoint (0.0, 0.5f));
    REQUIRE (lane.addPoint (4.0, 1.0f));

    const auto blocksBefore = blocksRun.load();

    for (int i = 0; i < 400; ++i)
    {
        engine.setTempoMap (nc::engine::constantTempoMap (90.0 + (i % 60)));
        engine.setTrackAutomation (i % 4, nc::AutomationParam::Volume, lane);
        engine.setMidiClips ({});
        engine.setPlaying ((i % 2) == 0);

        // Reclaiming while blocks run is the rule under test: processBlockFor-
        // Testing counts as an in-flight callback, so this poll takes the
        // two-block path instead of the "device is idle, free everything" one.
        // Before that counter existed this exact call freed a snapshot the block
        // was reading and crashed inside the loop.
        engine.pollRetiredInserts();

        if (i % 8 == 0)
            juce::Thread::sleep (1);
    }

    // Publishing is fast enough that the last publish can land between two
    // blocks; wait (bounded) for one more block to prove the two overlapped
    // rather than asserting on a scheduler coincidence.
    for (int i = 0; i < 2000 && blocksRun.load() <= blocksBefore; ++i)
        juce::Thread::sleep (1);

    REQUIRE (blocksRun.load() > blocksBefore);

    // The lists must drain while the block thread is still running: draining
    // after the join would pass even if reclaim had only ever taken the "no
    // callback, free everything" path, which is the bug that used to crash here.
    bool drainedWhileBlocksRan = false;
    const auto blocksBeforeDrain = blocksRun.load();

    for (int i = 0; i < 600; ++i)
    {
        engine.pollRetiredInserts();

        if (engine.retainedSnapshotCount() == 0)
        {
            drainedWhileBlocksRan = true;
            break;
        }

        juce::Thread::sleep (2);
    }

    const auto blocksWhileDraining = blocksRun.load();

    stop.store (true, std::memory_order_relaxed);
    audio.join();

    REQUIRE (drainedWhileBlocksRan);
    REQUIRE (blocksWhileDraining > blocksBeforeDrain);
    REQUIRE (engine.retainedSnapshotCount() == 0);
}
