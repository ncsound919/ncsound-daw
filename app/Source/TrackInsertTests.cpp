#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "BuiltinStudioFx.h"
#include "TrackInsertRack.h"

#include <atomic>
#include <cmath>
#include <memory>

namespace
{
class FakeInsert final : public nc::InsertProcessor
{
public:
    explicit FakeInsert (int latencyInSamples = 0, bool produceNan = false)
        : latency (latencyInSamples), nan (produceNan)
    {
    }

    ~FakeInsert() override { ++destroyed; }

    void prepare (double, int) override
    {
        left.assign (static_cast<size_t> (juce::jmax (1, latency)), 0.0f);
        right.assign (static_cast<size_t> (juce::jmax (1, latency)), 0.0f);
        write = 0;
    }

    void process (juce::AudioBuffer<float>& buffer, const nc::engine::TransportBlock&) override
    {
        if (bypassed)
            return;

        if (nan)
        {
            buffer.clear();
            buffer.setSample (0, 0, std::nanf (""));
            return;
        }

        if (latency <= 0 || buffer.getNumSamples() == 0)
            return;

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto outLeft = left[static_cast<size_t> (write)];
            const auto outRight = right[static_cast<size_t> (write)];
            const auto inLeft = buffer.getSample (0, sample);
            const auto inRight = buffer.getNumChannels() > 1 ? buffer.getSample (1, sample) : inLeft;

            left[static_cast<size_t> (write)] = inLeft;
            right[static_cast<size_t> (write)] = inRight;
            buffer.setSample (0, sample, outLeft);

            if (buffer.getNumChannels() > 1)
                buffer.setSample (1, sample, outRight);

            write = (write + 1) % latency;
        }
    }

    int latencySamples() const override { return latency; }
    void setBypassed (bool shouldBeBypassed) override { bypassed = shouldBeBypassed; }
    bool isBypassed() const override { return bypassed; }
    juce::String name() const override { return "Fake Insert"; }

    bool getState (nc::engine::PluginSlotState& state) const override
    {
        state.identifier = "fake";
        state.format = "fake";
        state.stateBase64 = "AA==";
        state.bypassed = bypassed;
        return true;
    }

    bool setState (const nc::engine::PluginSlotState& state) override
    {
        bypassed = state.bypassed;
        return true;
    }

    static std::atomic<int> destroyed;

private:
    int latency = 0;
    bool nan = false;
    bool bypassed = false;
    std::vector<float> left, right;
    int write = 0;
};

std::atomic<int> FakeInsert::destroyed { 0 };

nc::engine::PluginSlotState stateFor (const char* id)
{
    nc::engine::PluginSlotState state;
    state.identifier = id;
    state.format = "fake";
    return state;
}

void impulse (juce::AudioBuffer<float>& buffer)
{
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);

    if (buffer.getNumChannels() > 1)
        buffer.setSample (1, 0, 1.0f);
}
} // namespace

TEST_CASE ("a zero-latency track is delayed to match a latency insert")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 2);

    juce::String error;
    REQUIRE (rack.install (0, 0, std::make_unique<FakeInsert> (0), stateFor ("dry"), error));
    REQUIRE (rack.install (1, 0, std::make_unique<FakeInsert> (64), stateFor ("latent"), error));

    juce::AudioBuffer<float> dry (2, 256);
    juce::AudioBuffer<float> latent (2, 256);
    impulse (dry);
    impulse (latent);

    rack.beginBlock();
    rack.processTrack (0, dry, {});
    rack.processTrack (1, latent, {});
    rack.endBlock();

    REQUIRE (dry.getSample (0, 0) == 0.0f);
    REQUIRE (latent.getSample (0, 0) == 0.0f);
    REQUIRE (dry.getSample (0, 64) == 1.0f);
    REQUIRE (latent.getSample (0, 64) == 1.0f);
}

