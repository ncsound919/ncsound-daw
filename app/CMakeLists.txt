# The native mix rack is compiled once and shared by the app and its DSP test.
# It embeds the real Overlay FX / Open Mic / Mixology sources instead of
# reimplementing their signal paths.
set(NC_PLUGINS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../ChordStudio-master")

add_library(nc_mixrack STATIC
    Source/MixRack.cpp
    "${NC_PLUGINS_ROOT}/plugins/mixology/Source/Engine/ChainResolver.cpp"
    "${NC_PLUGINS_ROOT}/plugins/mixology/Source/Engine/OverlaySessionWriter.cpp"
    "${NC_PLUGINS_ROOT}/plugins/mixology/Source/Data/TechniqueLibrary.cpp"
    "${NC_PLUGINS_ROOT}/plugins/mixology/Source/Data/ChordStudioCapabilities.cpp"
    "${NC_PLUGINS_ROOT}/plugins/mixology/Source/Data/InstrumentPresets.cpp"
    "${NC_PLUGINS_ROOT}/plugins/Open Mic/mic-preamp-modeler/src/SaturationStage.cpp"
    "${NC_PLUGINS_ROOT}/plugins/Open Mic/mic-preamp-modeler/src/Oversampler.cpp")

target_include_directories(nc_mixrack PUBLIC
    Source
    "${NC_PLUGINS_ROOT}/plugins/overlay-fx/libs/fx_core/include"
    "${NC_PLUGINS_ROOT}/plugins/overlay-fx/plugins/overlay_master/Source"
    "${NC_PLUGINS_ROOT}/plugins/Open Mic/mic-preamp-modeler/include"
    "${NC_PLUGINS_ROOT}/plugins/mixology/Source")

target_link_libraries(nc_mixrack PUBLIC
    nc_engine
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_core)

if (MSVC)
    target_compile_options(nc_mixrack PRIVATE /utf-8 /EHsc)
endif()

juce_add_console_app(NCMixRackTests PRODUCT_NAME "NCMixRackTests")

target_sources(NCMixRackTests PRIVATE
    Source/MixRackTests.cpp
    Source/TrackInsertTests.cpp
    Source/PluginCatalogTests.cpp
    Source/VstHostSmokeTests.cpp
    Source/HostedInsert.cpp
    Source/BuiltinSynth.cpp
    Source/MidiRecorder.cpp
    Source/TrackInsertRack.cpp
    Source/Automation.cpp
    Source/MixPlan.cpp
    Source/PluginCatalog.cpp
    Source/PluginScanner.cpp
    Source/PluginRegistry.cpp
    # The engine itself, so the mixdown export is covered end to end.
    Source/AudioEngine.cpp
    Source/PluginHost.cpp
    Source/ExportTests.cpp
    Source/RecordingTests.cpp
    Source/AutomationTests.cpp
    Source/MixPlanTests.cpp)

target_link_libraries(NCMixRackTests PRIVATE
    nc_mixrack
    # AudioEngine's include chain reaches Theme.h, which needs the generated
    # BinaryData.h; NCSoundFonts provides it (declared below).
    NCSoundFonts
    juce::juce_audio_processors
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_core
    Catch2::Catch2WithMain)

target_compile_definitions(NCMixRackTests PRIVATE JUCE_PLUGINHOST_VST3=1)

if (MSVC)
    target_compile_options(NCMixRackTests PRIVATE /utf-8 /EHsc)
endif()

# Registered with CTest so `ctest` and CI actually run this suite; without it the
# 52k assertions below only run when the binary is started by hand.
add_test(NAME mix_rack COMMAND NCMixRackTests)

# Manual gate tool for the child-process plugin scanner; not part of a normal build.
juce_add_console_app(NCScannerProbe PRODUCT_NAME "NCScannerProbe")

target_sources(NCScannerProbe PRIVATE
    Source/ScannerProbe.cpp
    Source/PluginScanner.cpp
    Source/PluginScanner.h
    Source/PluginCatalog.cpp
    Source/PluginCatalog.h
    Source/PluginRegistry.cpp
    Source/PluginRegistry.h)

target_compile_definitions(NCScannerProbe PRIVATE JUCE_PLUGINHOST_VST3=1)

if (MSVC)
    target_compile_options(NCScannerProbe PRIVATE /utf-8 /EHsc)
endif()

target_link_libraries(NCScannerProbe PRIVATE
    nc_engine
    juce::juce_audio_processors
    juce::juce_events
    juce::juce_core)

set_target_properties(NCScannerProbe PROPERTIES EXCLUDE_FROM_ALL TRUE)

juce_add_gui_app(NCSoundHub
    PRODUCT_NAME "NCSound Music Hub"
    COMPANY_NAME "Overlay365"
    BUNDLE_ID "com.overlay365.ncsoundhub")

juce_add_binary_data(NCSoundFonts SOURCES
    ../assets/fonts/Anton-Regular.ttf
    ../assets/fonts/SpaceGrotesk-Regular.ttf
    ../assets/fonts/SpaceGrotesk-Bold.ttf
    ../assets/fonts/SpaceMono-Bold.ttf)

target_sources(NCSoundHub PRIVATE
    Source/Main.cpp
    Source/MixRack.h
    Source/MixologyBridge.cpp
    Source/MixologyBridge.h
    Source/MixRackEditor.cpp
    Source/MixRackEditor.h
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/AudioEngine.cpp
    Source/AudioEngine.h
    Source/ArrangeView.cpp
    Source/ArrangeView.h
    Source/BrowserOverlay.cpp
    Source/BrowserOverlay.h
    Source/ChannelStrip.cpp
    Source/ChannelStrip.h
    Source/InspectorPanel.cpp
    Source/InspectorPanel.h
    Source/MixerPanel.cpp
    Source/MixerPanel.h
    Source/PluginHost.cpp
    Source/PluginCatalog.cpp
    Source/PluginCatalog.h
    Source/PluginScanner.cpp
    Source/PluginScanner.h
    Source/HostedInsert.cpp
    Source/HostedInsert.h
    Source/PluginEditorWindow.cpp
    Source/PluginEditorWindow.h
    Source/MidiRecorder.cpp
    Source/MidiRecorder.h
    Source/BuiltinSynth.cpp
    Source/BuiltinSynth.h
    Source/PianoRoll.cpp
    Source/PianoRoll.h
    Source/TrackInsertRack.cpp
    Source/Automation.cpp
    Source/MixPlan.cpp
    Source/MixPlan.h
    Source/TrackInsertRack.h
    Source/ProjectFile.cpp
    Source/ProjectFile.h
    Source/PluginHost.h
    Source/PluginRegistry.cpp
    Source/PluginRegistry.h
    Source/Session.h
    Source/Theme.h
    Source/TransportBar.cpp
    Source/TransportBar.h
    Source/Widgets.cpp
    Source/Widgets.h)

target_compile_definitions(NCSoundHub PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1)

# Source strings use UTF-8 punctuation (middle dot, en dash).
if (MSVC)
    target_compile_options(NCSoundHub PRIVATE /utf-8)
endif()

target_link_libraries(NCSoundHub PRIVATE
    NCSoundFonts
    nc_mixrack
    nc_engine
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
