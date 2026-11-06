juce_add_gui_app(NCSoundHub
    PRODUCT_NAME "NCSound Music Hub"
    COMPANY_NAME "Overlay365"
    BUNDLE_ID "com.overlay365.ncsoundhub")

juce_add_binary_data(NCSoundFonts SOURCES
    ../assets/fonts/Inter.ttf
    ../assets/fonts/DMSans.ttf
    ../assets/fonts/BremenBlack.ttf
    ../assets/fonts/BankGothic.ttf
    ../assets/fonts/DigitalReadout.ttf)

target_sources(NCSoundHub PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/AudioEngine.cpp
    Source/AudioEngine.h
    Source/ChannelStrip.h
    Source/Theme.h
    Source/PluginRegistry.cpp
    Source/PluginRegistry.h)

target_compile_definitions(NCSoundHub PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(NCSoundHub PRIVATE
    NCSoundFonts
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
