#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "swcore/reference_db.hpp"

TEST_CASE("ReferenceDB indexes monsters by com2us_id") {
    std::vector<std::string> warnings;
    const auto database = swcore::ReferenceDB::load(
        std::string{SW_PROJECT_SOURCE_DIR} + "/tests/data/reference_monsters.json",
        [&](const std::string& warning) { warnings.push_back(warning); });

    REQUIRE(database.size() == 2);
    const auto* monster = database.find(12302);
    REQUIRE(monster != nullptr);
    CHECK(monster->name == "Forest Keeper");
    CHECK(monster->element == swcore::Element::Fire);
    CHECK(monster->skills == std::vector<int>{4});
    CHECK(database.find(999) == nullptr);
    CHECK(warnings.empty());
}

TEST_CASE("ReferenceDB warns and defaults missing fields") {
    std::vector<std::string> warnings;
    const auto database = swcore::ReferenceDB::load(
        std::string{SW_PROJECT_SOURCE_DIR} +
            "/tests/data/reference_monsters_missing.json",
        [&](const std::string& warning) { warnings.push_back(warning); });

    REQUIRE(database.find(321) != nullptr);
    CHECK(database.find(321)->name == "Incomplete");
    CHECK_FALSE(database.find(321)->base_hp.has_value());
    CHECK(database.find(321)->element == swcore::Element::Unknown);
    CHECK_FALSE(warnings.empty());
}

TEST_CASE("ReferenceDB treats null SWARFARM stats as absent without warnings") {
    std::vector<std::string> warnings;
    const auto database = swcore::ReferenceDB::load(
        std::string{SW_PROJECT_SOURCE_DIR} +
            "/tests/data/reference_monsters_null_stats.json",
        [&](const std::string& warning) { warnings.push_back(warning); });

    REQUIRE(database.size() == 2);
    const auto* absent = database.find(777);
    REQUIRE(absent != nullptr);
    CHECK_FALSE(absent->base_hp.has_value());
    CHECK_FALSE(absent->base_attack.has_value());
    CHECK_FALSE(absent->base_defense.has_value());
    CHECK_FALSE(absent->max_lvl_hp.has_value());
    CHECK_FALSE(absent->max_lvl_attack.has_value());
    CHECK_FALSE(absent->max_lvl_defense.has_value());
    CHECK_FALSE(absent->has_stats());
    CHECK(absent->speed == 100);

    const auto* complete = database.find(12302);
    REQUIRE(complete != nullptr);
    CHECK(complete->has_stats());
    CHECK(complete->max_lvl_hp == 6420);

    CHECK(database.monsters_without_stats() == std::vector<int>{777});
    CHECK(warnings.empty());
}
