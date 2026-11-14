/*
    Aux return engines.

    The sends sum into a stereo scratch buffer, one process() call per block, so
    a return must sound the same whatever the block size. The delay's write
    cursor is shared by both channels: advancing it per channel instead of per
    frame moved the read position a whole block every block and turned the
    return into a block-rate modulated copy of the send.
*/

#include <catch2/catch_test_macros.hpp>

#include "AuxBus.h"

#include <cmath>
#include <vector>

namespace
{
constexpr double rate = 48000.0;

std::vector<float> runImpulse (nc::AuxBus::Engine engine, int blockSize, int total)
{
    nc::AuxBus bus;
    bus.prepare (rate, blockSize, engine);

    std::vector<float> out (static_cast<size_t> (total), 0.0f);
    juce::AudioBuffer<float> block (2, blockSize);

    for (int start = 0; start < total; start += blockSize)
    {
        const auto n = juce::jmin (blockSize, total - start);
        block.clear();

        if (start == 0)
        {
            block.setSample (0, 0, 1.0f);
            block.setSample (1, 0, 1.0f);
        }

        bus.process (block, n);

        for (int i = 0; i < n; ++i)
            out[static_cast<size_t> (start + i)] = block.getSample (0, i);
    }

    return out;
}

int peakIndex (const std::vector<float>& x)
{
    int best = 0;

    for (int i = 1; i < static_cast<int> (x.size()); ++i)
        if (std::abs (x[static_cast<size_t> (i)]) > std::abs (x[static_cast<size_t> (best)]))
            best = i;

    return best;
}
} // namespace

TEST_CASE ("aux delay returns the echo at 375 ms for any block size", "[sends]")
{
    // Dotted 1/8 at 120 BPM is 375 ms, which is 18000 samples at 48 kHz.
    const auto total = static_cast<int> (rate * 0.5);

    REQUIRE (peakIndex (runImpulse (nc::AuxBus::Engine::overlayDelay, 512, total)) == 18000);
    REQUIRE (peakIndex (runImpulse (nc::AuxBus::Engine::overlayDelay, 256, total)) == 18000);
    // The Temporal engine moves its read position by up to ~70 samples of tape
    // wow and flutter, so its echo lands near 375 ms rather than exactly on it.
    const auto temporal = peakIndex (runImpulse (nc::AuxBus::Engine::temporalDelay, 512, total));
    REQUIRE (temporal >= 17920);
    REQUIRE (temporal <= 18080);
}
