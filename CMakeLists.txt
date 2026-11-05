cmake_minimum_required(VERSION 3.22)

project(NCSoundDAW VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CPM)
include(NcTargets)

nc_add_library(nc_engine
    SOURCES engine/src/version.cpp
    HEADERS engine/include/nc/version.hpp)

target_include_directories(nc_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/engine/include")

enable_testing()
add_subdirectory(tests)
