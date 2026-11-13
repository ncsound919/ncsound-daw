#include "BuiltinStudioFx.h"

#include <nc/engine/ProjectJson.hpp>

namespace nc
{
void BuiltinStudioFx::prepare (double sampleRate, int blockSize)
{
    (void) blockSize;
    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    // Module coefficients bake the rate at construction, so rebuild the
    // chain whenever the device rate changes. Message thread only.
    fx = nc::engine::studiofx::StudioFxChain (static_cast<float> (preparedSampleRate));
    fx.setChain (slots);
}

void BuiltinStudioFx::process (juce::AudioBuffer<float>& buffer,
                               const nc::engine::TransportBlock& block)
{
    if (bypassed || buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return;

    fx.setTempoBpm (static_cast<float> (block.tempo));

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();
    float out[2];

    int channel = 0;
    for (; channel + 1 < numChannels; channel += 2)
    {
        auto* left = buffer.getWritePointer (channel);
        auto* right = buffer.getWritePointer (channel + 1);

        for (int i = 0; i < numSamples; ++i)
        {
            fx.processInto (left[i], right[i], out);
            left[i] = out[0];
            right[i] = out[1];
        }
    }

    // An odd trailing channel (mono sessions) runs dual-mono.
    if (channel < numChannels)
    {
        auto* mono = buffer.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
        {
            fx.processInto (mono[i], mono[i], out);
            mono[i] = out[0];
        }
    }
}

bool BuiltinStudioFx::getState (nc::engine::PluginSlotState& state) const
{
    state.identifier = builtinStudioFxIdentifier;
    state.format = "builtin";
    state.descriptionXml.clear();
    state.bypassed = bypassed;
    state.stateBase64 = storedJson;
    return true;
}

bool BuiltinStudioFx::setState (const nc::engine::PluginSlotState& state)
{
    bypassed = state.bypassed;
    // A missing blob is the empty (passthrough) chain, not a failure.
    if (state.stateBase64.empty())
    {
        slots.clear();
        storedJson.clear();
        fx.clear();
        return true;
    }
    return setChainJson (state.stateBase64);
}

bool BuiltinStudioFx::setChainJson (const std::string& chainJson)
{
    const auto doc = nc::engine::json::Parser::parse (chainJson);
    const auto* schema = doc.has_value() ? doc->find ("schema") : nullptr;
    const bool valid = schema != nullptr
                       && schema->type == nc::engine::json::Value::Type::string
                       && schema->string == "nc.studiofx.v1";
    if (! valid)
        return false;

    slots = nc::engine::studiofx::decodeChain (*doc);
    storedJson = chainJson;
    fx.setChain (slots);
    return true;
}
} // namespace nc
