#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "swcore/runes/runes.hpp"

TEST_CASE("Rune loader preserves raw stat IDs and occupancy") {
    std::vector<std::string> warnings;
    const auto mapping = swcore::RuneMapping::load(
        std::string{SW_PROJECT_SOURCE_DIR} + "/data/game",
        [&](const std::string& warning) { warnings.push_back(warning); });
    REQUIRE(warnings.empty());
    const auto runes = swcore::load_runes(
        std::string{SW_PROJECT_SOURCE_DIR} + "/tests/data/exports/runes.json",
        mapping,
        [&](const std::string& warning) { warnings.push_back(warning); });

    REQUIRE(warnings.empty());
    REQUIRE(runes.size() == 2);
    CHECK(runes[0].primary->stat_id == 3);
    CHECK(runes[0].primary->kind == swcore::StatValueKind::Flat);
    CHECK(runes[0].secondary[0].value.stat_id == 10);
    CHECK(runes[0].secondary[0].value.kind == swcore::StatValueKind::Flat);
    CHECK(runes[0].occupancy == swcore::RuneOccupancy::Equipped);
    CHECK(runes[0].innate->stat == swcore::StatType::None);
    CHECK(runes[0].innate->stat_id == 0);
    CHECK(runes[1].innate->stat_id == 1);
    CHECK(runes[1].innate->kind == swcore::StatValueKind::Flat);
    CHECK(runes[1].occupancy == swcore::RuneOccupancy::Inventory);
}
