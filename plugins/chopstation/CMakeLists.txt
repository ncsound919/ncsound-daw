juce_add_plugin(NCChopStation
    COMPANY_NAME "NCSound"
    BUNDLE_ID "com.ncsound.chopstation"
    PLUGIN_MANUFACTURER_CODE Ncsd
    PLUGIN_CODE Chps
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "Chop Station")

juce_add_binary_data(NCChopFonts SOURCES
    ../../assets/fonts/BankGothic.ttf)

target_sources(NCChopStation PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(NCChopStation PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(NCChopStation PRIVATE
    NCChopFonts
    nc_engine
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Headless state/param tests (Catch2, same harness as nc_tests).
# The editor is GUI-only; these cover processor-level guarantees: 10 params,
# capped load, embedded-sample round-trip, presets.
add_executable(NCChopStationTests
    Source/ChopStationTests.cpp
    Source/PluginProcessor.cpp)

target_include_directories(NCChopStationTests PRIVATE Source)

target_link_libraries(NCChopStationTests PRIVATE
    NCChopFonts
    nc_engine
    Catch2::Catch2WithMain
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_compile_definitions(NCChopStationTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

add_test(NAME NCChopStationTests COMMAND NCChopStationTests)
