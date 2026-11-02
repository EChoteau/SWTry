#include <catch2/catch_test_macros.hpp>

#include "swcore/account.hpp"
#include "swcore/solver.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

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

    REQUIRE(report.scenarios.size() == 2);
    // Scores de référence (Fatal + Blade, Rage + Blade) ; recherche complète.
    CHECK_FALSE(report.scenarios[0].incomplete);
    CHECK_FALSE(report.scenarios[1].incomplete);
    REQUIRE_FALSE(report.scenarios[0].builds.empty());
    REQUIRE_FALSE(report.scenarios[1].builds.empty());
    CHECK(report.scenarios[0].builds.front().score == 4834);
    CHECK(report.scenarios[1].builds.front().score == 4875);
    const auto& filters = report.scenarios[0].slot_filters;
    // Comptages vérifiés directement dans l'export Qui-Gon (runes + unit_list[].runes).
    CHECK(filters[0].pool == 400);
    CHECK(filters[1].pool == 151);
    CHECK(filters[2].pool == 308);
    CHECK(filters[3].pool == 173);
    CHECK(filters[4].pool == 284);
    CHECK(filters[5].pool == 107);
    CHECK(filters[0].inventory + filters[0].target_equipped +
              filters[0].other_equipped ==
          filters[0].pool);
    CHECK(filters[0].inventory == 361);
    CHECK(filters[0].target_equipped == 1);
    CHECK(filters[0].other_equipped == 38);
    CHECK(filters[0].after_main_stat == 387);
    CHECK(filters[1].after_main_stat == 107);
    CHECK(filters[2].after_main_stat == 286);
    CHECK(filters[3].after_main_stat == 58);
    CHECK(filters[4].after_main_stat == 263);
    CHECK(filters[5].after_main_stat == 28);
    CHECK(report.scenarios[0].candidates_before == 159);
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

namespace {

double test_metric(const swcore::StatValues& s, const std::string& name) {
    if (name == "ATK" || name == "attack") return s.attack;
    if (name == "CDmg" || name == "crit_damage") return s.crit_damage;
    if (name == "CRate" || name == "crit_rate") return s.crit_rate;
    if (name == "SPD" || name == "speed") return s.speed;
    if (name == "ACC" || name == "accuracy") return s.accuracy;
    if (name == "HP" || name == "hp") return s.hp;
    if (name == "DEF" || name == "defense") return s.defense;
    if (name == "RES" || name == "resistance") return s.resistance;
    return 0;
}

// Recherche exhaustive de référence : toutes les combinaisons, stats calculées
// avec calculate_monster_stats (la fonction pure de l'application).
std::optional<double> brute_force_best(
    const swcore::AccountMonster& target,
    const std::array<std::vector<swcore::Rune>, 6>& by_slot,
    const swcore::SetEffects& effects, const swcore::SolverConfig& config,
    const swcore::SetScenario& scenario) {
    std::optional<double> best;
    std::array<std::size_t, 6> index{};
    while (true) {
        swcore::AccountMonster candidate = target;
        candidate.runes.clear();
        std::map<int, int> counts;
        for (std::size_t s = 0; s < 6; ++s) {
            auto rune = by_slot[s][index[s]];
            rune.occupancy = swcore::RuneOccupancy::Equipped;
            ++counts[rune.set_id];
            candidate.runes.push_back(rune);
        }
        bool sets_ok = true;
        for (const auto& requirement : scenario.sets)
            sets_ok &= counts[requirement.set_id] == requirement.pieces;
        if (sets_ok) {
            const auto total =
                swcore::calculate_monster_stats(candidate, effects).total;
            bool valid = true;
            for (const auto& [name, constraint] : config.constraints) {
                const double value = test_metric(total, name);
                if (constraint.minimum && value < *constraint.minimum) valid = false;
                if (constraint.maximum && value > *constraint.maximum) valid = false;
            }
            if (valid) {
                double score = 0;
                for (const auto& [name, weight] : config.objective.weights)
                    score += test_metric(total, name) * weight;
                if (!best || score > *best) best = score;
            }
        }
        std::size_t s = 0;
        while (s < 6 && ++index[s] == by_slot[s].size()) index[s++] = 0;
        if (s == 6) break;
    }
    return best;
}

}  // namespace

