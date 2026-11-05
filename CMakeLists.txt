cmake_minimum_required(VERSION 3.22)

project(NCSoundDAW VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CPM)
include(NcTargets)

nc_add_library(nc_engine
    SOURCES
        engine/src/version.cpp
        engine/src/SlicePlanner.cpp
    HEADERS
        engine/include/nc/version.hpp
        engine/include/nc/engine/SlicePlanner.hpp)

target_include_directories(nc_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/engine/include")

enable_testing()
add_subdirectory(tests)

# JUCE is a shallow clone in third_party so plugin builds stay offline and repeatable.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    add_subdirectory(third_party/JUCE)
    add_subdirectory(plugins/chopstation)
else()
    message(STATUS "JUCE not found in third_party/JUCE - skipping plugin targets")
endif()
