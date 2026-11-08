# The native mix rack is compiled once and shared by the app and its DSP test.
# It embeds the real Overlay FX / Open Mic / Mixology sources instead of
# reimplementing their signal paths.
# Resolved and hash-checked by cmake/ChordStudioDeps.cmake: the vendored
# copy under third_party/chordstudio is the default, overridable with
# -DNC_PLUGINS_ROOT=<path> or the NC_PLUGINS_ROOT environment variable.
include(ChordStudioDeps)

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
    Source/MidiLearnManager.cpp
    Source/MidiLearnManager.h
    Source/TrackInsertRack.cpp
    Source/AuxBus.cpp
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
    Source/SendsTests.cpp
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
    NEEDS_WEBVIEW2 TRUE
    BUNDLE_ID "com.overlay365.ncsoundhub")

juce_add_binary_data(NCSoundFonts SOURCES
    ../assets/fonts/Anton-Regular.ttf
    ../assets/fonts/SpaceGrotesk-Regular.ttf
    ../assets/fonts/SpaceGrotesk-Bold.ttf
    ../assets/fonts/SpaceMono-Bold.ttf)

# React pitch UI (VST DAW/ui, built with Vite) embedded via BinaryData and
# served to the WebBrowserComponent through a ResourceProvider.
# If ui/dist is stale, rebuild it:  cd ui; npm install; npm run build
set(VSTDAW_WEBUI_DIST "${CMAKE_CURRENT_SOURCE_DIR}/../ui/dist")

if(NOT EXISTS "${VSTDAW_WEBUI_DIST}/index.html")
    message(FATAL_ERROR
        "ui/dist/index.html not found. Run 'npm install && npm run build' in ${CMAKE_CURRENT_SOURCE_DIR}/../ui.")
endif()

file(GLOB_RECURSE VSTDAW_WEBUI_DIST_FILES CONFIGURE_DEPENDS "${VSTDAW_WEBUI_DIST}/*")
list(FILTER VSTDAW_WEBUI_DIST_FILES EXCLUDE REGEX "\\.map$")

juce_add_binary_data(VstawWebUI
    NAMESPACE VstawWeb
    HEADER_NAME VstawWeb.h
    SOURCES ${VSTDAW_WEBUI_DIST_FILES})

target_sources(NCSoundHub PRIVATE
    Source/Main.cpp
    Source/MidiLearnManager.cpp
    Source/MidiLearnManager.h
    Source/MixRack.h
    Source/MixologyBridge.cpp
    Source/MixologyBridge.h
    Source/MixRackEditor.cpp
    Source/MixRackEditor.h
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/WebViewBridge.cpp
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
    Source/SettingsWindow.cpp
    Source/SettingsWindow.h
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
    Source/AuxBus.cpp
    Source/AuxBus.h
    Source/AutomationLaneView.cpp
    Source/AutomationLaneView.h
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
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_USE_ASIO=1
    JUCE_PLUGINHOST_VST3=1)

# Windows: declare DPI awareness in the app manifest so the WebView content
# is not bitmap-stretched on scaled displays.
if(MSVC)
    set_target_properties(NCSoundHub PROPERTIES VS_DPI_AWARE "PerMonitor")
endif()

# Source strings use UTF-8 punctuation (middle dot, en dash).
if (MSVC)
    target_compile_options(NCSoundHub PRIVATE /utf-8)
endif()

target_link_libraries(NCSoundHub PRIVATE
    NCSoundFonts
    VstawWebUI
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
