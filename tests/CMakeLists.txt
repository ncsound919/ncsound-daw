CPMAddPackage("gh:catchorg/Catch2@3.7.1")

list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")

add_executable(nc_tests test_smoke.cpp test_slice_planner.cpp test_clip_timeline.cpp test_project_limits.cpp test_plugin_policy.cpp test_stream_window.cpp test_audiosuite.cpp test_sync_clock.cpp test_latency_comp.cpp test_project_state.cpp test_media_collect.cpp test_project_json.cpp test_midi_clip.cpp)
target_link_libraries(nc_tests PRIVATE nc_engine Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(nc_tests)

# The cache may hold an extensionless shim from an earlier configure; ctest
# cannot execute that on Windows, so re-resolve and prefer node.exe.
unset(NODE_EXECUTABLE CACHE)
find_program(NODE_EXECUTABLE NAMES node.exe node)
if(NODE_EXECUTABLE AND EXISTS "${NODE_EXECUTABLE}")
    add_test(NAME plugin_registry
             COMMAND "${NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/check-registry.mjs" "${CMAKE_SOURCE_DIR}/plugins/registry.json")
else()
    message(STATUS "node not found - plugin registry check disabled")
endif()
