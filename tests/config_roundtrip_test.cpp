#include <catch2/catch_test_macros.hpp>

#include "swcore/account/account.hpp"
#include "swcore/solver/solve_async.hpp"
#include "swcore/solver/solver_config.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

const std::string kRoot{SW_PROJECT_SOURCE_DIR};
const std::string kMapping = kRoot + "/data/game";

swcore::SolverConfig reparse(const swcore::SolverConfig& config,
                             std::vector<std::string>& warnings) {
    return swcore::SolverConfig::parse(
        config.to_json(), kMapping,
        [&](const std::string& warning) { warnings.push_back(warning); });
}

}  // namespace

TEST_CASE("Solver config JSON round trip: every supported key") {
    swcore::SolverConfig config;
    config.pool.target_unit_id = 17858844162ULL;
    config.pool.locked_unit_ids = {1, 2};
    config.pool.inventory_only = true;
    config.pool.minimum_rune_level = 12;
    config.pool.minimum_stars = 6;
    config.pool.projection_level = swcore::kProjectionCurrent;
    config.scenarios = {{"Fatal + Lame", {{8, 4}, {4, 2}}, false}, {"Tous", {}, true}};
    config.main_stats[2] = {false, {4, 8}};
    config.main_stats[4] = {true, {}};
    config.main_stats[6] = {false, {4}};
    config.constraints["speed"] = {170.0, std::nullopt};
    config.constraints["crit_rate"] = {70.0, 100.0};
    config.constraints["effective_hp"] = {std::nullopt, 50000.5};
    config.craft_min_level = 9;
    config.grindstones[4] = {swcore::GrindstoneMode::Max, 5, 0};
    config.grindstones[8] = {swcore::GrindstoneMode::Exact, 4, 3};
    config.grindstones[2] = {swcore::GrindstoneMode::Min, 3, 0};
    config.max_new_grinds = 2;
    config.gems[9] = {swcore::GrindstoneMode::Exact, 5, 7};
    config.gems[4] = {swcore::GrindstoneMode::Max, 4, 0};
    config.max_new_gems = 1;
    config.gem_regem_existing = false;
    config.objective.mode = "explore";
    config.objective.weights = {{"attack", 1.0}, {"crit_damage", 8.25}};
    config.objective.caps = {{"crit_rate", 100.0}};
    config.objective.sort_metrics = {"speed"};
    config.limits.top_n = 0;
    config.limits.time_budget_ms = 60000;
    config.limits.node_limit = 123456;
    config.limits.max_builds_per_scenario = 7;
    config.limits.one_build_per_set_composition = true;
    config.limits.max_results = 1000;
    config.limits.min_score = 3000.5;

    std::vector<std::string> warnings;
    const auto back = reparse(config, warnings);
    CHECK(warnings.empty());
    CHECK(back.errors.empty());
    CHECK(back.pool == config.pool);
    CHECK(back.scenarios == config.scenarios);
    CHECK(back.main_stats == config.main_stats);
    CHECK(back.constraints == config.constraints);
    CHECK(back.grindstones == config.grindstones);
    CHECK(back.gems == config.gems);
    CHECK(back.objective == config.objective);
    CHECK(back.limits == config.limits);
    CHECK(back == config);
}

TEST_CASE("Solver config JSON round trip: test configs read by the CLI") {
    for (const char* file :
         {"solver_raoq_config.json", "solver_raoq_atk_test.json",
          "solver_raoq_atk_craft.json", "solver_grind_config.json",
          "solver_gems_config.json"}) {
        INFO(file);
        std::vector<std::string> ignored;
        const auto loaded = swcore::SolverConfig::load(
            kRoot + "/tests/data/solver_configs/" + file, kMapping,
            [&](const std::string& warning) { ignored.push_back(warning); });
        std::vector<std::string> warnings;
        const auto back = reparse(loaded, warnings);
        CHECK(warnings.empty());
        CHECK(back == loaded);
    }
}