TEST_CASE ("bypass removes the insert latency and the chain delay")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);

    juce::String error;
    REQUIRE (rack.install (0, 0, std::make_unique<FakeInsert> (64), stateFor ("latent"), error));
    rack.setBypassed (0, 0, true);

    juce::AudioBuffer<float> buffer (2, 256);
    impulse (buffer);

    rack.beginBlock();
    rack.processTrack (0, buffer, {});
    rack.endBlock();

    REQUIRE (rack.chainLatency (0) == 0);
    REQUIRE (buffer.getSample (0, 0) == 1.0f);
}

TEST_CASE ("non-finite insert output is contained before the bus")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);

    juce::String error;
    REQUIRE (rack.install (0, 0, std::make_unique<FakeInsert> (0, true), stateFor ("bad"), error));

    juce::AudioBuffer<float> buffer (2, 256);
    buffer.clear();

    rack.beginBlock();
    rack.processTrack (0, buffer, {});
    rack.endBlock();

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            REQUIRE (std::isfinite (buffer.getSample (channel, sample)));
}

TEST_CASE ("replaced inserts are destroyed only after garbage collection")
{
    FakeInsert::destroyed.store (0);

    {
        nc::TrackInsertRack rack;
        rack.prepare (48000.0, 256, 1);

        juce::String error;
        REQUIRE (rack.install (0, 0, std::make_unique<FakeInsert> (0), stateFor ("first"), error));
        REQUIRE (FakeInsert::destroyed.load() == 0);

        REQUIRE (rack.install (0, 0, std::make_unique<FakeInsert> (0), stateFor ("second"), error));
        REQUIRE (FakeInsert::destroyed.load() == 0);

        rack.collectGarbage (true); // no callback can run in this test
        REQUIRE (FakeInsert::destroyed.load() == 1);
    }

    REQUIRE (FakeInsert::destroyed.load() == 2);
}

TEST_CASE ("remove clears the active slot and saved metadata")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);

    juce::String error;
    REQUIRE (rack.install (0, 0, std::make_unique<FakeInsert> (0), stateFor ("gone"), error));
    REQUIRE_FALSE (rack.slotState (0, 0).isEmpty());

    rack.remove (0, 0);
    rack.collectGarbage (true);

    REQUIRE (rack.slotState (0, 0).isEmpty());
    REQUIRE_FALSE (rack.isMissing (0, 0));
}


TEST_CASE ("moving an insert keeps the instance and its state", "[inserts]")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);

    juce::String error;
    REQUIRE (rack.install (0, 0, std::make_unique<FakeInsert> (64), stateFor ("moving"), error));

    FakeInsert::destroyed.store (0);
    REQUIRE (rack.moveInsert (0, 0, 2));

    // Moved, not rebuilt: the same instance now sits in slot 2.
    REQUIRE (rack.slot (0, 0).active.load() == nullptr);
    REQUIRE (rack.slot (0, 0).owned == nullptr);
    REQUIRE (rack.slot (0, 2).active.load() != nullptr);
    REQUIRE (rack.slot (0, 2).active.load()->latencySamples() == 64);
    REQUIRE (rack.slotState (0, 2).identifier == juce::String ("moving"));
    REQUIRE (FakeInsert::destroyed.load() == 0);
}

TEST_CASE ("moving onto an occupied slot swaps both inserts", "[inserts]")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);

    juce::String error;
    REQUIRE (rack.install (0, 0, std::make_unique<FakeInsert> (64), stateFor ("a"), error));
    REQUIRE (rack.install (0, 1, std::make_unique<FakeInsert> (128), stateFor ("b"), error));

    FakeInsert::destroyed.store (0);
    REQUIRE (rack.moveInsert (0, 0, 1));

    REQUIRE (rack.slot (0, 0).active.load()->latencySamples() == 128);
    REQUIRE (rack.slot (0, 1).active.load()->latencySamples() == 64);
    REQUIRE (rack.slotState (0, 0).identifier == juce::String ("b"));
    REQUIRE (rack.slotState (0, 1).identifier == juce::String ("a"));
    REQUIRE (FakeInsert::destroyed.load() == 0);
}