TEST_CASE("Solver pruning matches an exhaustive search on a small pool") {
    const std::string root{SW_PROJECT_SOURCE_DIR};
    const std::string export_path =
        root + "/tests/data/Qui-Gon-21196058-2026-11-02_001212.json";
    const std::string mapping_path = root + "/data/mapping";
    const auto ignore = [](const std::string&) {};

    auto config = swcore::SolverConfig::load(
        root + "/tests/data/solver_raoq_config.json", mapping_path, ignore);
    const auto rune_mapping = swcore::RuneMapping::load(mapping_path);
    const auto account = swcore::load_account(export_path, rune_mapping, ignore);
    const auto all_runes = swcore::load_runes(export_path, rune_mapping, ignore);
    const auto effects =
        swcore::SetEffects::load(mapping_path + "/set_effects.json", ignore);
    const auto target = std::find_if(
        account.begin(), account.end(), [&](const swcore::AccountMonster& m) {
            return m.unit_id == *config.pool.target_unit_id;
        });
    REQUIRE(target != account.end());
    REQUIRE_FALSE(config.scenarios.empty());
    const auto scenario = config.scenarios.front();
    REQUIRE(scenario.sets.size() == 2);

    // Petit pool : par slot, 4 runes du premier set et 3 du second, filtrées
    // comme le solveur (niveau, étoiles, stat principale).
    std::array<std::vector<swcore::Rune>, 6> by_slot;
    std::vector<swcore::Rune> pool;
    for (int slot = 1; slot <= 6; ++slot) {
        for (std::size_t t = 0; t < 2; ++t) {
            int taken = 0;
            for (const auto& rune : all_runes) {
                if (taken == (t == 0 ? 4 : 3)) break;
                if (rune.slot != slot ||
                    rune.set_id != scenario.sets[t].set_id ||
                    rune.upgrade_level < config.pool.minimum_rune_level ||
                    rune.stars < config.pool.minimum_stars)
                    continue;
                const auto& selection = config.main_stats[slot];
                if ((slot == 2 || slot == 4 || slot == 6) && !selection.all &&
                    !selection.stat_ids.empty() &&
                    (!rune.primary ||
                     std::find(selection.stat_ids.begin(),
                               selection.stat_ids.end(),
                               rune.primary->stat_id) == selection.stat_ids.end()))
                    continue;
                by_slot[static_cast<std::size_t>(slot - 1)].push_back(rune);
                pool.push_back(rune);
                ++taken;
            }
        }
        REQUIRE_FALSE(by_slot[static_cast<std::size_t>(slot - 1)].empty());
    }

    config.scenarios = {scenario};
    config.limits.time_budget_ms = 0;
    config.limits.top_n = 1;
    config.limits.max_builds_per_scenario = 1;

    SECTION("without constraints") {
        config.constraints.clear();
        const auto expected =
            brute_force_best(*target, by_slot, effects, config, scenario);
        const auto report =
            swcore::solve_runes(*target, pool, effects, config, mapping_path, ignore);
        REQUIRE(report.scenarios.size() == 1);
        const auto& result = report.scenarios.front();
        CHECK_FALSE(result.incomplete);
        CHECK(result.validation_error.empty());
        REQUIRE(expected.has_value());
        REQUIRE_FALSE(result.builds.empty());
        CHECK(result.builds.front().score == *expected);
    }
    SECTION("with the configured constraints") {
        const auto expected =
            brute_force_best(*target, by_slot, effects, config, scenario);
        const auto report =
            swcore::solve_runes(*target, pool, effects, config, mapping_path, ignore);
        REQUIRE(report.scenarios.size() == 1);
        const auto& result = report.scenarios.front();
        CHECK_FALSE(result.incomplete);
        CHECK(result.validation_error.empty());
        CHECK(result.builds.empty() == !expected.has_value());
        if (expected && !result.builds.empty())
            CHECK(result.builds.front().score == *expected);
    }
}

namespace {

struct RaoqFixture {
    std::string root{SW_PROJECT_SOURCE_DIR};
    std::string mapping_path = root + "/data/mapping";
    std::string export_path =
        root + "/tests/data/Qui-Gon-21196058-2026-11-02_001212.json";
    swcore::RuneMapping rune_mapping = swcore::RuneMapping::load(mapping_path);
    std::vector<swcore::AccountMonster> account =
        swcore::load_account(export_path, rune_mapping, [](const std::string&) {});
    std::vector<swcore::Rune> runes =
        swcore::load_runes(export_path, rune_mapping, [](const std::string&) {});
    swcore::SetEffects effects = swcore::SetEffects::load(
        mapping_path + "/set_effects.json", [](const std::string&) {});

    swcore::SolverConfig config(const std::string& file) const {
        return swcore::SolverConfig::load(root + "/tests/data/" + file, mapping_path,
                                          [](const std::string&) {});
    }
    const swcore::AccountMonster& target(const swcore::SolverConfig& config) const {
        const auto it = std::find_if(
            account.begin(), account.end(), [&](const swcore::AccountMonster& m) {
                return m.unit_id == *config.pool.target_unit_id;
            });
        REQUIRE(it != account.end());
        return *it;
    }
    swcore::SolverReport solve(const swcore::SolverConfig& config) const {
        return swcore::solve_runes(target(config), runes, effects, config,
                                   mapping_path, [](const std::string&) {});
    }
};

// Scores de tous les builds valides (sets exacts + contraintes), par force brute
// avec calculate_monster_stats.
std::vector<double> brute_force_scores(
    const swcore::AccountMonster& target,
    const std::array<std::vector<swcore::Rune>, 6>& by_slot,
    const swcore::SetEffects& effects, const swcore::SolverConfig& config,
    const swcore::SetScenario& scenario) {
    std::vector<double> scores;
    std::array<std::size_t, 6> index{};
    while (true) {
        swcore::AccountMonster candidate = target;
        candidate.runes.clear();
        std::map<int, int> counts;
        for (std::size_t s = 0; s < 6; ++s) {
            auto rune = by_slot[s][index[s]];
            rune.occupancy = swcore::RuneOccupancy::Equipped;
            ++counts[rune.set_id];
            candidate.runes.push_back(rune);
        }
        bool valid = true;
        for (const auto& requirement : scenario.sets)
            valid &= counts[requirement.set_id] == requirement.pieces;
        if (valid) {
            const auto total =
                swcore::calculate_monster_stats(candidate, effects).total;
            for (const auto& [name, constraint] : config.constraints) {
                const double value = test_metric(total, name);
                if (constraint.minimum && value < *constraint.minimum) valid = false;
                if (constraint.maximum && value > *constraint.maximum) valid = false;
            }
            if (valid) {
                double score = 0;
                for (const auto& [name, weight] : config.objective.weights)
                    score += test_metric(total, name) * weight;
                scores.push_back(score);
            }
        }
        std::size_t s = 0;
        while (s < 6 && ++index[s] == by_slot[s].size()) index[s++] = 0;
        if (s == 6) break;
    }
    return scores;
}

swcore::Rune make_leveled_rune(std::uint64_t id, int slot, int stat, int value,
                               int level) {
    auto rune = make_rune(id, slot, stat, value);
    rune.upgrade_level = level;
    return rune;
}

}  // namespace

