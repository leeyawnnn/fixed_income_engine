#include <catch2/catch_test_macros.hpp>

#include <string>

#include "fi/version.hpp"

TEST_CASE("library reports its version", "[smoke]") {
    REQUIRE(std::string(fi::library_version()) == "0.1.0");
    REQUIRE(fi::version_major == 0);
}
