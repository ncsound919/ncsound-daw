#include <catch2/catch_test_macros.hpp>

#include <nc/version.hpp>

#include <string>

TEST_CASE("engine version is reported")
{
    REQUIRE(std::string(nc::version()).starts_with("0."));
}
