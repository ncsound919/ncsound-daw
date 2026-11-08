juce_add_plugin(NCEightOHeight
    COMPANY_NAME "NCSound"
    BUNDLE_ID "com.ncsound.eightoheight"
    PLUGIN_MANUFACTURER_CODE Ncsd
    PLUGIN_CODE 8o8d
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "808 Designer")

target_sources(NCEightOHeight PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(NCEightOHeight PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(NCEightOHeight PRIVATE
    nc_engine
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Headless DSP/state tests. The editor is GUI-only; these cover the voice
# engine, mono-lock, clipper ceiling and state round-trip.
add_executable(NCEightOHeightTests
    Source/EightOHeightTests.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(NCEightOHeightTests PRIVATE Source)

target_link_libraries(NCEightOHeightTests PRIVATE
    nc_engine
    Catch2::Catch2WithMain
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_compile_definitions(NCEightOHeightTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# $<TARGET_FILE> resolves the real per-config binary; a bare target
# name made ctest look in the build root and report "Not Run".
add_test(NAME NCEightOHeightTests COMMAND $<TARGET_FILE:NCEightOHeightTests>)
