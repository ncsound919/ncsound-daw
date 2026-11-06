juce_add_gui_app(NCSoundHub
    PRODUCT_NAME "NCSound Music Hub"
    COMPANY_NAME "Overlay365"
    BUNDLE_ID "com.overlay365.ncsoundhub")

target_sources(NCSoundHub PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/PluginRegistry.cpp
    Source/PluginRegistry.h)

target_compile_definitions(NCSoundHub PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(NCSoundHub PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
