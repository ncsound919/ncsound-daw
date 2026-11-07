#pragma once

#include <cstring>

namespace nc::engine
{
/** What the build can actually do with an indexed plug-in.

    The registry lists more plug-ins than this binary can instantiate: ported
    entries have no binary yet, external VST3s are only referenced, and archived
    entries are deliberately retired. Reporting a load for any of those was a
    false success, so the tier decides the claim.
*/
enum class PluginTier
{
    native,
    external,
    port,
    archived
};

inline constexpr bool canInstantiate (PluginTier tier) noexcept
{
    return tier == PluginTier::native;
}

/** Why a tier cannot load, or nullptr when it can. */
inline constexpr const char* instantiateRefusal (PluginTier tier) noexcept
{
    switch (tier)
    {
        case PluginTier::native:   return nullptr;
        case PluginTier::port:     return "NO PORT BINARY IN THIS BUILD";
        case PluginTier::external: return "VST3 INDEXED ONLY / SCANNER NOT LINKED";
        case PluginTier::archived: return "ARCHIVED / RETIRED IN THIS BUILD";
    }

    return "UNKNOWN PLUG-IN TIER";
}

inline constexpr PluginTier tierFromName (const char* status) noexcept
{
    if (status == nullptr)
        return PluginTier::external;

    if (std::strcmp (status, "native") == 0)   return PluginTier::native;
    if (std::strcmp (status, "port") == 0)     return PluginTier::port;
    if (std::strcmp (status, "archived") == 0) return PluginTier::archived;

    return PluginTier::external;
}
} // namespace nc::engine
