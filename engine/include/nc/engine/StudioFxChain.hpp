#pragma once

#include <nc/engine/ProjectJson.hpp>
#include <nc/engine/StudioFxDynamics.hpp>
#include <nc/engine/StudioFxSpace.hpp>
#include <nc/engine/StudioFxVoice.hpp>

#include <memory>
#include <string>
#include <vector>

namespace nc::engine::studiofx
{
/** Decode an `nc.studiofx.v1` chain document. Unknown schemas fail, unknown
    modules and params are dropped, values are sanitised against the catalog.
    Mirrors `decodeChain`. */
inline std::vector<FxSlot> decodeChain (const json::Value& doc)
{
    std::vector<FxSlot> slots;
    if (doc.type != json::Value::Type::object)
        return slots;

    const json::Value* schema = doc.find ("schema");
    const json::Value* chain = doc.find ("chain");
    if (schema == nullptr || schema->type != json::Value::Type::string || schema->string != "nc.studiofx.v1")
        return {};
    if (chain == nullptr || chain->type != json::Value::Type::array)
        return {};

    std::size_t index = 0;
    for (const auto& raw : chain->array)
    {
        if (raw.type != json::Value::Type::object)
        {
            ++index;
            continue;
        }
        const json::Value* kindValue = raw.find ("kind");
        if (kindValue == nullptr || kindValue->type != json::Value::Type::string)
        {
            ++index;
            continue;
        }
        const FxKindSpec* kind = findKind (kindValue->string);
        if (kind == nullptr)
        {
            ++index;
            continue;
        }
        FxSlot slot;
        const json::Value* id = raw.find ("id");
        slot.id = (id != nullptr && id->type == json::Value::Type::string && ! id->string.empty())
                      ? id->string
                      : "slot-" + std::to_string (index);
        slot.kind = kindValue->string;
        const json::Value* enabled = raw.find ("enabled");
        slot.enabled = enabled == nullptr || enabled->type != json::Value::Type::boolean || enabled->boolean;
        const json::Value* params = raw.find ("params");
        std::unordered_map<std::string, float> rawParams;
        if (params != nullptr && params->type == json::Value::Type::object)
        {
            for (const auto& member : params->object)
                if (member.second.type == json::Value::Type::number)
                    rawParams[member.first] = static_cast<float> (member.second.number);
        }
        slot.params = sanitizeParams (*kind, rawParams);
        slots.push_back (std::move (slot));
        ++index;
    }
    return slots;
}

inline std::vector<FxSlot> decodeChain (const std::string& text)
{
    const auto doc = json::Parser::parse (text);
    if (! doc.has_value())
        return {};
    return decodeChain (*doc);
}

/** One module per catalog kind. Returns null for unimplemented kinds, so a
    selected slot can never silently do nothing. Mirrors `createModule`. */
inline std::unique_ptr<StudioFxModule> createModule (const std::string& kindName, float sampleRate)
{
    if (kindName == "TRIM") return std::make_unique<TrimModule> (sampleRate);
    if (kindName == "FILTER") return std::make_unique<FilterModule> (sampleRate);
    if (kindName == "LADDER_FILTER") return std::make_unique<LadderFilterModule> (sampleRate);
    if (kindName == "NEVE_1073") return std::make_unique<Neve1073Module> (sampleRate);
    if (kindName == "SSL_CHANNEL_EQ") return std::make_unique<SslChannelEqModule> (sampleRate);
    if (kindName == "PULTEC_EQP1A") return std::make_unique<PultecEqp1aModule> (sampleRate);
    if (kindName == "AIR_SHELF") return std::make_unique<AirShelfModule> (sampleRate);
    if (kindName == "MID_SIDE_EQ") return std::make_unique<MidSideEqModule> (sampleRate);
    if (kindName == "FET_1176") return std::make_unique<Fet1176Module> (sampleRate);
    if (kindName == "OPTO_LA2A") return std::make_unique<OptoLa2aModule> (sampleRate);
    if (kindName == "VCA_BUS_COMP") return std::make_unique<VcaBusCompModule> (sampleRate);
    if (kindName == "VARI_MU_COMP") return std::make_unique<VariMuCompModule> (sampleRate);
    if (kindName == "PARALLEL_COMP") return std::make_unique<ParallelCompModule> (sampleRate);
    if (kindName == "DE_ESSER") return std::make_unique<DeEsserModule> (sampleRate);
    if (kindName == "SIDECHAIN_DUCK") return std::make_unique<SidechainDuckModule> (sampleRate);
    if (kindName == "SATURATION") return std::make_unique<SaturationModule> (sampleRate);
    if (kindName == "TAPE_MACHINE") return std::make_unique<TapeMachineModule> (sampleRate);
    if (kindName == "PLATE_REVERB") return std::make_unique<AlgorithmicReverbModule> (sampleRate, "PLATE_REVERB", 0);
    if (kindName == "HALL_REVERB") return std::make_unique<AlgorithmicReverbModule> (sampleRate, "HALL_REVERB", 1);
    if (kindName == "ROOM_REVERB") return std::make_unique<AlgorithmicReverbModule> (sampleRate, "ROOM_REVERB", 2);
    if (kindName == "TEMPO_DELAY") return std::make_unique<TempoDelayModule> (sampleRate);
    if (kindName == "CHORUS_WIDTH") return std::make_unique<ChorusWidthModule> (sampleRate);
    if (kindName == "TRUE_PEAK_LIMITER") return std::make_unique<TruePeakLimiterModule> (sampleRate);
    if (kindName == "CHANNEL_LIMITER") return std::make_unique<ChannelLimiterModule> (sampleRate);
    if (kindName == "PHONE_NOISE_REDUCTION") return std::make_unique<PhoneNoiseReductionModule> (sampleRate);
    if (kindName == "PHONE_MIC_EMULATION") return std::make_unique<PhoneMicEmulationModule> (sampleRate);
    return nullptr;
}

/** Ordered insert chain. Rebuilds only when the shape changes; a parameter
    move reconfigures the live modules so state survives. Mirrors
    `StudioFxChainProcessor` / `TrackPluginRack`. */
class StudioFxChain
{
public:
    StudioFxChain (float sampleRate, float tempoBpm = 120.0f) noexcept
        : fs (sampleRate)
        , tempo (tempoBpm)
    {
    }