TEST_CASE("Async solver gives the synchronous result and can be cancelled") {
    const auto mapping = swcore::RuneMapping::load(kMapping);
    const std::string export_path =
        kRoot + "/tests/data/exports/Qui-Gon-21196058-2026-11-02_001212.json";
    const auto account = swcore::load_account(export_path, mapping, {});
    const auto runes = swcore::load_runes(export_path, mapping, {});
    const auto effects = swcore::SetEffects::load(kMapping + "/runes/set_effects.json");
    const auto config =
        swcore::SolverConfig::load(kRoot + "/tests/data/solver_configs/solver_raoq_atk_test.json",
                                   kMapping);
    const auto target = std::find_if(
        account.begin(), account.end(), [&](const swcore::AccountMonster& m) {
            return m.unit_id == *config.pool.target_unit_id;
        });
    REQUIRE(target != account.end());

    swcore::AsyncSolver solver;
    solver.start(*target, runes, effects, config, kMapping);
    const auto report = solver.wait();
    REQUIRE(report.scenarios.size() == 1);
    REQUIRE_FALSE(report.scenarios[0].builds.empty());
    CHECK(report.scenarios[0].builds.front().score == 3084);
    const auto done = solver.snapshot();
    CHECK_FALSE(done.running);
    CHECK(done.builds_found == report.scenarios[0].builds.size());
    CHECK_FALSE(done.incomplete);

    // Annulation immédiate : la recherche s'arrête, résultat marqué incomplet
    // (ou terminé avant l'annulation, selon la vitesse de la machine).
    auto everything = config;
    everything.limits.top_n = 0;
    solver.start(*target, runes, effects, everything, kMapping);
    solver.cancel();
    const auto cancelled = solver.wait();
    const auto after = solver.snapshot();
    CHECK_FALSE(after.running);
    if (after.incomplete) {
        for (const auto& scenario : cancelled.scenarios)
            CHECK(scenario.incomplete);
    }
}

TEST_CASE("GUI helpers: candidate counts and expanded builds match the solver") {
    const auto mapping = swcore::RuneMapping::load(kMapping);
    const std::string export_path =
        kRoot + "/tests/data/exports/Qui-Gon-21196058-2026-11-02_001212.json";
    const auto account = swcore::load_account(export_path, mapping, {});
    const auto runes = swcore::load_runes(export_path, mapping, {});
    const auto effects = swcore::SetEffects::load(kMapping + "/runes/set_effects.json");
    for (const char* file : {"solver_raoq_atk_test.json", "solver_raoq_atk_craft.json"}) {
        INFO(file);
        auto config =
            swcore::SolverConfig::load(kRoot + "/tests/data/solver_configs/" + file, kMapping);
        config.limits.top_n = 5;
        const auto target = std::find_if(
            account.begin(), account.end(), [&](const swcore::AccountMonster& m) {
                return m.unit_id == *config.pool.target_unit_id;
            });
        REQUIRE(target != account.end());
        const auto report =
            swcore::solve_runes(*target, runes, effects, config, kMapping);
        REQUIRE(report.scenarios.size() == 1);
        const auto& scenario = report.scenarios[0];

        const auto counts = swcore::count_candidate_runes(runes, config);
        for (std::size_t slot = 0; slot < 6; ++slot)
            CHECK(counts[slot] == scenario.slot_filters[slot].after_set);

        REQUIRE_FALSE(scenario.builds.empty());
        for (const auto& build : scenario.builds) {
            swcore::CompactBuild compact;
            for (std::size_t slot = 0; slot < 6; ++slot)
                compact.rune_ids[slot] = build.runes[slot].rune_id;
            compact.variants = build.variants;
            const auto expanded = swcore::expand_build(
                *target, runes, effects, config, kMapping, compact, scenario.name);
            REQUIRE(expanded.has_value());
            CHECK(expanded->score == build.score);
            CHECK(expanded->stats.total.attack == build.stats.total.attack);
            CHECK(expanded->stats.total.speed == build.stats.total.speed);
            CHECK(expanded->grind_cost == build.grind_cost);
            CHECK(expanded->gem_cost == build.gem_cost);
        }
    }
}
