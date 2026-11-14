cmake_minimum_required(VERSION 3.22)

project(NCSoundDAW VERSION 0.1.0 LANGUAGES C CXX)

# Guard: an MSVC Release-class build must actually be optimized. When the build
# tree is configured from a shell whose environment lost the CMake defaults,
# the per-configuration C++ flag sets can end up empty; every C++ file then
# compiles at /Od and the audio engine cannot meet its real-time budget (the
# render alone exceeds the 10 ms WASAPI shared-mode block, which the user
# hears as periodic distortion). Every optimized configuration is covered, not
# just Release: RelWithDebInfo is what performance work is profiled in, and
# MinSizeRel is what size-constrained targets ship.
#
# Repair: an emptied flag set gets the platform default back, so a clean
# configure on a broken shell still produces an optimized build. A non-empty
# explicit override is never touched.
#
# Refuse: a non-empty override carrying no optimizer for a configuration this
# tree will compile fails configure, instead of quietly shipping an /Od
# "Release" that cannot hold the real-time budget. Only the configurations
# that will actually be compiled are checked, so a single-config Debug tree
# is not held to release flags it never uses.
if(MSVC)
    # What the environment should have supplied, keyed by configuration.
    set(_nc_opt_defaults_RELEASE        "/O2 /Ob2 /DNDEBUG")
    set(_nc_opt_defaults_RELWITHDEBINFO "/O2 /Ob2 /DNDEBUG")
    # MinSizeRel is deliberately size-optimized, so restore CMake's own
    # default rather than upgrading it to /O2 behind the user's back.
    set(_nc_opt_defaults_MINSIZEREL     "/O1 /Ob1 /DNDEBUG")
    # The switches that satisfy each configuration (MinSizeRel accepts /O2 too,
    # so an explicit /O2 there is not flagged as a regression).
    set(_nc_opt_ok_RELEASE        "/(O2|Ox)")
    set(_nc_opt_ok_RELWITHDEBINFO "/(O2|Ox)")
    set(_nc_opt_ok_MINSIZEREL     "/(O1|O2|Ox)")

    foreach(_nc_cfg RELEASE RELWITHDEBINFO MINSIZEREL)
        if("${CMAKE_CXX_FLAGS_${_nc_cfg}}" STREQUAL "")
            set(CMAKE_CXX_FLAGS_${_nc_cfg} "${_nc_opt_defaults_${_nc_cfg}}"
                CACHE STRING "CXX ${_nc_cfg} flags" FORCE)
        endif()
    endforeach()

    if(CMAKE_CONFIGURATION_TYPES)
        set(_nc_built_configs ${CMAKE_CONFIGURATION_TYPES})
    else()
        set(_nc_built_configs ${CMAKE_BUILD_TYPE})
    endif()

    foreach(_nc_cfg IN LISTS _nc_built_configs)
        string(TOUPPER "${_nc_cfg}" _nc_cfg_upper)

        if(NOT DEFINED _nc_opt_ok_${_nc_cfg_upper})
            continue()
        endif()

        set(_nc_flags "${CMAKE_CXX_FLAGS_${_nc_cfg_upper}}")

        if(NOT _nc_flags MATCHES "${_nc_opt_ok_${_nc_cfg_upper}}")
            message(FATAL_ERROR
                "CMAKE_CXX_FLAGS_${_nc_cfg_upper} has no optimization switch "
                "(expected ${_nc_opt_ok_${_nc_cfg_upper}}):\n"
                "  \"${_nc_flags}\"\n"
                "A ${_nc_cfg_upper} C++ build here compiles the audio engine at /Od "
                "and cannot meet the real-time block budget. Fix it with:\n"
                "  -DCMAKE_CXX_FLAGS_${_nc_cfg_upper}=\"${_nc_opt_defaults_${_nc_cfg_upper}}\"\n"
                "or delete that cache entry and re-configure.")
        endif()
    endforeach()
endif()

# Android needs 16 KB ELF alignment; no effect on desktop VST3 builds.
if(ANDROID)
    add_link_options(-Wl,-z,max-page-size=16384)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CPM)
include(NcTargets)

