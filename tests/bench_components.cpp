// Component benchmark harness for the JUCE-free engine.
//
// Kaggle (and any CI box) can build only `nc_engine` + this target, because the
// root CMakeLists skips every JUCE target when third_party/JUCE is absent. The
// numbers measure the kernels the audio callback and the message-thread import
// path actually call, so a regression shows up as a slower ns/op rather than as
// an anecdote about the GUI feeling heavy.
//
// Usage:
//   nc_bench                      JSON to stdout
//   nc_bench --json results.json  JSON to a file as well
//   nc_bench --quick              one pass per kernel (fast smoke run)
//
// Every kernel runs a warm-up pass, then three timed passes; the fastest is
// reported. Timing is wall clock on this machine, so treat absolute values as
// machine-specific and compare runs against a stored baseline instead.

#include <nc/engine/ClipTimeline.hpp>
#include <nc/engine/Export.hpp>
#include <nc/engine/MidiClip.hpp>
#include <nc/engine/MidiSchedule.hpp>
#include <nc/engine/Resample.hpp>
#include <nc/engine/SpscQueue.hpp>
#include <nc/engine/StreamWindow.hpp>
#include <nc/engine/TempoMap.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

volatile double sink = 0.0;

struct Result
{
    std::string name;
    std::string units;
    int64_t ops = 0;
    double nsPerOp = 0.0;
    double nsTotal = 0.0;
    double note = 0.0;   // kernel-specific secondary number (MB/s, rt factor...)
    std::string noteUnits;
};

int passes = 3;

template <typename Fn>
Result measure (const char* name, const char* units, int64_t ops, int64_t iterations, Fn&& fn)
{
    Result r;
    r.name = name;
    r.units = units;
    r.ops = ops;

    fn();  // warm-up: first touch of the data, cache state, branch prediction

    double best = 0.0;

    for (int pass = 0; pass < std::max (1, passes); ++pass)
    {
        const auto start = Clock::now();

        for (int64_t i = 0; i < iterations; ++i)
            fn();

        const auto elapsed = std::chrono::duration<double, std::nano> (Clock::now() - start).count();
        best = pass == 0 ? elapsed : std::min (best, elapsed);
    }

    r.nsTotal = best;
    r.nsPerOp = best / static_cast<double> (std::max<int64_t> (1, ops * std::max<int64_t> (1, iterations)));
    return r;
}

