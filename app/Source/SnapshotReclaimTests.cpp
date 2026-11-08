#include "AudioEngine.h"

#include <catch2/catch_test_macros.hpp>

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
