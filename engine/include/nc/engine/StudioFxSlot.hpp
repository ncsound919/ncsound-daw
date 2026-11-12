#pragma once

#include <nc/engine/StudioFxCatalogData.hpp>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace nc::engine::studiofx
{
/** One insert slot. Mirrors `FxSlot`: values not present fall back to the
    catalog default, unknown kinds never reach a module. */
struct FxSlot
{
    std::string id;
    std::string kind;
    bool enabled = true;
    std::unordered_map<std::string, float> params;

    float value (const char* key, float fallback) const noexcept
    {
        const auto it = params.find (key);
        if (it == params.end() || ! std::isfinite (it->second))
            return fallback;
        return it->second;
    }
};

inline const FxKindSpec* findKind (const std::string& id) noexcept
{
    for (std::size_t i = 0; i < kNumFxKinds; ++i)
        if (id == kFxKinds[i].id)
            return &kFxKinds[i];
    return nullptr;
}

inline const FxParamSpec* findParam (const FxKindSpec& kind, const std::string& key) noexcept
{
    for (std::size_t i = 0; i < kind.numParams; ++i)
        if (key == kind.params[i].key)
            return &kind.params[i];
    return nullptr;
}

/** Nearest choice wins; otherwise clamp into range. Mirrors `clampParam`. */
inline float clampParam (const FxParamSpec& spec, float value) noexcept
{
    if (! std::isfinite (value))
        return spec.defaultValue;
    if (spec.choices != nullptr && spec.numChoices > 0)
    {
        float best = spec.choices[0].value;
        float bestDistance = std::fabs (best - value);
        for (std::size_t i = 1; i < spec.numChoices; ++i)
        {
            const float distance = std::fabs (spec.choices[i].value - value);
            if (distance < bestDistance)
            {
                best = spec.choices[i].value;
                bestDistance = distance;
            }
        }
        return best;
    }
    return std::min (std::max (value, spec.minValue), spec.maxValue);
}

/** Reads one slot value with catalog sanitising. Mirrors `slotValue`. */
inline float slotValue (const FxSlot& slot, const char* key) noexcept
{
    const FxKindSpec* kind = findKind (slot.kind);
    if (kind == nullptr)
        return 0.0f;
    const FxParamSpec* spec = findParam (*kind, key);
    if (spec == nullptr)
        return 0.0f;
    const auto it = slot.params.find (key);
    if (it == slot.params.end() || ! std::isfinite (it->second))
        return spec->defaultValue;
    return clampParam (*spec, it->second);
}

/** Sanitise a raw param map against the catalog. Mirrors `sanitizeParams`. */
inline std::unordered_map<std::string, float> sanitizeParams (
    const FxKindSpec& kind, const std::unordered_map<std::string, float>& raw)
{
    std::unordered_map<std::string, float> out;
    for (std::size_t i = 0; i < kind.numParams; ++i)
    {
        const auto& spec = kind.params[i];
        const auto it = raw.find (spec.key);
        const float value = it == raw.end() ? spec.defaultValue : it->second;
        out[spec.key] = std::isfinite (value) ? clampParam (spec, value) : spec.defaultValue;
    }
    return out;
}

} // namespace nc::engine::studiofx
