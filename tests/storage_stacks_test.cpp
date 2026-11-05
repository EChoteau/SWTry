#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "swcore/account/storage_stacks.hpp"

TEST_CASE("StorageStacks preserves sealed monster stacks") {
    std::vector<std::string> warnings;
    const auto storage = swcore::StorageStacks::load(
        std::string{SW_PROJECT_SOURCE_DIR} +
            "/tests/data/exports/storage-1793574749027.json",
        [&](const std::string& warning) { warnings.push_back(warning); });

    REQUIRE(warnings.empty());
    REQUIRE(storage.stacks().size() == 386);
    CHECK(storage.totalQuantity() == 4422);
    CHECK(storage.distinctEntryCount() == 386);
    CHECK(storage.stacks().front() ==
          swcore::StoredStack{10101, 3, 3});
}

TEST_CASE("StorageStacks skips malformed entries with warnings") {
    std::vector<std::string> warnings;
    const auto storage = swcore::StorageStacks::load(
        std::string{SW_PROJECT_SOURCE_DIR} +
            "/tests/data/exports/storage_stacks_invalid.json",
        [&](const std::string& warning) { warnings.push_back(warning); });

    REQUIRE(storage.stacks().size() == 2);
    CHECK(storage.totalQuantity() == 3);
    CHECK(storage.distinctEntryCount() == 2);
    CHECK(warnings.size() == 2);
}