nc_add_library(nc_engine
    SOURCES
        engine/src/version.cpp
        engine/src/SlicePlanner.cpp
        engine/src/ClipTimeline.cpp
    HEADERS
        engine/include/nc/version.hpp
        engine/include/nc/engine/MidiClip.hpp
        engine/include/nc/engine/PitchStack.hpp
        engine/include/nc/engine/MidiPitch.hpp
        engine/include/nc/engine/SlicePlanner.hpp
        engine/include/nc/engine/ClipTimeline.hpp
        engine/include/nc/engine/PluginPolicy.hpp
        engine/include/nc/engine/StreamWindow.hpp
        engine/include/nc/engine/AudioSuite.hpp
        engine/include/nc/engine/SyncClock.hpp
        engine/include/nc/engine/LatencyComp.hpp
        engine/include/nc/engine/ProjectState.hpp
        engine/include/nc/engine/MediaCollect.hpp
        engine/include/nc/engine/ProjectJson.hpp
        engine/include/nc/engine/ProjectLimits.hpp
        engine/include/nc/engine/SpscQueue.hpp
        engine/include/nc/engine/TempoMap.hpp
        engine/include/nc/engine/Markers.hpp
        engine/include/nc/engine/ClipEdit.hpp
        engine/include/nc/engine/UndoStack.hpp
        engine/include/nc/engine/Export.hpp
        engine/include/nc/engine/Autosave.hpp
        engine/include/nc/engine/Comping.hpp
        engine/include/nc/engine/StudioFxDsp.hpp
        engine/include/nc/engine/StudioFxSlot.hpp
        engine/include/nc/engine/StudioFxCatalogData.hpp
        engine/include/nc/engine/StudioFxEq.hpp
        engine/include/nc/engine/StudioFxDynamics.hpp
        engine/include/nc/engine/StudioFxSpace.hpp
        engine/include/nc/engine/StudioFxVoice.hpp
        engine/include/nc/engine/StudioFxChain.hpp
        engine/include/nc/engine/MidiClip.hpp
        engine/include/nc/engine/MidiSchedule.hpp
        engine/include/nc/engine/MidiScheduler.hpp)

target_include_directories(nc_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/engine/include")

enable_testing()
add_subdirectory(tests)

# JUCE is a shallow clone in third_party so plugin builds stay offline and repeatable.
# The floor is 8.0.17-security: it is the release that bounded JSON and ValueTree
# nesting depth, confined ZipFile symlinks to the target directory, and hardened
# WAV/AIFF parsing. An older tree fails configure instead of silently shipping
# known-vulnerable code. See third_party/JUCE_PIN.txt for the exact commit.
set(NC_JUCE_MIN_VERSION "8.0.17")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt"
         _nc_juce_project REGEX "^project\\(JUCE VERSION")

    if(_nc_juce_project MATCHES "VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)")
        set(_nc_juce_version "${CMAKE_MATCH_1}")
    else()
        message(FATAL_ERROR "Could not read the JUCE version from third_party/JUCE/CMakeLists.txt")
    endif()

    if(_nc_juce_version VERSION_LESS NC_JUCE_MIN_VERSION)
        message(FATAL_ERROR
            "JUCE ${_nc_juce_version} in third_party/JUCE is below the required "
            "${NC_JUCE_MIN_VERSION}. Check out the security tag:\n"
            "  git -C third_party/JUCE fetch --depth 1 origin "
            "refs/tags/8.0.17-security:refs/tags/8.0.17-security\n"
            "  git -C third_party/JUCE checkout 8.0.17-security")
    endif()

    message(STATUS "Using JUCE ${_nc_juce_version} from third_party/JUCE")

    add_subdirectory(third_party/JUCE)
    add_subdirectory(plugins/chopstation)
    add_subdirectory(plugins/eightoheight)
    # The hub UI needs the WebView2 package at configure time. Resolve it from a
    # documented source rather than a path that only exists in one build cache.
    include(WebView2Deps)

    add_subdirectory(app)
else()
    message(STATUS "JUCE not found in third_party/JUCE - skipping plugin targets")
endif()

include(Packaging)
