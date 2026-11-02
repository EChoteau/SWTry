#include <catch2/catch_test_macros.hpp>

#include "swcore/solver_config.hpp"

#include <string>
#include <vector>

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