TEST_CASE ("a moved insert carries its bypass flag", "[inserts]")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);

    juce::String error;
    REQUIRE (rack.install (0, 0, std::make_unique<FakeInsert> (0), stateFor ("byp"), error));
    rack.setBypassed (0, 0, true);

    REQUIRE (rack.moveInsert (0, 0, 3));
    REQUIRE (rack.slot (0, 3).bypassed.load());
    REQUIRE_FALSE (rack.slot (0, 0).bypassed.load());
    REQUIRE (rack.chainLatency (0) == 0);   // a bypassed insert adds no latency
}

TEST_CASE ("a move outside the rack is refused and changes nothing", "[inserts]")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);

    juce::String error;
    REQUIRE (rack.install (0, 0, std::make_unique<FakeInsert> (32), stateFor ("stay"), error));

    REQUIRE_FALSE (rack.moveInsert (0, 0, 0));    // same slot
    REQUIRE_FALSE (rack.moveInsert (0, 0, nc::TrackInsertRack::slotsPerTrack));  // past the last slot
    REQUIRE_FALSE (rack.moveInsert (-1, 0, 1));   // no such track

    REQUIRE (rack.slot (0, 0).active.load() != nullptr);
    REQUIRE (rack.slotState (0, 0).identifier == juce::String ("stay"));
}

TEST_CASE ("master slot installs, bypasses, reports latency and clears", "[inserts]")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);

    juce::String error;
    REQUIRE (rack.installMaster (std::make_unique<FakeInsert> (32), stateFor ("master"), error));
    REQUIRE (rack.masterName() == "Fake Insert");
    REQUIRE (rack.masterLatency() == 32);
    REQUIRE_FALSE (rack.isMasterMissing());
    REQUIRE (rack.masterState().identifier == juce::String ("master"));

    rack.setMasterBypassed (true);
    REQUIRE (rack.masterLatency() == 0);

    rack.removeMaster();
    rack.collectGarbage (true);
    REQUIRE (rack.masterState().isEmpty());
    REQUIRE (rack.masterLatency() == 0);
}

TEST_CASE ("master missing keeps saved state for replace", "[inserts]")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);

    rack.markMasterMissing (stateFor ("gone-master"), "Gone Master");
    REQUIRE (rack.isMasterMissing());
    REQUIRE (rack.masterName() == "Gone Master");
    REQUIRE (rack.masterState().identifier == juce::String ("gone-master"));
    REQUIRE (rack.masterLatency() == 0);
}

TEST_CASE ("master NaN output is contained", "[inserts]")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);

    juce::String error;
    REQUIRE (rack.installMaster (std::make_unique<FakeInsert> (0, true), stateFor ("bad-master"), error));

    juce::AudioBuffer<float> buffer (2, 256);
    buffer.clear();
    rack.processMaster (buffer, {});

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            REQUIRE (std::isfinite (buffer.getSample (channel, sample)));
}

TEST_CASE ("builtin studio-fx insert processes audio through the ported chain", "[inserts]")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 2);

    // Heavy saturation: unmistakable if the rack really runs the chain.
    const std::string chainJson = R"({"schema":"nc.studiofx.v1","chain":[{"kind":"SATURATION","id":"sat","params":{"type":0,"driveDb":12,"warmth":0.5,"mix":1,"oversample":0},"enabled":true}]})";

    auto fx = std::make_unique<nc::BuiltinStudioFx>();
    REQUIRE (fx->setChainJson (chainJson));
    REQUIRE (fx->latencySamples() == 0);

    nc::engine::PluginSlotState saved;
    REQUIRE (fx->getState (saved));
    REQUIRE (saved.identifier == nc::builtinStudioFxIdentifier);

    juce::String error;
    REQUIRE (rack.install (0, 0, std::move (fx), saved, error));
    REQUIRE (rack.slotName (0, 0) == "Studio FX (Built-in)");
    REQUIRE (rack.chainLatency (0) == 0);

    juce::AudioBuffer<float> buffer (2, 256);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        buffer.setSample (0, i, 0.5f * static_cast<float> (std::sin (6.283185307179586 * 220.0 * i / 48000.0)));
        buffer.setSample (1, i, 0.4f * static_cast<float> (std::sin (6.283185307179586 * 220.0 * i / 48000.0)));
    }
    juce::AudioBuffer<float> dry (2, 256);
    dry.makeCopyOf (buffer);

    nc::engine::TransportBlock block;
    block.tempo = 120.0;
    block.numSamples = buffer.getNumSamples();
    rack.beginBlock();
    rack.processTrack (0, buffer, block);
    rack.endBlock();

    // Reference: the same input straight through the ported chain.
    nc::engine::studiofx::StudioFxChain reference (48000.0f);
    reference.setChain (nc::engine::studiofx::decodeChain (chainJson));
    bool audiblyDifferent = false;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float out[2];
        const float inL = dry.getSample (0, i), inR = dry.getSample (1, i);
        reference.processInto (inL, inR, out);
        REQUIRE (std::isfinite (buffer.getSample (0, i)));
        REQUIRE (std::isfinite (buffer.getSample (1, i)));
        REQUIRE (buffer.getSample (0, i) == Catch::Approx (out[0]).margin (1e-6f));
        REQUIRE (buffer.getSample (1, i) == Catch::Approx (out[1]).margin (1e-6f));
        if (std::fabs (buffer.getSample (0, i) - inL) > 0.01f)
            audiblyDifferent = true;
    }
    REQUIRE (audiblyDifferent);
}