TEST_CASE("Projection current keeps the reference ATK result") {
    const RaoqFixture fixture;
    auto config = fixture.config("solver_raoq_atk_test.json");
    config.pool.projection_level = swcore::kProjectionCurrent;
    const auto report = fixture.solve(config);
    REQUIRE(report.scenarios.size() == 1);
    REQUIRE_FALSE(report.scenarios[0].builds.empty());
    CHECK_FALSE(report.scenarios[0].incomplete);
    CHECK(report.scenarios[0].builds.front().score == 3084);
}

TEST_CASE("Projection +15 with minimum level 15 keeps the ATK score") {
    const RaoqFixture fixture;
    const auto config = fixture.config("solver_raoq_atk_test.json");
    REQUIRE(config.pool.projection_level == 15);
    REQUIRE(config.pool.minimum_rune_level == 15);
    const auto report = fixture.solve(config);
    REQUIRE(report.scenarios.size() == 1);
    REQUIRE_FALSE(report.scenarios[0].builds.empty());
    CHECK(report.scenarios[0].builds.front().score == 3084);
    CHECK(report.scenarios[0].builds.front().projection_level == 0);
}

TEST_CASE("Projection +15 with minimum level 12 is at least as good") {
    const RaoqFixture fixture;
    auto config = fixture.config("solver_raoq_atk_test.json");
    config.pool.minimum_rune_level = 12;
    const auto report = fixture.solve(config);
    REQUIRE(report.scenarios.size() == 1);
    const auto& scenario = report.scenarios[0];
    REQUIRE_FALSE(scenario.builds.empty());
    CHECK(scenario.validation_error.empty());
    CHECK(scenario.builds.front().score >= 3084);
    const auto& target = fixture.target(config);
    for (const auto& build : scenario.builds) {
        for (std::size_t s = 0; s < 6; ++s) {
            const auto& used = build.runes[s];
            const auto& real = build.actual_runes[s];
            REQUIRE(real.rune_id == used.rune_id);
            CHECK(real.upgrade_level >= 12);
            if (real.upgrade_level < 15 && used.upgrade_level == 15) {
                // Rune projetée : la carte garde la valeur réelle, le calcul +15.
                CHECK(build.projection_level == 15);
                REQUIRE(used.primary);
                REQUIRE(real.primary);
                CHECK(used.primary->value >= real.primary->value);
                const auto card = swcore::make_build_report(target, build);
                CHECK(card.runes[s].rune.upgrade_level == real.upgrade_level);
                CHECK(card.runes[s].rune.primary->value == real.primary->value);
            }
            // Substats, innée, meules : celles de la rune réelle.
            CHECK(used.secondary.size() == real.secondary.size());
        }
        // Les stats totales viennent des runes projetées (fonction pure).
        swcore::AccountMonster check = target;
        check.runes.assign(build.runes.begin(), build.runes.end());
        for (auto& rune : check.runes) rune.occupancy = swcore::RuneOccupancy::Equipped;
        const auto total = swcore::calculate_monster_stats(check, fixture.effects).total;
        CHECK(total.attack == build.stats.total.attack);
        CHECK(total.speed == build.stats.total.speed);
    }
}

