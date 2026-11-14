#include <catch2/catch_test_macros.hpp>

#include <nc/engine/SlicePlanner.hpp>
#include <nc/engine/VoiceToMidi.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef NC_REAL_CORPUS_DIR
#error "NC_REAL_CORPUS_DIR must point at tests/corpus/real"
#endif

namespace
{
constexpr int corpusSampleRate = 48000;

std::uint32_t readLE32 (const std::vector<unsigned char>& bytes, std::size_t at)
{
    if (at + 4 > bytes.size())
        return 0;

    return static_cast<std::uint32_t> (bytes[at])
           | (static_cast<std::uint32_t> (bytes[at + 1]) << 8)
           | (static_cast<std::uint32_t> (bytes[at + 2]) << 16)
           | (static_cast<std::uint32_t> (bytes[at + 3]) << 24);
}

std::uint16_t readLE16 (const std::vector<unsigned char>& bytes, std::size_t at)
{
    if (at + 2 > bytes.size())
        return 0;

    return static_cast<std::uint16_t> (bytes[at])
           | static_cast<std::uint16_t> (static_cast<std::uint16_t> (bytes[at + 1]) << 8);
}

// Minimal PCM-16 WAV reader. The committed corpus is deliberately a simple
// format; using it keeps this test independent of JUCE and platform codecs.
std::vector<float> readPcm16Wav (const std::filesystem::path& path)
{
    std::ifstream stream (path, std::ios::binary);

    if (! stream)
        return {};

    const std::vector<unsigned char> bytes ((std::istreambuf_iterator<char> (stream)),
                                            std::istreambuf_iterator<char>());

    if (bytes.size() < 44
        || std::memcmp (bytes.data(), "RIFF", 4) != 0
        || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
        return {};

    std::size_t at = 12;
    std::uint16_t channels = 0;
    std::uint32_t dataOffset = 0;
    std::uint32_t dataSize = 0;

    while (at + 8 <= bytes.size())
    {
        const auto size = readLE32 (bytes, at + 4);
        const auto payload = at + 8;

        if (std::memcmp (bytes.data() + at, "fmt ", 4) == 0)
        {
            if (payload + 16 > bytes.size() || readLE16 (bytes, payload) != 1)
                return {};

            channels = readLE16 (bytes, payload + 2);

            if (readLE32 (bytes, payload + 4) != corpusSampleRate || readLE16 (bytes, payload + 14) != 16)
                return {};
        }
        else if (std::memcmp (bytes.data() + at, "data", 4) == 0)
        {
            dataOffset = static_cast<std::uint32_t> (payload);
            dataSize = size;
            break;
        }

        at = payload + size;
    }

    if (channels != 1 || dataOffset == 0 || dataSize == 0 || dataOffset + dataSize > bytes.size())
        return {};

    std::vector<float> samples (dataSize / 2);

    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const auto raw = readLE16 (bytes, dataOffset + i * 2);
        samples[i] = static_cast<float> (static_cast<std::int16_t> (raw)) / 32768.0f;
    }

    return samples;
}

struct RealCase
{
    const char* file;
    int midi;
};

int countMidi (const std::vector<nc::engine::V2MNote>& notes, int midi)
{
    return static_cast<int> (std::count_if (notes.begin(), notes.end(),
                                            [midi] (const auto& note) { return note.midiNote == midi; }));
}
} // namespace

TEST_CASE ("real recorded piano corpus detects attacks and pitch", "[quality][real]")
{
    const RealCase cases[] { { "piano_a4.wav", 69 }, { "piano_c5.wav", 72 },
                             { "speech_deana.wav", -1 } };
    const auto root = std::filesystem::path (NC_REAL_CORPUS_DIR);

    for (const auto& fixture : cases)
    {
        INFO (fixture.file);
        const auto audio = readPcm16Wav (root / fixture.file);
        REQUIRE (! audio.empty());

        const float* channels[] = { audio.data() };
        const auto onsets = nc::detectTransients (channels, 1, static_cast<std::int64_t> (audio.size()), 0.45);
        REQUIRE (! onsets.empty());
        REQUIRE (onsets.front() <= corpusSampleRate * 80 / 1000);

        if (fixture.midi >= 0)
        {
            nc::engine::V2MConfig cfg;
            cfg.sampleRate = corpusSampleRate;
            cfg.bpm = 120.0;
            const auto notes = nc::engine::voiceToMidi (audio.data(), audio.size(), cfg);
            REQUIRE (countMidi (notes, fixture.midi) >= 1);
        }
    }
}
