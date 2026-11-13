#include "AudioEngine.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
bool applied (const juce::var& result)
{
    return (bool) result.getProperty ("ok", false);
}

juce::String errorOf (const juce::var& result)
{
    return result.getProperty ("error", {}).toString();
}
} // namespace

TEST_CASE ("applyAudioSettings refuses an unknown driver", "[audio-settings]")
{
    nc::AudioEngine engine;
    const auto before = engine.audioDeviceManager().getCurrentAudioDeviceType();

    const auto result = engine.applyAudioSettings ("NC No Such Driver", {}, 0.0, 0);

    REQUIRE_FALSE (applied (result));
    REQUIRE (errorOf (result).contains ("Unknown audio driver"));
    REQUIRE (errorOf (result).contains ("NC No Such Driver"));
    REQUIRE (engine.audioDeviceManager().getCurrentAudioDeviceType() == before);

    const auto state = result.getProperty ("state", {});
    REQUIRE (state.getDynamicObject() != nullptr);
    REQUIRE (state.getProperty ("deviceType", {}).toString() == before);
}

TEST_CASE ("applyAudioSettings refuses an output the current driver does not list", "[audio-settings]")
{
    nc::AudioEngine engine;
    auto& manager = engine.audioDeviceManager();
    const auto driver = manager.getCurrentAudioDeviceType();

    if (driver.isEmpty() || manager.getCurrentDeviceTypeObject() == nullptr)
    {
        SUCCEED ("No audio driver is present on this machine.");
        return;
    }

    const auto before = manager.getAudioDeviceSetup();
    const auto result = engine.applyAudioSettings (driver, "NC Missing Output 0f3c", 0.0, 0);

    REQUIRE_FALSE (applied (result));
    REQUIRE (errorOf (result).contains ("No output device named"));
    REQUIRE (errorOf (result).contains ("NC Missing Output 0f3c"));
    REQUIRE (manager.getAudioDeviceSetup().outputDeviceName == before.outputDeviceName);
}
