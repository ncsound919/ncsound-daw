#include <catch2/catch_test_macros.hpp>

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

        rack.collectGarbage();
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
    rack.collectGarbage();

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
    REQUIRE_FALSE (rack.moveInsert (0, 0, 4));    // past the last slot
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
    rack.collectGarbage();
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