TEST_CASE("A +12 rune is shown as +12 and computed with its +15 main stat") {
    swcore::AccountMonster target;
    target.con = 648;
    target.attack = 801;
    target.defense = 582;
    target.speed = 108;
    target.crit_rate = 15;
    target.crit_damage = 50;
    target.resistance = 15;

    // Slot 2 n'a qu'une rune +12 (ATQ% 51) ; la valeur +15 de l'ATQ% 6* (63)
    // est observée sur les runes des slots 4 et 6.
    const std::vector<swcore::Rune> pool{
        make_rune(1, 1, 3, 160), make_leveled_rune(2, 2, 4, 51, 12),
        make_rune(3, 3, 5, 160), make_rune(4, 4, 4, 63),
        make_rune(5, 5, 1, 2448), make_rune(6, 6, 4, 63)};

    swcore::SolverConfig config;
    config.scenarios.push_back({"Six", {{17, 6}}, false});
    config.objective.weights["attack"] = 1;
    config.limits.top_n = 1;
    const std::string mapping = std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping";

    config.pool.projection_level = swcore::kProjectionCurrent;
    const auto current = swcore::solve_runes(target, pool, {}, config, mapping);
    config.pool.projection_level = 15;
    const auto projected = swcore::solve_runes(target, pool, {}, config, mapping);

    REQUIRE(current.scenarios.at(0).builds.size() == 1);
    REQUIRE(projected.scenarios.at(0).builds.size() == 1);
    const auto& base = current.scenarios[0].builds[0];
    const auto& build = projected.scenarios[0].builds[0];
    // ATQ% (slots 2, 4, 6) : 51 + 63 + 63 = 177 en l'état -> ceil(801 * 1.77) = 1418 ;
    // 63 + 63 + 63 = 189 projeté -> ceil(801 * 1.89) = 1514. Slot 1 : ATQ fixe 160.
    CHECK(base.stats.total.attack == 801 + 160 + 1418);
    CHECK(build.stats.total.attack == 801 + 160 + 1514);
    CHECK(base.projection_level == 0);
    CHECK(build.projection_level == 15);
    CHECK(build.runes[1].primary->value == 63);
    CHECK(build.actual_runes[1].upgrade_level == 12);
    CHECK(build.actual_runes[1].primary->value == 51);
    const auto card = swcore::make_build_report(target, build);
    CHECK(card.projection_level == 15);
    CHECK(card.runes[1].rune.upgrade_level == 12);
    CHECK(card.runes[1].rune.primary->value == 51);
}

