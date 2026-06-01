#include <catch2/catch_test_macros.hpp>

#include "fi/json.hpp"

using fi::json::parse;

TEST_CASE("JSON parses scalars, arrays and nested objects", "[json]") {
    auto v = parse(
        R"({"a":1.5,"b":"hi","c":[1,2,3],"d":true,"e":null,"f":{"g":-2}})");

    REQUIRE(v.contains("a"));
    REQUIRE(v["a"].number() == 1.5);
    REQUIRE(v["b"].as_string() == "hi");
    REQUIRE(v["c"].as_array().size() == 3);
    REQUIRE(v["c"].as_array()[2].number() == 3.0);
    REQUIRE(v["d"].as_bool() == true);
    REQUIRE(v["e"].is_null());
    REQUIRE(v["f"]["g"].number() == -2.0);
    REQUIRE_FALSE(v.contains("zzz"));
}

TEST_CASE("JSON handles whitespace, escapes and exponents", "[json]") {
    REQUIRE(parse(R"(  "a\nb\t\"c\"" )").as_string() == "a\nb\t\"c\"");
    REQUIRE(parse("1.25e-2").number() == 0.0125);
    auto arr = parse("[ ]");
    REQUIRE(arr.as_array().empty());
}

TEST_CASE("JSON rejects malformed input", "[json]") {
    REQUIRE_THROWS(parse("{bad}"));
    REQUIRE_THROWS(parse(R"({"a":1,})"));   // trailing comma
    REQUIRE_THROWS(parse(R"("unterminated)"));
    REQUIRE_THROWS(parse("[1,2"));          // unclosed array
}
