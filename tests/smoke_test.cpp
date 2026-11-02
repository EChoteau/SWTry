#include <catch2/catch_test_macros.hpp>

#include <string>

#include "swcore/swcore.hpp"

TEST_CASE("swcore library is available") {
    REQUIRE(std::string{swcore::library_name()} == "swcore");
}
