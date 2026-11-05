CPMAddPackage("gh:catchorg/Catch2@3.7.1")

list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")

add_executable(nc_tests test_smoke.cpp test_slice_planner.cpp)
target_link_libraries(nc_tests PRIVATE nc_engine Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(nc_tests)