std::string escape (const std::string& value)
{
    std::string out;
    out.reserve (value.size());

    for (const char c : value)
    {
        if (c == '"' || c == '\\')
            out.push_back ('\\');

        out.push_back (c);
    }

    return out;
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

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--json" && i + 1 < argc)
            jsonPath = argv[++i];
        else if (arg == "--quick")
            passes = 1;
        else
        {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }

    std::vector<Result> results;

    // --- calibration: the cost of the harness, not of any kernel ----------------
    // Same loop shape as the kernels below, so every other row can be read
    // against it. If a kernel lands on this number, the loop is being timed. It
    // also makes a harness change obvious: if this row moves, the harness moved.
    {
        volatile int calibrationInput = 1;
        results.push_back (measure ("harness.baseline_loop", "op", 4096, 250, [&calibrationInput] {
            double acc = 0.0;

            for (int i = 0; i < 4096; ++i)
                acc += static_cast<double> (i * calibrationInput);

            sink += acc;
        }));
    }

    // --- tempo map: the per-block position maths every clip and plugin sees ---
    {
        auto map = nc::engine::constantTempoMap (120.0);
        results.push_back (measure ("tempo_map.ppq_at_samples", "query", 4096, 250, [&map] {
            // Accumulate locally: a volatile store per operation serialises the
            // loop and can cost more than the kernel it is meant to measure.
            double acc = 0.0;

            for (int i = 0; i < 4096; ++i)
                acc += nc::engine::ppqAtSamplesWithMap (map, i * 512, 48000.0);

            sink += acc;
        }));
    }

    {
        auto map = nc::engine::constantTempoMap (120.0);
        results.push_back (measure ("tempo_map.seconds_to_ppq", "query", 4096, 250, [&map] {
            double acc = 0.0;

            for (int i = 0; i < 4096; ++i)
                acc += nc::engine::secondsToPpq (map, i * 0.01);

            sink += acc;
        }));
    }

    // --- clip timeline: the per-sample clip test inside the render pass ---
    {
        results.push_back (measure ("clip_timeline.clip_active_at", "query", 8192, 250, [] {
            double acc = 0.0;

            for (int i = 0; i < 8192; ++i)
            {
                const auto t = static_cast<double> (i) * 0.01;
                acc += nc::engine::clipActiveAt (t, 1.5, 4.0) ? 1.0 : 0.0;
            }

            sink += acc;
        }));
    }

    // --- SPSC command rail: message thread to audio callback ---
    {
        nc::engine::SpscQueue<int> queue (2048);
        results.push_back (measure ("spsc_queue.push_pop", "item", 1000000, 1, [&queue] {
            double acc = 0.0;

            for (int i = 0; i < 1000000; ++i)
            {
                queue.tryPush (i);
                int out = 0;
                if (queue.tryPop (out))
                    acc += out;
            }

            sink += acc;
        }));
    }

    // --- streaming window planning: what the reader thread asks the disk for ---
    {
        results.push_back (measure ("stream_window.plan_and_plan_read", "query", 1000000, 1, [] {
            double acc = 0.0;

            for (int64_t i = 0; i < 1000000; ++i)
            {
                const auto target = nc::engine::planWindow (i * 64, 48000 * 600, 48000 * 4);
                const auto resident = nc::engine::planWindow (i * 64 - 48000, 48000 * 600, 48000 * 4);
                const auto read = nc::engine::planRead (resident, target);
                acc += static_cast<double> (read.startFrame);
            }

            sink += acc;
        }));
    }

    // --- resampler: kernel build (message thread) and per-sample weights (audio) ---
    {
        nc::engine::resample::Bank bank;
        results.push_back (measure ("resample.bank_build", "build", 1, 200, [&bank] {
            bank.build (44100.0, 48000.0);
        }));

        bank.build (44100.0, 48000.0);
        results.push_back (measure ("resample.weights_lookup", "lookup", 1000000, 1, [&bank] {
            double acc = 0.0;

            for (int i = 0; i < 1000000; ++i)
            {
                const auto* weights = bank.weights (static_cast<double> (i % 1000) / 1000.0);
                acc += weights != nullptr ? weights[i % nc::engine::resample::taps] : 0.0;
            }

            sink += acc;
        }));
    }

    // --- loudness measurement: the export's two-pass analysis ---
    {
        const std::size_t frames = 48000 * 10;
        std::vector<float> left (frames, 0.0f), right (frames, 0.0f);

        for (std::size_t i = 0; i < frames; ++i)
            left[i] = right[i] = static_cast<float> (0.1 * std::sin (static_cast<double> (i) * 0.01));

        results.push_back (measure ("export.measure_loudness_10s", "buffer", 1, 5, [&] {
            const auto loudness = nc::engine::export_::measureLoudness (left.data(), right.data(), frames, 48000.0);
            sink += loudness.integratedLufs;
        }));
    }

    // --- dither: the export's per-sample noise on bit-depth reduction ---
    {
        const std::size_t frames = 48000 * 10;
        std::vector<float> channel (frames, 0.25f);
        nc::engine::export_::DitherRng rng;

        results.push_back (measure ("export.apply_tpdf_dither_10s", "sample", static_cast<int64_t> (frames), 5,
                                    [&] {
                                        nc::engine::export_::applyTpdfDither (channel.data(), channel.size(), 16, rng);
                                        sink += channel[0];
                                    }));
    }

    // --- MIDI scheduling: one block's worth of clip events ---
    {
        std::vector<nc::engine::MidiNote> notes;

        for (int i = 0; i < 2000; ++i)
            notes.push_back ({ static_cast<double> (i) * 0.25, 0.25, 48 + (i % 24), 100, 0 });

        auto map = nc::engine::constantTempoMap (120.0);
        std::vector<nc::engine::MidiEvent> out;
        out.reserve (4096);

        results.push_back (measure ("midi_schedule.clip_block", "event", 2000, 100, [&] {
            out.clear();
            nc::engine::scheduleMidiClip (notes, 1.0, 4.0, 0.0, 16.0, map, 48000.0, 512, out);
            sink += static_cast<double> (out.size());
        }));
    }

    std::ostringstream json;
    json << std::fixed << std::setprecision (3);
    json << "{\n";
    json << "  \"schema\": \"nc-bench/1\",\n";
    json << "  \"run\": {\n";
    json << "    \"utc\": \"" << utcNow() << "\",\n";
    json << "    \"hardware_threads\": " << std::thread::hardware_concurrency() << ",\n";
    // Bump when the measurement changes shape, so a baseline captured with an
    // older harness is not compared against a newer one by accident.
    json << "    \"harness\": 2,\n";
    json << "    \"passes\": " << passes << "\n";
    json << "  },\n";
    json << "  \"kernels\": [\n";

    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const auto& r = results[i];
        json << "    { \"name\": \"" << escape (r.name) << "\", \"units\": \"" << r.units
             << "\", \"ops\": " << r.ops
             << ", \"ns_per_op\": " << r.nsPerOp
             << ", \"ns_total\": " << r.nsTotal << " }";

        json << (i + 1 == results.size() ? "\n" : ",\n");
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