    bool empty() const noexcept { return modules.empty(); }

    void setTempoBpm (float bpm) noexcept
    {
        tempo = std::min (std::max (bpm, 20.0f), 300.0f);
        for (auto& module : modules)
            module->setTempoBpm (tempo);
    }

    void setChain (const std::vector<FxSlot>& slots)
    {
        bool sameShape = slots.size() == modules.size();
        for (std::size_t i = 0; sameShape && i < slots.size(); ++i)
            sameShape = slots[i].kind == modules[i]->kind() && slots[i].id == ids[i];
        if (sameShape)
        {
            for (std::size_t i = 0; i < slots.size(); ++i)
                modules[i]->configure (slots[i]);
        }
        else
        {
            build (slots);
        }
        chain = slots;
    }

    void clear() noexcept
    {
        modules.clear();
        ids.clear();
        chain.clear();
    }

    void processInto (float inL, float inR, float* out) noexcept
    {
        if (modules.empty())
        {
            out[0] = inL;
            out[1] = inR;
            return;
        }
        float l = inL, r = inR, scratch[2];
        for (auto& module : modules)
        {
            module->processSampleInto (l, r, scratch);
            l = scratch[0];
            r = scratch[1];
        }
        out[0] = l;
        out[1] = r;
    }

    void setKey (float l, float r) noexcept
    {
        for (auto& module : modules)
            module->setKey (l, r);
    }

    void reset() noexcept
    {
        for (auto& module : modules)
            module->reset();
    }

private:
    void build (const std::vector<FxSlot>& slots)
    {
        modules.clear();
        ids.clear();
        for (const auto& slot : slots)
        {
            auto module = createModule (slot.kind, fs);
            if (module == nullptr)
                continue;
            module->configure (slot);
            module->setTempoBpm (tempo);
            modules.push_back (std::move (module));
            ids.push_back (slot.id);
        }
    }

    float fs, tempo;
    std::vector<std::unique_ptr<StudioFxModule>> modules;
    std::vector<std::string> ids;
    std::vector<FxSlot> chain;
};

/** Offline mono render used by export and by the parity test. */
inline std::vector<float> renderChainMono (
    const std::vector<float>& input, float sampleRate, const std::vector<FxSlot>& slots, float tempoBpm = 120.0f)
{
    std::vector<float> output (input.size(), 0.0f);
    if (input.empty())
        return output;
    StudioFxChain rack (sampleRate, tempoBpm);
    rack.setChain (slots);
    float scratch[2];
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        rack.processInto (input[i], input[i], scratch);
        output[i] = (scratch[0] + scratch[1]) * 0.5f;
    }
    return output;
}

} // namespace nc::engine::studiofx
