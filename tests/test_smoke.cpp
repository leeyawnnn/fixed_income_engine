#include <catch2/catch_test_macros.hpp>

#include "fi/version.hpp"

namespace fi {
const char* library_version() noexcept;
}

TEST_CASE("library reports its version", "[smoke]") {
    REQUIRE(std::string(fi::library_version()) == "0.1.0");
    REQUIRE(fi::version_major == 0);
}