TEST_CASE ("builtin studio-fx state round-trips the chain JSON", "[inserts]")
{
    const std::string chainJson = R"({"schema":"nc.studiofx.v1","chain":[{"kind":"TRIM","id":"t","params":{"gainDb":6,"autoStage":0},"enabled":true}]})";

    auto first = std::make_unique<nc::BuiltinStudioFx>();
    REQUIRE (first->setChainJson (chainJson));
    nc::engine::PluginSlotState saved;
    REQUIRE (first->getState (saved));
    REQUIRE (saved.stateBase64 == chainJson);

    // Garbage never replaces the installed chain.
    REQUIRE (! first->setChainJson ("not json"));
    REQUIRE (first->chainJson() == chainJson);
    // Empty text is rejected too: the empty chain comes from empty state.
    REQUIRE (! first->setChainJson (""));

    auto second = std::make_unique<nc::BuiltinStudioFx>();
    REQUIRE (second->setState (saved));
    REQUIRE (second->chainJson() == chainJson);

    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);
    juce::String error;
    REQUIRE (rack.install (0, 0, std::move (second), saved, error));

    juce::AudioBuffer<float> buffer (1, 64);
    for (int i = 0; i < 64; ++i)
        buffer.setSample (0, i, 0.25f);
    nc::engine::TransportBlock block;
    rack.beginBlock();
    rack.processTrack (0, buffer, block);
    rack.endBlock();

    // The restored chain must render exactly what the ported chain renders.
    nc::engine::studiofx::StudioFxChain reference (48000.0f);
    reference.setChain (nc::engine::studiofx::decodeChain (chainJson));
    for (int i = 0; i < 64; ++i)
    {
        float out[2];
        reference.processInto (0.25f, 0.25f, out);
        REQUIRE (buffer.getSample (0, i) == Catch::Approx (out[0]).margin (1e-6f));
    }
    // And +6 dB of trim is audibly hotter than the dry signal.
    REQUIRE (buffer.getSample (0, 63) > 0.4f);
}

TEST_CASE ("empty studio-fx chain is a passthrough", "[inserts]")
{
    nc::TrackInsertRack rack;
    rack.prepare (48000.0, 256, 1);

    auto fx = std::make_unique<nc::BuiltinStudioFx>();
    REQUIRE (fx->chainJson().empty());

    nc::engine::PluginSlotState saved;
    REQUIRE (fx->getState (saved));
    juce::String error;
    REQUIRE (rack.install (0, 0, std::move (fx), saved, error));

    juce::AudioBuffer<float> buffer (2, 64);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 64; ++i)
            buffer.setSample (channel, i, 0.1f * static_cast<float> (channel + 1 + i % 7));

    nc::engine::TransportBlock block;
    rack.beginBlock();
    rack.processTrack (0, buffer, block);
    rack.endBlock();

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 64; ++i)
            REQUIRE (buffer.getSample (channel, i)
                     == Catch::Approx (0.1f * static_cast<float> (channel + 1 + i % 7)));
}
