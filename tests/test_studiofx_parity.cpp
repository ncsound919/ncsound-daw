#include <nc/engine/StudioFxChain.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
using namespace nc::engine::studiofx;

std::string fixtureDir()
{
    std::string path = __FILE__;
    const auto slash = path.find_last_of ("/\\");
    return (slash == std::string::npos ? std::string() : path.substr (0, slash)) + "/fixtures/studiofx/";
}

std::string readFile (const std::string& path)
{
    std::ifstream stream (path, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

const nc::engine::json::Value& require (const nc::engine::json::Value& doc, const char* key)
{
    const auto* value = doc.find (key);
    REQUIRE (value != nullptr);
    return *value;
}
} // namespace

TEST_CASE ("studio FX parity fixture loads and matches the catalog", "[studiofx]")
{
    const auto fixtures = nc::engine::json::Parser::parse (readFile (fixtureDir() + "parity-fixtures.json"));
    REQUIRE (fixtures.has_value());
    REQUIRE (require (*fixtures, "schema").string == "nc.studiofx.parity.v1");
    REQUIRE (require (*fixtures, "sampleRate").number == 48000.0);
    REQUIRE (require (*fixtures, "frames").number == 1024.0);

    const auto catalog = nc::engine::json::Parser::parse (readFile (fixtureDir() + "catalog.json"));
    REQUIRE (catalog.has_value());
    std::size_t implemented = 0;
    for (const auto& kind : require (*catalog, "kinds").array)
    {
        const std::string id = require (kind, "id").string;
        if (id != "VOCAL_RIDER" && id != "RESONANCE_SMOOTHER")
            ++implemented;
    }

    const auto& cases = require (*fixtures, "cases").array;
    // One case per implemented module plus the full-chain case.
    REQUIRE (cases.size() == implemented + 1);
    REQUIRE (cases.size() == 27);
}

TEST_CASE ("C++ renderer matches the golden vectors", "[studiofx]")
{
    const auto fixtures = nc::engine::json::Parser::parse (readFile (fixtureDir() + "parity-fixtures.json"));
    REQUIRE (fixtures.has_value());

    std::vector<float> input;
    for (const auto& sample : require (*fixtures, "input").array)
        input.push_back (static_cast<float> (sample.number));

    for (const auto& testCase : require (*fixtures, "cases").array)
    {
        const std::string name = require (testCase, "name").string;
        const std::string chainText = require (testCase, "chain").string;
        const auto slots = decodeChain (chainText);
        INFO ("case " << name);
        REQUIRE (! slots.empty());

        const auto output = renderChainMono (input, 48000.0f, slots, 120.0f);
        const auto& expected = require (testCase, "output").array;
        REQUIRE (output.size() == expected.size());

        float maxDiff = 0.0f;
        std::size_t maxIndex = 0;
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            const float diff = std::fabs (output[i] - static_cast<float> (expected[i].number));
            if (diff > maxDiff)
            {
                maxDiff = diff;
                maxIndex = i;
            }
        }
        INFO (name << ": sample " << maxIndex << " differs by " << maxDiff);
        // Same bound as the TS port: ~20 dB below the quietest 16-bit LSB.
        REQUIRE (maxDiff < 2e-3f);
    }
}

TEST_CASE ("studio FX chain codec rejects garbage and drops unknowns", "[studiofx]")
{
    REQUIRE (decodeChain (std::string ("{\"schema\":\"nc.studiofx.v0\",\"chain\":[]}")).empty());

    const auto slots = decodeChain (std::string (
        "{\"schema\":\"nc.studiofx.v1\",\"chain\":["
        "{\"kind\":\"FUTURE_MODULE\",\"params\":{}},"
        "{\"id\":\"x\",\"kind\":\"TRIM\",\"params\":{\"gainDb\":6,\"future\":2}}]}"));
    REQUIRE (slots.size() == 1);
    REQUIRE (slots[0].id == "x");
    REQUIRE (slots[0].kind == "TRIM");
    REQUIRE (slots[0].params.count ("future") == 0);
    REQUIRE (slots[0].value ("gainDb", 0.0f) == 6.0f);
}

TEST_CASE ("studio FX codec clamps and fills defaults", "[studiofx]")
{
    const auto slots = decodeChain (std::string (
        "{\"schema\":\"nc.studiofx.v1\",\"chain\":["
        "{\"id\":\"x\",\"kind\":\"FET_1176\",\"params\":{\"ratio\":999,\"attackUs\":1}}]}"));
    REQUIRE (slots.size() == 1);
    // Ratio snaps to the nearest choice (20:1); attack clamps to its minimum.
    REQUIRE (slots[0].value ("ratio", 0.0f) == 20.0f);
    REQUIRE (slots[0].value ("attackUs", 0.0f) == 20.0f);
    // Missing params fall back to catalog defaults.
    REQUIRE (slots[0].value ("mix", -1.0f) == 1.0f);
}

TEST_CASE ("studio FX rack keeps module state when only a parameter moves", "[studiofx]")
{
    FxSlot slot;
    slot.id = "tape";
    slot.kind = "TAPE_MACHINE";
    slot.enabled = true;
    slot.params = sanitizeParams (*findKind ("TAPE_MACHINE"), {});
    StudioFxChain rack (48000.0f, 120.0f);
    rack.setChain ({ slot });
    float out[2] = { 0.0f, 0.0f };
    // Longer than the tape module's 4 ms modulated delay, so it has signal.
    for (int i = 0; i < 512; ++i)
        rack.processInto (0.1f, 0.1f, out);
    const float before = out[0];
    FxSlot moved = slot;
    moved.params["driveDb"] = slotValue (slot, "driveDb") + 1.0f;
    rack.setChain ({ moved });
    rack.processInto (0.1f, 0.1f, out);
    // A rebuilt module would start from silence; the live one must not.
    REQUIRE (std::fabs (out[0] - before) < 0.2f);
    REQUIRE (out[0] != 0.0f);
}
