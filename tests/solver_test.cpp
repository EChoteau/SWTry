#include <catch2/catch_test_macros.hpp>

#include "swcore/account.hpp"
#include "swcore/solver.hpp"

#include <algorithm>
#include <string>

namespace {

swcore::Rune make_rune(std::uint64_t id, int slot, int stat, int value) {
    swcore::Rune rune;
    rune.rune_id = id;
    rune.slot = slot;
    rune.stars = 6;
    rune.upgrade_level = 15;
    rune.set_id = 17;
    rune.occupancy = swcore::RuneOccupancy::Inventory;
    rune.primary = swcore::StatValue{swcore::StatType::Unknown,
                                     stat == 2 || stat == 4 || stat == 6
                                         ? swcore::StatValueKind::Percent
                                         : swcore::StatValueKind::Flat,
                                     stat, value};
    return rune;
}

}  // namespace

TEST_CASE("Solver finds the only build and retains rune origins") {
    swcore::AccountMonster target;
    target.con = 648;
    target.attack = 801;
    target.defense = 582;
    target.speed = 108;
    target.crit_rate = 15;
    target.crit_damage = 50;
    target.resistance = 15;
    target.accuracy = 0;

    std::vector<swcore::Rune> pool;
    for (int slot = 1; slot <= 6; ++slot)
        pool.push_back(make_rune(1000 + slot, slot,
                                 slot == 1 ? 1 : slot == 3 ? 5 : 3, 0));

    swcore::SolverConfig config;
    config.scenarios.push_back({"any", {}, true});
    config.objective.mode = "optimize";
    config.objective.weights["hp"] = 1;
    config.limits.top_n = 1;

    const auto report = swcore::solve_runes(
        target, pool, {}, config,
        std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping");

    REQUIRE(report.scenarios.size() == 1);
    REQUIRE(report.scenarios[0].builds.size() == 1);
    CHECK(report.scenarios[0].builds[0].runes[0].rune_id == 1001);
    CHECK(report.scenarios[0].builds[0].stats.total.hp == 9720);
    CHECK(report.scenarios[0].builds[0].metrics.effective_hp > 0);
    CHECK(report.scenarios[0].builds[0].runes[0].source_name == "inventaire");
}

TEST_CASE("Solver returns no build for impossible constraints") {
    swcore::AccountMonster target;
    target.con = 648;
    std::vector<swcore::Rune> pool;
    for (int slot = 1; slot <= 6; ++slot)
        pool.push_back(make_rune(2000 + slot, slot, slot == 1 ? 1 : 3, 0));

    swcore::SolverConfig config;
    config.scenarios.push_back({"any", {}, true});
    config.constraints["hp"].minimum = 999999;

    const auto report = swcore::solve_runes(
        target, pool, {}, config,
        std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping");
    REQUIRE(report.scenarios.size() == 1);
    CHECK(report.scenarios[0].builds.empty());
    CHECK(report.limiting_constraints.size() == 1);
}

TEST_CASE("Derived solver metrics use the configured formulas") {
    const swcore::StatValues stats{10000, 2000, 1000, 150, 80, 70, 30, 20};
    const auto metrics = swcore::calculate_derived_metrics(stats);

    CHECK(metrics.effective_hp == 46400);
    CHECK(metrics.effective_hp_broken_def == 21900);
    CHECK(metrics.expected_damage == 3120);
    CHECK_FALSE(metrics.rune_efficiency_available);
}

TEST_CASE("Raoq solver configuration finds constrained builds in all scenarios") {
    const std::string export_path =
        std::string{SW_PROJECT_SOURCE_DIR} +
        "/tests/data/Qui-Gon-21196058-2026-11-02_001212.json";
    const std::string mapping_path =
        std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping";

    std::vector<std::string> warnings;
    const auto config = swcore::SolverConfig::load(
        std::string{SW_PROJECT_SOURCE_DIR} +
            "/tests/data/solver_raoq_config.json",
        mapping_path,
        [&](const std::string& warning) { warnings.push_back(warning); });
    const auto rune_mapping = swcore::RuneMapping::load(mapping_path);
    const auto account = swcore::load_account(
        export_path, rune_mapping,
        [&](const std::string& warning) { warnings.push_back(warning); });
    const auto pool = swcore::load_runes(
        export_path, rune_mapping,
        [&](const std::string& warning) { warnings.push_back(warning); });
    const auto set_effects = swcore::SetEffects::load(
        mapping_path + "/set_effects.json",
        [&](const std::string& warning) { warnings.push_back(warning); });

    const auto target = std::find_if(
        account.begin(), account.end(), [&](const swcore::AccountMonster& monster) {
            return monster.unit_id == *config.pool.target_unit_id;
        });
    REQUIRE(target != account.end());

    const auto report = swcore::solve_runes(
        *target, pool, set_effects, config, mapping_path,
        [&](const std::string& warning) { warnings.push_back(warning); });

    REQUIRE(report.scenarios.size() == 3);
    const auto& filters = report.scenarios[0].slot_filters;
    CHECK(filters[0].pool == 392);
    CHECK(filters[1].pool == 150);
    CHECK(filters[2].pool == 305);
    CHECK(filters[3].pool == 171);
    CHECK(filters[4].pool == 282);
    CHECK(filters[5].pool == 107);
    CHECK(filters[0].inventory + filters[0].target_equipped +
              filters[0].other_equipped ==
          filters[0].pool);
    CHECK(filters[0].inventory == 353);
    CHECK(filters[0].target_equipped == 6);
    CHECK(filters[0].other_equipped == 33);
    CHECK(filters[0].after_main_stat == 379);
    CHECK(filters[1].after_main_stat == 107);
    CHECK(filters[2].after_main_stat == 283);
    CHECK(filters[3].after_main_stat == 57);
    CHECK(filters[4].after_main_stat == 261);
    CHECK(filters[5].after_main_stat == 28);
    CHECK(report.scenarios[0].candidates_before == 1115);
    for (const auto& scenario : report.scenarios) {
        REQUIRE(scenario.nodes > 0);
        REQUIRE(scenario.candidates_before > 0);
        for (const auto& build : scenario.builds) {
            for (const auto& [name, constraint] : config.constraints) {
                double value = 0;
                if (name == "SPD") value = build.stats.total.speed;
                else if (name == "ACC") value = build.stats.total.accuracy;
                else if (name == "CRate") value = build.stats.total.crit_rate;
                else FAIL("unhandled test constraint: " << name);
                if (constraint.minimum) CHECK(value >= *constraint.minimum);
                if (constraint.maximum) CHECK(value <= *constraint.maximum);
            }
        }
    }
}
