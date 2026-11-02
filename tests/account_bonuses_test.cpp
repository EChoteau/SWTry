#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "swcore/account_bonuses.hpp"

TEST_CASE("Account bonuses parse all export sources and preserve unknowns") {
    std::vector<std::string> warnings;
    const auto bonuses = swcore::AccountBonuses::load(
        std::string{SW_PROJECT_SOURCE_DIR} +
            "/tests/data/Qui-Gon-21196058-2026-11-02_001212.json",
        std::string{SW_PROJECT_SOURCE_DIR} +
            "/data/mapping/account_bonuses.json",
        [&](const std::string& warning) { warnings.push_back(warning); });

    REQUIRE_FALSE(bonuses.values().empty());
    CHECK(std::all_of(
        bonuses.values().begin(), bonuses.values().end(),
        [](const swcore::AccountBonus& bonus) {
            return !bonus.definition.has_value();
        }));
}
