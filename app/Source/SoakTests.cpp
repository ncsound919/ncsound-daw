#include "AudioEngine.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include <vector>

TEST_CASE ("soak renders hosted inserts for the requested duration", "[.][soak]")
{
    nc::AudioEngine engine;
    engine.setActiveTrackCount (8);
    engine.suspendAudioCallbackForTesting();
    engine.setPlaying (true);

    const auto minutes = juce::SystemStats::getEnvironmentVariable ("NC_SOAK_MINUTES", "1").getDoubleValue();
    const auto blocks = static_cast<int> (minutes * 60.0 * 48000.0 / 512.0);

    std::vector<float> storage (2 * 512, 0.0f);
    float* channels[2] { storage.data(), storage.data() + 512 };

    for (int i = 0; i < blocks; ++i)
        engine.processBlockForTesting (nullptr, 0, channels, 2, 512);

    CHECK (engine.recordOverflows() == 0);
}
