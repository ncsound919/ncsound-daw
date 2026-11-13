#pragma once

#include "HostedInsert.h"

#include <nc/engine/StudioFxChain.hpp>

#include <string>
#include <vector>

namespace nc
{
inline constexpr const char* builtinStudioFxIdentifier = "builtin-studiofx";

/** The ported shared studio-FX chain as a track insert, so sessions get EQ,
    dynamics, saturation and space with no VST3 installed. The chain JSON
    (the `nc.studiofx.v1` codec) is the state: setState decodes it, getState
    returns the stored text verbatim, and an empty chain is a passthrough. */
class BuiltinStudioFx final : public InsertProcessor
{
public:
    BuiltinStudioFx() = default;
    explicit BuiltinStudioFx (std::string chainJson) { setChainJson (chainJson); }

    void prepare (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, const nc::engine::TransportBlock&) override;
    int latencySamples() const override { return 0; }
    void setBypassed (bool shouldBeBypassed) override { bypassed = shouldBeBypassed; }
    bool isBypassed() const override { return bypassed; }
    juce::String name() const override { return "Studio FX (Built-in)"; }
    bool getState (nc::engine::PluginSlotState&) const override;
    bool setState (const nc::engine::PluginSlotState&) override;

    /** Decodes and installs a chain; false leaves the previous chain alone. */
    bool setChainJson (const std::string& chainJson);
    const std::string& chainJson() const noexcept { return storedJson; }

    const nc::engine::studiofx::StudioFxChain& chain() const noexcept { return fx; }

private:
    nc::engine::studiofx::StudioFxChain fx { 48000.0f };
    std::vector<nc::engine::studiofx::FxSlot> slots;
    std::string storedJson;
    double preparedSampleRate = 48000.0;
    bool bypassed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltinStudioFx)
};
} // namespace nc