TEST_CASE("top_n = 0 returns exactly the valid builds of a brute force") {
    const RaoqFixture fixture;
    auto config = fixture.config("solver_raoq_atk_test.json");
    const auto scenario = config.scenarios.at(0);
    REQUIRE(scenario.sets.size() == 2);

    std::array<std::vector<swcore::Rune>, 6> by_slot;
    std::vector<swcore::Rune> pool;
    for (int slot = 1; slot <= 6; ++slot) {
        for (std::size_t t = 0; t < 2; ++t) {
            int taken = 0;
            for (const auto& rune : fixture.runes) {
                if (taken == (t == 0 ? 4 : 3)) break;
                if (rune.slot != slot || rune.set_id != scenario.sets[t].set_id ||
                    rune.upgrade_level < config.pool.minimum_rune_level ||
                    rune.stars < config.pool.minimum_stars)
                    continue;
                const auto& selection = config.main_stats[slot];
                if ((slot == 2 || slot == 4 || slot == 6) && !selection.all &&
                    !selection.stat_ids.empty() &&
                    (!rune.primary ||
                     std::find(selection.stat_ids.begin(), selection.stat_ids.end(),
                               rune.primary->stat_id) == selection.stat_ids.end()))
                    continue;
                by_slot[static_cast<std::size_t>(slot - 1)].push_back(rune);
                pool.push_back(rune);
                ++taken;
            }
        }
        REQUIRE_FALSE(by_slot[static_cast<std::size_t>(slot - 1)].empty());
    }

    config.limits.top_n = 0;
    config.limits.time_budget_ms = 0;
    config.limits.detailed_builds = 5;
    const auto& target = fixture.target(config);

    // Sans contraintes (beaucoup de builds), puis avec les contraintes de la config.
    for (const bool constrained : {false, true}) {
        auto run_config = config;
        if (!constrained) run_config.constraints.clear();
        auto expected = brute_force_scores(target, by_slot, fixture.effects,
                                           run_config, scenario);
        std::sort(expected.rbegin(), expected.rend());
        const auto report = swcore::solve_runes(target, pool, fixture.effects,
                                                run_config, fixture.mapping_path,
                                                [](const std::string&) {});
        REQUIRE(report.scenarios.size() == 1);
        const auto& result = report.scenarios[0];
        CHECK(result.all_builds_mode);
        CHECK_FALSE(result.truncated);
        CHECK_FALSE(result.incomplete);
        CHECK(result.validation_error.empty());
        CHECK(result.valid_builds == expected.size());
        REQUIRE(result.all_builds.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK(result.all_builds[i].score == expected[i]);
        CHECK(result.builds.size() == std::min<std::size_t>(5, expected.size()));
    }
}

namespace {

swcore::Rune grind_rune(std::uint64_t id, int slot, int primary, int primary_value,
                        const std::vector<std::array<int, 3>>& subs,
                        int level = 15) {
    auto rune = make_leveled_rune(id, slot, primary, primary_value, level);
    for (const auto& [stat, value, grind] : subs) {
        swcore::SecondaryStat line;
        line.value = {swcore::StatType::Unknown,
                      stat == 2 || stat == 4 || stat == 6
                          ? swcore::StatValueKind::Percent
                          : swcore::StatValueKind::Flat,
                      stat, value};
        line.grind = grind;
        rune.secondary.push_back(line);
    }
    return rune;
}

// Règles du test, appliquées indépendamment du solveur : ATQ% (4) rareté 5
// "max" -> max(g0, 10) ; VIT (8) rareté 4 valeur exacte 3. Runes >= +12.
std::vector<std::pair<swcore::Rune, int>> brute_variants(const swcore::Rune& rune) {
    std::vector<std::pair<std::size_t, int>> options;
    if (rune.upgrade_level >= 12) {
        for (std::size_t j = 0; j < rune.secondary.size(); ++j) {
            const auto& line = rune.secondary[j];
            int target = line.grind;
            if (line.value.stat_id == 4) target = std::max(line.grind, 10);
            else if (line.value.stat_id == 8) target = 3;
            if (target != line.grind) options.emplace_back(j, target);
        }
    }
    std::vector<std::pair<swcore::Rune, int>> variants;
    for (std::size_t mask = 0; mask < (std::size_t{1} << options.size()); ++mask) {
        auto copy = rune;
        int cost = 0;
        for (std::size_t bit = 0; bit < options.size(); ++bit)
            if (mask & (std::size_t{1} << bit)) {
                copy.secondary[options[bit].first].grind = options[bit].second;
                ++cost;
            }
        variants.emplace_back(copy, cost);
    }
    return variants;
}

struct BruteBest {
    std::optional<double> score;
    int cost = 0;
};

BruteBest brute_force_grinds(const swcore::AccountMonster& target,
                             const std::vector<swcore::Rune>& pool,
                             const swcore::SolverConfig& config) {
    std::array<std::vector<std::pair<swcore::Rune, int>>, 6> by_slot;
    for (const auto& rune : pool)
        for (auto& variant : brute_variants(rune))
            by_slot[static_cast<std::size_t>(rune.slot - 1)].push_back(variant);
    BruteBest best;
    std::array<std::size_t, 6> index{};
    while (true) {
        swcore::AccountMonster candidate = target;
        candidate.runes.clear();
        int cost = 0;
        for (std::size_t s = 0; s < 6; ++s) {
            auto rune = by_slot[s][index[s]].first;
            rune.occupancy = swcore::RuneOccupancy::Equipped;
            cost += by_slot[s][index[s]].second;
            candidate.runes.push_back(rune);
        }
        bool valid = !config.max_new_grinds || cost <= *config.max_new_grinds;
        const auto total = swcore::calculate_monster_stats(candidate, {}).total;
        for (const auto& [name, constraint] : config.constraints) {
            const double value = test_metric(total, name);
            if (constraint.minimum && value < *constraint.minimum) valid = false;
            if (constraint.maximum && value > *constraint.maximum) valid = false;
        }
        if (valid) {
            double score = 0;
            for (const auto& [name, weight] : config.objective.weights)
                score += test_metric(total, name) * weight;
            if (!best.score || score > *best.score ||
                (score == *best.score && cost < best.cost)) {
                best.score = score;
                best.cost = cost;
            }
        }
        std::size_t s = 0;
        while (s < 6 && ++index[s] == by_slot[s].size()) index[s++] = 0;
        if (s == 6) break;
    }
    return best;
}

}  // namespace

TEST_CASE("Simulated grindstones match a brute force over every grind subset") {
    swcore::AccountMonster target;
    target.con = 648;
    target.attack = 801;
    target.defense = 582;
    target.speed = 108;
    target.crit_rate = 15;
    target.crit_damage = 50;
    target.resistance = 15;

    // Substats {stat, valeur, meule actuelle}. r21 a déjà une meule VIT 4 : la
    // valeur exacte 3 la ferait baisser. r32 est +9 : jamais meulée.
    const std::vector<swcore::Rune> pool{
        grind_rune(11, 1, 3, 160, {{4, 5, 0}, {8, 6, 0}}),
        grind_rune(12, 1, 3, 160, {{4, 8, 0}, {9, 6, 0}}),
        grind_rune(21, 2, 4, 63, {{8, 5, 4}, {3, 20, 0}}),
        grind_rune(22, 2, 8, 42, {{4, 7, 2}}),
        grind_rune(31, 3, 5, 160, {{4, 6, 0}, {8, 4, 0}}),
        grind_rune(32, 3, 5, 160, {{4, 9, 0}, {8, 5, 0}}, 9),
        grind_rune(41, 4, 4, 63, {{8, 3, 0}}),
        grind_rune(42, 4, 10, 80, {{4, 5, 0}}),
        grind_rune(51, 5, 1, 2448, {{4, 7, 0}, {8, 5, 0}}),
        grind_rune(52, 5, 1, 2448, {{4, 4, 0}}),
        grind_rune(61, 6, 4, 63, {{8, 6, 0}}),
        grind_rune(62, 6, 4, 63, {{9, 5, 0}, {8, 2, 0}})};

    swcore::SolverConfig base;
    base.grindstones[4] = {swcore::GrindstoneMode::Max, 5, 0};
    base.grindstones[8] = {swcore::GrindstoneMode::Exact, 4, 3};
    base.objective.weights["attack"] = 1;
    base.objective.weights["speed"] = 3;
    base.limits.top_n = 1;
    base.limits.max_builds_per_scenario = 1;
    const std::string mapping = std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping";

    struct Case {
        const char* name;
        std::optional<int> budget;
        std::optional<double> max_speed;
    };
    for (const auto& test : {Case{"sans limite", std::nullopt, std::nullopt},
                             Case{"1 meule max", 1, std::nullopt},
                             Case{"0 meule", 0, std::nullopt},
                             Case{"VIT max 160", std::nullopt, 160.0}}) {
        for (const bool any_set : {false, true}) {
            INFO(test.name << (any_set ? " (any_set)" : " (sets fixes)"));
            auto config = base;
            config.max_new_grinds = test.budget;
            if (test.max_speed) config.constraints["speed"].maximum = *test.max_speed;
            config.scenarios = {any_set ? swcore::SetScenario{"Tous", {}, true}
                                        : swcore::SetScenario{"Six", {{17, 6}}, false}};
            const auto expected = brute_force_grinds(target, pool, config);
            const auto report = swcore::solve_runes(target, pool, {}, config, mapping);
            REQUIRE(report.scenarios.size() == 1);
            const auto& result = report.scenarios[0];
            CHECK(result.validation_error.empty());
            REQUIRE(result.builds.empty() == !expected.score.has_value());
            if (!expected.score) continue;
            const auto& build = result.builds.front();
            CHECK(build.score == *expected.score);
            CHECK(build.grind_cost == expected.cost);
            if (test.budget) CHECK(build.grind_cost <= *test.budget);
            for (const auto& rune : build.runes)
                if (rune.rune_id == 32) CHECK(rune.applied_grindstones.empty());
        }
    }
}

namespace {

// Substats {stat, valeur, meule, enchantée}.
swcore::Rune craft_rune(std::uint64_t id, int slot, int primary, int primary_value,
                        const std::vector<std::array<int, 4>>& subs, int innate = 0,
                        int level = 15) {
    auto rune = make_leveled_rune(id, slot, primary, primary_value, level);
    if (innate != 0)
        rune.innate = swcore::StatValue{swcore::StatType::Unknown,
                                        swcore::StatValueKind::Flat, innate, 4};
    for (const auto& [stat, value, grind, enchanted] : subs) {
        swcore::SecondaryStat line;
        line.value = {swcore::StatType::Unknown,
                      stat == 2 || stat == 4 || stat == 6
                          ? swcore::StatValueKind::Percent
                          : swcore::StatValueKind::Flat,
                      stat, value};
        line.grind = grind;
        line.enchanted = enchanted != 0;
        rune.secondary.push_back(line);
    }
    return rune;
}

struct CraftVariant {
    swcore::Rune rune;
    int grinds = 0;
    int gems = 0;
};

// Règles du test, réécrites indépendamment du solveur :
// gemmes : ATQ% (4) rareté 5 "max" (9-13) ; Tx crit. (9) valeur exacte 7.
// meules : ATQ% (4) rareté 5 "max" (5-10) ; VIT (8) valeur exacte 3.
// Une gemme par rune ; rune gemmée : seule sa ligne enchantée ; stat absente
// ailleurs (principale, innée, autres substats) ; meule de la ligne perdue.
std::vector<CraftVariant> brute_craft_variants(const swcore::Rune& rune,
                                               bool with_gems, bool with_grinds,
                                               bool regem = true) {
    std::vector<CraftVariant> states{{rune, 0, 0}};
    if (rune.upgrade_level < 12) return states;
    if (with_gems) {
        int enchanted = -1;
        for (std::size_t j = 0; j < rune.secondary.size(); ++j)
            if (rune.secondary[j].enchanted) enchanted = static_cast<int>(j);
        for (std::size_t i = 0; i < rune.secondary.size(); ++i) {
            if (enchanted >= 0 && !regem) break;  // rune gemmée laissée telle quelle
            if (enchanted >= 0 && static_cast<int>(i) != enchanted) continue;
            for (const int stat : {4, 9}) {
                if (rune.primary->stat_id == stat) continue;
                if (rune.innate && rune.innate->stat_id == stat) continue;
                bool elsewhere = false;
                for (std::size_t j = 0; j < rune.secondary.size(); ++j)
                    elsewhere |= j != i && rune.secondary[j].value.stat_id == stat;
                if (elsewhere) continue;
                const auto& line = rune.secondary[i];
                const bool same = line.value.stat_id == stat;
                int value = stat == 4 ? 13 : 7;
                if (stat == 4 && same) value = std::max(line.value.value, 13);
                if (same && value == line.value.value) continue;
                auto copy = rune;
                copy.secondary[i].value = {swcore::StatType::Unknown,
                                           stat == 4 ? swcore::StatValueKind::Percent
                                                     : swcore::StatValueKind::Flat,
                                           stat, value};
                copy.secondary[i].enchanted = true;
                copy.secondary[i].grind = 0;
                states.push_back({copy, 0, 1});
            }
        }
    }
    std::vector<CraftVariant> variants;
    for (const auto& state : states) {
        std::vector<std::pair<std::size_t, int>> options;
        if (with_grinds) {
            for (std::size_t j = 0; j < state.rune.secondary.size(); ++j) {
                const auto& line = state.rune.secondary[j];
                int target = line.grind;
                if (line.value.stat_id == 4) target = std::max(line.grind, 10);
                else if (line.value.stat_id == 8) target = 3;
                if (target != line.grind) options.emplace_back(j, target);
            }
        }
        for (std::size_t mask = 0; mask < (std::size_t{1} << options.size()); ++mask) {
            auto variant = state;
            for (std::size_t bit = 0; bit < options.size(); ++bit)
                if (mask & (std::size_t{1} << bit)) {
                    variant.rune.secondary[options[bit].first].grind =
                        options[bit].second;
                    ++variant.grinds;
                }
            variants.push_back(variant);
        }
    }
    return variants;
}

struct CraftBest {
    std::optional<double> score;
    int gems = 0;
    int grinds = 0;
};

CraftBest brute_force_craft(const swcore::AccountMonster& target,
                            const std::vector<swcore::Rune>& pool,
                            const swcore::SolverConfig& config) {
    std::array<std::vector<CraftVariant>, 6> by_slot;
    for (const auto& rune : pool)
        for (auto& variant : brute_craft_variants(rune, !config.gems.empty(),
                                                  !config.grindstones.empty(),
                                                  config.gem_regem_existing))
            by_slot[static_cast<std::size_t>(rune.slot - 1)].push_back(variant);
    CraftBest best;
    std::array<std::size_t, 6> index{};
    while (true) {
        swcore::AccountMonster candidate = target;
        candidate.runes.clear();
        int grinds = 0;
        int gems = 0;
        for (std::size_t s = 0; s < 6; ++s) {
            const auto& variant = by_slot[s][index[s]];
            candidate.runes.push_back(variant.rune);
            candidate.runes.back().occupancy = swcore::RuneOccupancy::Equipped;
            grinds += variant.grinds;
            gems += variant.gems;
        }
        bool valid = (!config.max_new_grinds || grinds <= *config.max_new_grinds) &&
                     (!config.max_new_gems || gems <= *config.max_new_gems);
        if (valid) {
            const auto total = swcore::calculate_monster_stats(candidate, {}).total;
            for (const auto& [name, constraint] : config.constraints) {
                const double value = test_metric(total, name);
                if (constraint.minimum && value < *constraint.minimum) valid = false;
                if (constraint.maximum && value > *constraint.maximum) valid = false;
            }
            if (valid) {
                double score = 0;
                for (const auto& [name, weight] : config.objective.weights)
                    score += test_metric(total, name) * weight;
                const bool better =
                    !best.score || score > *best.score ||
                    (score == *best.score &&
                     (gems < best.gems || (gems == best.gems && grinds < best.grinds)));
                if (better) best = {score, gems, grinds};
            }
        }
        std::size_t s = 0;
        while (s < 6 && ++index[s] == by_slot[s].size()) index[s++] = 0;
        if (s == 6) break;
    }
    return best;
}

}  // namespace

TEST_CASE("Simulated gems and grindstones match a brute force") {
    swcore::AccountMonster target;
    target.con = 648;
    target.attack = 801;
    target.defense = 582;
    target.speed = 108;
    target.crit_rate = 15;
    target.crit_damage = 50;
    target.resistance = 15;

    const std::vector<swcore::Rune> pool{
        // Gemmes possibles sur les deux lignes (ATQ% ou Tx crit.).
        craft_rune(1, 1, 3, 160, {{8, 5, 0, 0}, {11, 6, 0, 0}}),
        // Principale ATQ% : pas de gemme ATQ% ; Tx crit. sur la ligne Tx crit.
        // (même stat gardée) mais pas sur la ligne VIT (Tx crit. déjà présent).
        craft_rune(2, 2, 4, 63, {{9, 5, 0, 0}, {8, 4, 2, 0}}),
        // Déjà gemmée (ligne ATQ%) : seule cette ligne peut être regemmée.
        craft_rune(3, 3, 5, 160, {{4, 5, 0, 1}, {8, 3, 0, 0}}),
        // +9 : jamais modifiée.
        craft_rune(33, 3, 5, 160, {{4, 9, 0, 0}, {9, 6, 0, 0}}, 0, 9),
        // Innée Tx crit. : gemme Tx crit. interdite.
        craft_rune(4, 4, 10, 80, {{4, 6, 0, 0}}, 9),
        craft_rune(5, 5, 1, 2448, {{4, 7, 4, 0}, {9, 3, 0, 0}}),
        craft_rune(6, 6, 4, 63, {{9, 4, 0, 0}})};

    swcore::SolverConfig base;
    base.objective.weights["attack"] = 1;
    base.objective.weights["crit_rate"] = 20;
    base.objective.weights["speed"] = 5;
    base.limits.top_n = 1;
    base.limits.max_builds_per_scenario = 1;
    const std::string mapping = std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping";

    struct Case {
        const char* name;
        bool gems;
        bool grinds;
        std::optional<int> max_gems;
        std::optional<int> max_grinds;
        std::optional<double> max_crit;
        bool regem = true;
    };
    for (const auto& test : {Case{"meules seules", false, true, {}, {}, {}},
                             Case{"gemmes seules", true, false, {}, {}, {}},
                             Case{"les deux", true, true, {}, {}, {}},
                             Case{"plafonds 1 et 1", true, true, 1, 1, {}},
                             Case{"0 gemme", true, true, 0, {}, {}},
                             Case{"Tx crit. max 40", true, true, {}, {}, 40.0},
                             Case{"regem_existing false", true, true, {}, {}, {}, false},
                             Case{"regem_existing false, gemmes seules", true, false,
                                  {}, {}, {}, false}}) {
        for (const bool any_set : {false, true}) {
            INFO(test.name << (any_set ? " (any_set)" : " (sets fixes)"));
            auto config = base;
            if (test.grinds) {
                config.grindstones[4] = {swcore::GrindstoneMode::Max, 5, 0};
                config.grindstones[8] = {swcore::GrindstoneMode::Exact, 4, 3};
            }
            if (test.gems) {
                config.gems[4] = {swcore::GrindstoneMode::Max, 5, 0};
                config.gems[9] = {swcore::GrindstoneMode::Exact, 5, 7};
            }
            config.max_new_gems = test.max_gems;
            config.max_new_grinds = test.max_grinds;
            config.gem_regem_existing = test.regem;
            if (test.max_crit) config.constraints["crit_rate"].maximum = *test.max_crit;
            config.scenarios = {any_set ? swcore::SetScenario{"Tous", {}, true}
                                        : swcore::SetScenario{"Six", {{17, 6}}, false}};
            const auto expected = brute_force_craft(target, pool, config);
            const auto report = swcore::solve_runes(target, pool, {}, config, mapping);
            REQUIRE(report.scenarios.size() == 1);
            const auto& result = report.scenarios[0];
            CHECK(result.validation_error.empty());
            REQUIRE(result.builds.empty() == !expected.score.has_value());
            if (!expected.score) continue;
            const auto& build = result.builds.front();
            CHECK(build.score == *expected.score);
            CHECK(build.gem_cost == expected.gems);
            CHECK(build.grind_cost == expected.grinds);
            for (std::size_t s = 0; s < 6; ++s) {
                const auto& rune = build.runes[s];
                // Pas de doublon de stat ; au plus une ligne enchantée.
                std::vector<int> stats{rune.primary->stat_id};
                if (rune.innate) stats.push_back(rune.innate->stat_id);
                int enchanted = 0;
                for (const auto& line : rune.secondary) {
                    stats.push_back(line.value.stat_id);
                    enchanted += line.enchanted ? 1 : 0;
                }
                std::sort(stats.begin(), stats.end());
                CHECK(std::adjacent_find(stats.begin(), stats.end()) == stats.end());
                CHECK(enchanted <= 1);
                for (const auto& gem : rune.applied_gems)
                    if (gem.stat_id == 9) CHECK(gem.value == 7);  // mode exact
                if (rune.rune_id == 33) {
                    CHECK(rune.applied_gems.empty());
                    CHECK(rune.applied_grindstones.empty());
                }
                // Rune 3 déjà gemmée : pas de nouvelle gemme si regem_existing = false.
                if (rune.rune_id == 3 && !test.regem) CHECK(rune.applied_gems.empty());
                // La carte montre la rune réelle.
                CHECK(build.actual_runes[s].applied_gems.empty());
                CHECK(build.actual_runes[s].applied_grindstones.empty());
            }
        }
    }
}

TEST_CASE("Simulated grindstones on Raoq: cap 0 keeps 3084, cap 1 is not worse") {
    const RaoqFixture fixture;
    auto config = fixture.config("solver_raoq_atk_test.json");
    config.grindstones[4] = {swcore::GrindstoneMode::Max, 5, 0};
    config.limits.top_n = 1;
    config.max_new_grinds = 0;
    const auto none = fixture.solve(config);
    config.max_new_grinds = 1;
    const auto one = fixture.solve(config);
    REQUIRE_FALSE(none.scenarios.at(0).builds.empty());
    REQUIRE_FALSE(one.scenarios.at(0).builds.empty());
    CHECK(none.scenarios[0].builds.front().score == 3084);
    CHECK(one.scenarios[0].builds.front().score >= 3084);
    CHECK(one.scenarios[0].builds.front().grind_cost <= 1);
}

TEST_CASE("Builds with the same score are ordered deterministically") {
    const RaoqFixture fixture;
    auto config = fixture.config("solver_raoq_atk_test.json");
    config.limits.top_n = 0;
    config.limits.detailed_builds = 0;
    const auto first = fixture.solve(config);
    const auto second = fixture.solve(config);
    REQUIRE(first.scenarios.size() == 1);
    REQUIRE(second.scenarios.size() == 1);
    const auto& a = first.scenarios[0].all_builds;
    const auto& b = second.scenarios[0].all_builds;
    REQUIRE(a.size() == b.size());
    REQUIRE_FALSE(a.empty());
    bool same_order = true;
    bool ordered = true;
    for (std::size_t i = 0; i < a.size(); ++i) {
        same_order &= a[i].rune_ids == b[i].rune_ids;
        if (i == 0) continue;
        const auto& prev = a[i - 1];
        const auto& cur = a[i];
        if (prev.score != cur.score) ordered &= prev.score > cur.score;
        else if (prev.effective_hp != cur.effective_hp)
            ordered &= prev.effective_hp > cur.effective_hp;
        else if (prev.efficiency != cur.efficiency)
            ordered &= prev.efficiency > cur.efficiency;
    }
    CHECK(same_order);
    CHECK(ordered);
}
