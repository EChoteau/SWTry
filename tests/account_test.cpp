#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "swcore/account.hpp"
#include "swcore/reference_db.hpp"

TEST_CASE("Account monster without runes matches ReferenceDB max-level stats") {
    std::vector<std::string> warnings;
    const auto account = swcore::load_account(
        std::string{SW_PROJECT_SOURCE_DIR} + "/tests/data/account_monster.json",
        [&](const std::string& warning) { warnings.push_back(warning); });
    const auto reference = swcore::ReferenceDB::load(
        std::string{SW_PROJECT_SOURCE_DIR} +
            "/tests/data/reference_monsters.json");

    REQUIRE(account.size() == 1);
    const auto* monster = reference.find(22901);
    REQUIRE(monster != nullptr);
    CHECK(account[0].runes.empty());
    CHECK(account[0].attack == monster->max_lvl_attack);
    CHECK(account[0].defense == monster->max_lvl_defense);
    CHECK(account[0].speed == monster->speed);
    CHECK(account[0].con * 15 == monster->max_lvl_hp);
    CHECK(account[0].artifacts.size() == 1);
    CHECK(account[0].artifacts[0].primary[1] == 1500.0);
    CHECK(warnings.empty());
}

TEST_CASE("Account equipment is stably ordered by slot") {
    std::vector<std::string> warnings;
    const auto account = swcore::load_account(
        std::string{SW_PROJECT_SOURCE_DIR} +
            "/tests/data/Qui-Gon-21196058-2026-11-02_001212.json",
        [&](const std::string& warning) { warnings.push_back(warning); });

    REQUIRE_FALSE(account.empty());
    for (const auto& monster : account) {
        for (std::size_t i = 1; i < monster.runes.size(); ++i)
            CHECK(monster.runes[i - 1].slot <= monster.runes[i].slot);
        for (std::size_t i = 1; i < monster.artifacts.size(); ++i)
            CHECK(monster.artifacts[i - 1].slot <= monster.artifacts[i].slot);
    }
}
