// Block-path benchmark: cost of one renderBlock at realistic block sizes.
//
// This is the number optimisation has to beat. It needs the real engine (JUCE
// types, the prepared rack, the transport), so unlike tests/bench_components.cpp
// it cannot live in the pure-engine tree and cannot run on Linux. It also needs
// no audio device: the callback is suspended and the same render path the device
// uses is driven directly, so the result is machine timing rather than a driver's.
//
// Usage:
//   NCBenchRender                     JSON to stdout
//   NCBenchRender --json out.json     also write a file
//   NCBenchRender --quick             fewer blocks (smoke run)
//
// Reported per scenario and block size:
//   ns_per_op        one block
//   realtime_factor  audio time produced / wall time spent (higher is better;
//                    1.0 means the block used its entire budget)
//
// The engine is prepared at whatever rate the saved audio settings choose, so
// realtime_factor uses the engine's own reported rate rather than assuming 48k.

#include "AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

struct Row
{
    std::string scenario;
    int blockSize = 0;
    double nsPerBlock = 0.0;
    double realtimeFactor = 0.0;
};

/** Renders `blocks` blocks and returns the best per-block cost of `passes`. */
double timeBlocks (nc::AudioEngine& engine, int blockSize, int blocks, int passes)
{
    std::vector<float> left (static_cast<size_t> (blockSize), 0.0f);
    std::vector<float> right (static_cast<size_t> (blockSize), 0.0f);
    float* outputs[2] { left.data(), right.data() };

    double best = 0.0;

    for (int pass = 0; pass < passes; ++pass)
    {
        const auto start = Clock::now();

        for (int i = 0; i < blocks; ++i)
            engine.processBlockForTesting (nullptr, 0, outputs, 2, blockSize);

        const auto elapsed = std::chrono::duration<double, std::nano> (Clock::now() - start).count();
        const auto perBlock = elapsed / static_cast<double> (std::max (1, blocks));
        best = pass == 0 ? perBlock : std::min (best, perBlock);
    }

    return best;
}

std::string utcNow()
{
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t (now);
    std::tm tm {};
#if defined(_WIN32)
    gmtime_s (&tm, &seconds);
#else
    gmtime_r (&seconds, &tm);
#endif
    char buffer[32] {};
    std::strftime (buffer, sizeof (buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}
} // namespace

int main (int argc, char** argv)
{
    std::string jsonPath;
    bool quick = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--json" && i + 1 < argc)
            jsonPath = argv[++i];
        else if (arg == "--quick")
            quick = true;
        else
        {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }

    // 64 is the worst case for per-block overhead, 512 is what a shared-mode
    // WASAPI device typically hands us. Anything larger is an offline path and
    // is measured by the export tests instead.
    const std::vector<int> blockSizes { 64, 256, 512 };
    const int blocks = quick ? 80 : 600;
    const int passes = quick ? 1 : 3;

    nc::AudioEngine engine;
    // The constructor restores saved audio settings and may open a device.
    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    const auto sampleRate = engine.currentSampleRate() > 0.0 ? engine.currentSampleRate() : 48000.0;

    engine.setBpm (120.0);
    engine.setLoopEnabled (true);
    engine.setLoopBars (1.0, 9.0);
    engine.setPlaying (true);

    std::vector<Row> rows;

    // Idle: transport running, nothing to render. This is the floor a change has
    // to be measured against — it is the cost of the passes noticing there is no
    // work, not the cost of the work.
    for (const auto blockSize : blockSizes)
    {
        // Warm-up: first touch of the buffers and any lazy preparation.
        timeBlocks (engine, blockSize, 50, 1);

        const auto ns = timeBlocks (engine, blockSize, blocks, passes);
        rows.push_back ({ "idle", blockSize, ns,
                          (static_cast<double> (blockSize) / sampleRate) / (ns / 1.0e9) });
    }

    // Timeline: demo assets across every track that has one, eight clips each.
    // Uses the same publish path the UI uses, so the measured blocks run the
    // real clip and resample code rather than a test-only shortcut.
    std::vector<nc::AudioEngine::TimelineClip> clips;
    int usedTracks = 0;

    for (int track = 0; track < nc::AudioEngine::trackCount; ++track)
    {
        const auto asset = engine.demoAssetId (track);

        if (asset < 0)
            continue;

        ++usedTracks;

        for (int i = 0; i < 8; ++i)
        {
            nc::AudioEngine::TimelineClip clip;
            clip.assetId = asset;
            clip.track = track;
            clip.startBar = 1.0 + static_cast<double> (i) * 2.0;
            clip.lengthBars = 2.0;
            clips.push_back (clip);
        }
    }

    if (! clips.empty())
        engine.setTimelineClips (clips);

    // One block drains the publish commands; without it the first timed block
    // would include the clip build.
    {
        std::vector<float> left (512, 0.0f), right (512, 0.0f);
        float* outputs[2] { left.data(), right.data() };
        engine.processBlockForTesting (nullptr, 0, outputs, 2, 512);
    }

    for (const auto blockSize : blockSizes)
    {
        timeBlocks (engine, blockSize, 50, 1);

        const auto ns = timeBlocks (engine, blockSize, blocks, passes);
        rows.push_back ({ "timeline", blockSize, ns,
                          (static_cast<double> (blockSize) / sampleRate) / (ns / 1.0e9) });
    }

    std::ostringstream json;
    json << std::fixed << std::setprecision (3);
    json << "{\n";
    json << "  \"schema\": \"nc-bench/1\",\n";
    json << "  \"run\": {\n";
    json << "    \"utc\": \"" << utcNow() << "\",\n";
    json << "    \"kind\": \"render_block\",\n";
    json << "    \"sample_rate\": " << sampleRate << ",\n";
    json << "    \"tracks_with_clips\": " << usedTracks << ",\n";
    json << "    \"clips\": " << clips.size() << ",\n";
    json << "    \"blocks\": " << blocks << ",\n";
    json << "    \"passes\": " << passes << "\n";
    json << "  },\n";
    json << "  \"kernels\": [\n";

    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        const auto& r = rows[i];
        json << "    { \"name\": \"render_block." << r.scenario << '.' << r.blockSize
             << "\", \"units\": \"block\", \"ops\": " << r.blockSize
             << ", \"ns_per_op\": " << r.nsPerBlock
             << ", \"ns_total\": " << r.nsPerBlock
             << ", \"realtime_factor\": " << r.realtimeFactor << " }";

        json << (i + 1 == rows.size() ? "\n" : ",\n");
    }

    json << "  ]\n}\n";

    const auto text = json.str();
    std::cout << text;
    std::cout.flush();

    if (! jsonPath.empty())
    {
        std::ofstream file (jsonPath, std::ios::binary | std::ios::trunc);
        file << text;

        if (! file)
        {
            std::cerr << "could not write " << jsonPath << "\n";
            return 1;
        }
    }

    return 0;
}
