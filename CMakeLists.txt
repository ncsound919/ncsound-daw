cmake_minimum_required(VERSION 3.22)

project(NCSoundDAW VERSION 0.1.0 LANGUAGES C CXX)

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
        engine/include/nc/engine/MidiClip.hpp
        engine/include/nc/engine/MidiSchedule.hpp)

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
    add_subdirectory(app)
else()
    message(STATUS "JUCE not found in third_party/JUCE - skipping plugin targets")
endif()
