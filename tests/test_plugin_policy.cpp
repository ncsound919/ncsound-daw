#include <catch2/catch_test_macros.hpp>

#include <nc/engine/PluginPolicy.hpp>

#include <string>

using namespace nc::engine;

// RED: the browser claimed "loaded" for every registry entry, including
// archived entries and un-ported VST3s this build cannot instantiate.
TEST_CASE ("only native plugins can be instantiated in this build")
{
    REQUIRE (canInstantiate (PluginTier::native));
    REQUIRE_FALSE (canInstantiate (PluginTier::external));
    REQUIRE_FALSE (canInstantiate (PluginTier::port));
    REQUIRE_FALSE (canInstantiate (PluginTier::archived));
}

TEST_CASE ("refused plugins explain themselves instead of claiming success")
{
    REQUIRE (instantiateRefusal (PluginTier::native) == nullptr);

    const auto port = std::string (instantiateRefusal (PluginTier::port));
    const auto external = std::string (instantiateRefusal (PluginTier::external));
    const auto archived = std::string (instantiateRefusal (PluginTier::archived));

    REQUIRE (port.find ("PORT") != std::string::npos);
    REQUIRE (external.find ("INDEXED") != std::string::npos);
    REQUIRE (archived.find ("ARCHIVED") != std::string::npos);
}

TEST_CASE ("registry status names map onto instantiation tiers")
{
    REQUIRE (tierFromName ("native") == PluginTier::native);
    REQUIRE (tierFromName ("external") == PluginTier::external);
    REQUIRE (tierFromName ("port") == PluginTier::port);
    REQUIRE (tierFromName ("archived") == PluginTier::archived);
    REQUIRE (tierFromName ("nonsense") == PluginTier::external);
}
