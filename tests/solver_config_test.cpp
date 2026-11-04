#include <catch2/catch_test_macros.hpp>

#include "swcore/solver_config.hpp"

#include <string>
#include <vector>

TEST_CASE("Scenario name is generated from its sets unless one was typed") {
    const std::unordered_map<int, std::string> names{{8, "Fatal"}, {4, "Blade"}};
    swcore::SetScenario scenario{"", {{8, 4}, {4, 2}}, false};
    CHECK(swcore::scenario_label(scenario, names) == "Fatal 4 + Blade 2");
    CHECK(swcore::scenario_display_name(scenario, names) == "Fatal 4 + Blade 2");
    scenario.name = "  ";  // espaces seuls : pas un nom saisi
    CHECK(swcore::scenario_display_name(scenario, names) == "Fatal 4 + Blade 2");
    scenario.name = "Mon build";
    CHECK(swcore::scenario_display_name(scenario, names) == "Mon build");
    const swcore::SetScenario any{"", {}, true};
    CHECK(swcore::scenario_display_name(any, names) == "Tous sets");
    // Set inconnu du mapping : identifiant, jamais un nom inventé.
    const swcore::SetScenario unknown{"", {{99, 6}}, false};
    CHECK(swcore::scenario_label(unknown, names) == "Set 99 6");
}

TEST_CASE("Solver configuration loads pool scenarios and validated options") {
    std::vector<std::string> warnings;
    const auto config = swcore::SolverConfig::load(
        std::string{SW_PROJECT_SOURCE_DIR} + "/tests/data/solver_config.json",
        std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping",
        [&](const std::string& warning) { warnings.push_back(warning); });

    CHECK(config.pool.minimum_rune_level == 12);
    CHECK(config.pool.minimum_stars == 5);
    CHECK(config.pool.target_unit_id == 17858844162);
    CHECK(config.scenarios.size() == 2);
    CHECK(config.scenarios[0].sets[0].set_id == 5);
    CHECK(config.main_stats[2].stat_ids == std::vector<int>{8, 9});
    CHECK(config.main_stats[4].all);
    CHECK(config.constraints.at("effective_hp").minimum == 20000);
    CHECK(config.grindstones.at(1).mode == swcore::GrindstoneMode::Max);
    CHECK(!config.grindstones.contains(9));
    CHECK(config.objective.mode == "explore");
    CHECK(config.limits.node_limit == 100000);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("grindstone stat ignored") != std::string::npos);
}

namespace {

swcore::SolverConfig load_test_config(const std::string& file,
                                      std::vector<std::string>& warnings) {
    return swcore::SolverConfig::load(
        std::string{SW_PROJECT_SOURCE_DIR} + "/tests/data/" + file,
        std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping",
        [&](const std::string& warning) { warnings.push_back(warning); });
}

}  // namespace

TEST_CASE("Grindstone config: default, per-stat rules, exact roll and budget") {
    std::vector<std::string> warnings;
    const auto config = load_test_config("solver_grind_config.json", warnings);
    CHECK(config.errors.empty());
    CHECK(config.craft_min_level == 12);
    REQUIRE(config.max_new_grinds.has_value());
    CHECK(*config.max_new_grinds == 2);
    // "stats" présent : seulement les stats listées et meulables.
    REQUIRE(config.grindstones.size() == 3);
    CHECK(config.grindstones.at(2).mode == swcore::GrindstoneMode::Min);
    CHECK(config.grindstones.at(2).rarity == 5);  // rareté venant de "default"
    CHECK(config.grindstones.at(4).mode == swcore::GrindstoneMode::Max);
    CHECK(config.grindstones.at(8).mode == swcore::GrindstoneMode::Exact);
    CHECK(config.grindstones.at(8).rarity == 4);
    CHECK(config.grindstones.at(8).exact == 3);
    CHECK_FALSE(config.grindstones.contains(9));
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("grindstone stat ignored") != std::string::npos);
}

TEST_CASE("Grindstone config: default alone covers every grindable stat") {
    std::vector<std::string> warnings;
    const auto config = load_test_config("solver_grind_default.json", warnings);
    CHECK(config.errors.empty());
    CHECK_FALSE(config.max_new_grinds.has_value());
    for (const int stat : {1, 2, 3, 4, 5, 6, 8}) {
        REQUIRE(config.grindstones.contains(stat));
        CHECK(config.grindstones.at(stat).mode == swcore::GrindstoneMode::Max);
        CHECK(config.grindstones.at(stat).rarity == 5);
    }
    for (const int stat : {9, 10, 11, 12}) CHECK_FALSE(config.grindstones.contains(stat));
    CHECK(warnings.empty());
}

TEST_CASE("Grindstone config: an exact roll outside the rarity range is an error") {
    std::vector<std::string> warnings;
    const auto config = load_test_config("solver_grind_bad.json", warnings);
    REQUIRE(config.errors.size() == 1);
    CHECK(config.errors[0].find("9") != std::string::npos);
    CHECK(config.grindstones.empty());
}

TEST_CASE("Gem config: allowed stats come from enchanted_gem.json, exact roll") {
    std::vector<std::string> warnings;
    const auto config = load_test_config("solver_gems_config.json", warnings);
    CHECK(config.errors.empty());
    REQUIRE(config.max_new_gems.has_value());
    CHECK(*config.max_new_gems == 2);
    REQUIRE(config.gems.size() == 2);
    CHECK(config.gems.at(4).mode == swcore::GrindstoneMode::Max);
    CHECK(config.gems.at(4).rarity == 5);
    // Tx crit. (9) : non meulable mais gemmable ; valeur exacte 7 (rareté 5 : 6-9).
    CHECK(config.gems.at(9).mode == swcore::GrindstoneMode::Exact);
    CHECK(config.gems.at(9).exact == 7);
    CHECK(config.gems.at(9).rarity == 5);
    CHECK(config.grindstones.empty());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("gem stat ignored") != std::string::npos);
    CHECK(warnings[0].find("13") != std::string::npos);
}

TEST_CASE("Gem config: default alone allows every stat of enchanted_gem.json") {
    std::vector<std::string> warnings;
    const auto config = load_test_config("solver_raoq_atk_craft.json", warnings);
    CHECK(config.errors.empty());
    for (int stat = 1; stat <= 12; ++stat) {
        if (stat == 7) continue;
        CHECK(config.gems.contains(stat));
    }
    for (const int stat : {1, 2, 3, 4, 5, 6, 8}) CHECK(config.grindstones.contains(stat));
    CHECK_FALSE(config.max_new_gems.has_value());
    CHECK_FALSE(config.max_new_grinds.has_value());
}

TEST_CASE("Grindstone config: empty or absent disables simulated grinds") {
    std::vector<std::string> warnings;
    const auto config = load_test_config("solver_raoq_atk_test.json", warnings);
    CHECK(config.grindstones.empty());
    CHECK_FALSE(config.max_new_grinds.has_value());
}
