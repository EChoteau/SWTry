// Vérifications des meules et gemmes simulées (phase 0).
// Les preuves "avant / après" sont imprimées : ./build/tests.exe "[craft-proof]"

#include <catch2/catch_test_macros.hpp>

#include "swcore/account/account.hpp"
#include "swcore/runes/craft_audit.hpp"
#include "swcore/solver/solver.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <utility>
#include <string>
#include <vector>

namespace {

const std::string kRoot{SW_PROJECT_SOURCE_DIR};
const std::string kMapping = kRoot + "/data/game";
const std::string kExport = kRoot + "/tests/data/exports/Qui-Gon-21196058-2026-11-02_001212.json";

void ignore(const std::string&) {}

// Rune réelle 48938444358 (Fatal slot 1, sur Raoq) :
// principale ATQ fixe 160, pas d'innée ;
// lignes : 0 Tx crit. 10 | 1 ATQ% 7 meule 5 gemmée | 2 VIT 16 meule 4 | 3 Dgts crit. 12
const swcore::Rune& real_rune() {
    static const auto runes =
        swcore::load_runes(kExport, swcore::RuneMapping::load(kMapping), ignore);
    static const auto it =
        std::find_if(runes.begin(), runes.end(), [](const swcore::Rune& rune) {
            return rune.rune_id == 48938444358ULL;
        });
    REQUIRE(it != runes.end());
    return *it;
}

std::vector<swcore::RuneCraftVariant> variants(const swcore::SolverConfig& config) {
    return swcore::list_craft_variants(real_rune(), config, kMapping);
}

std::string describe(const swcore::Rune& rune) {
    std::string text;
    for (std::size_t j = 0; j < rune.secondary.size(); ++j) {
        const auto& line = rune.secondary[j];
        text += (j ? " | " : "") + std::to_string(line.value.stat_id) + ":" +
                std::to_string(line.value.value) + "+" + std::to_string(line.grind) +
                (line.enchanted ? "(g)" : "");
    }
    return text;
}

void show(const char* title, const std::vector<swcore::RuneCraftVariant>& list) {
    std::cout << "\n[" << title << "]\n  avant : " << describe(real_rune()) << '\n';
    for (std::size_t i = 1; i < list.size(); ++i)
        std::cout << "  apres : " << describe(list[i].rune) << "  (meules "
                  << list[i].grinds << ", gemmes " << list[i].gems << ")\n";
    if (list.size() == 1) std::cout << "  apres : aucune variante (cout 0)\n";
}

swcore::SolverConfig grind(int stat, swcore::GrindstoneMode mode, int rarity,
                           int exact = 0) {
    swcore::SolverConfig config;
    config.grindstones[stat] = {mode, rarity, exact};
    return config;
}

swcore::SolverConfig gem(int stat, swcore::GrindstoneMode mode, int rarity,
                         int exact = 0) {
    swcore::SolverConfig config;
    config.gems[stat] = {mode, rarity, exact};
    return config;
}

}  // namespace

TEST_CASE("A1 grindstone roll modes on a real rune", "[craft-proof]") {
    using swcore::GrindstoneMode;
    REQUIRE(real_rune().secondary.at(2).value.stat_id == 8);
    REQUIRE(real_rune().secondary.at(2).grind == 4);

    // min (rareté 5 : 4-5) : max(4, 4) = 4 -> rien à faire, coût 0.
    const auto min = variants(grind(8, GrindstoneMode::Min, 5));
    show("A1 VIT min rarete 5", min);
    CHECK(min.size() == 1);

    // max (rareté 5) : max(4, 5) = 5.
    const auto max = variants(grind(8, GrindstoneMode::Max, 5));
    show("A1 VIT max rarete 5", max);
    REQUIRE(max.size() == 2);
    CHECK(max[1].rune.secondary[2].grind == 5);
    CHECK(max[1].grinds == 1);
    CHECK(max[1].rune.applied_grindstones.at(0).previous == 4);

    // exact 3 (rareté 4 : 3-4) : 3 même si < 4.
    const auto lower = variants(grind(8, GrindstoneMode::Exact, 4, 3));
    show("A1 VIT exact 3 rarete 4", lower);
    REQUIRE(lower.size() == 2);
    CHECK(lower[1].rune.secondary[2].grind == 3);

    // exact 4 == meule actuelle : coût 0, aucune variante.
    CHECK(variants(grind(8, GrindstoneMode::Exact, 4, 4)).size() == 1);
    // Hors plage : erreur de configuration (voir solver_grind_bad.json).
}

TEST_CASE("A2 grindstone rarity per stat, values from grindstone.json", "[craft-proof]") {
    using swcore::GrindstoneMode;
    const auto ranges = swcore::load_craft_ranges(kMapping + "/craft/grindstone.json");
    const auto hero = variants(grind(4, GrindstoneMode::Max, 4));
    const auto legend = variants(grind(4, GrindstoneMode::Max, 5));
    show("A2 ATQ% max rarete 4 (Hero)", hero);
    show("A2 ATQ% max rarete 5 (Legend)", legend);
    REQUIRE(hero.size() == 2);
    REQUIRE(legend.size() == 2);
    CHECK(hero[1].rune.secondary[1].grind == ranges.at(4).at(4).second);
    CHECK(legend[1].rune.secondary[1].grind == ranges.at(4).at(5).second);
    CHECK(hero[1].rune.secondary[1].grind != legend[1].rune.secondary[1].grind);
    // Rareté conservée pour l'affichage (carte : "meule Hero/Legend").
    CHECK(hero[1].rune.applied_grindstones.at(0).rarity == 4);
    CHECK(legend[1].rune.applied_grindstones.at(0).rarity == 5);
}

TEST_CASE("A3 only listed or grindable stats; never main, innate or 9-12",
          "[craft-proof]") {
    std::vector<std::string> warnings;
    const auto config = swcore::SolverConfig::load(
        kRoot + "/tests/data/solver_configs/solver_grind_default.json", kMapping,
        [&](const std::string& w) { warnings.push_back(w); });
    const auto all = variants(config);
    show("A3 default seul (toutes les meulables)", all);
    // Lignes meulables de la rune : ATQ% (1) et VIT (2). Meulage complet : la
    // rune telle quelle (diagnostic) puis UNE version où les deux sont meulées.
    REQUIRE(all.size() == 2);
    CHECK(all[1].grinds == 2);
    CHECK(all[1].rune.applied_grindstones.size() == 2);
    for (const auto& variant : all) {
        CHECK(variant.rune.primary->value == real_rune().primary->value);
        CHECK(variant.rune.secondary[0].grind == real_rune().secondary[0].grind);  // Tx crit.
        CHECK(variant.rune.secondary[3].grind == real_rune().secondary[3].grind);  // Dgts crit.
        CHECK(variant.rune.innate.has_value() == real_rune().innate.has_value());
    }
    // "stats" présent : seulement la VIT.
    const auto only_speed = variants(grind(8, swcore::GrindstoneMode::Max, 5));
    CHECK(only_speed.size() == 2);
    CHECK(only_speed[1].rune.secondary[1].grind == real_rune().secondary[1].grind);
}

TEST_CASE("B1 gem roll modes on the enchanted line of a real rune", "[craft-proof]") {
    using swcore::GrindstoneMode;
    REQUIRE(real_rune().secondary.at(1).enchanted);
    REQUIRE(real_rune().secondary.at(1).value.value == 7);

    const auto same_max = variants(gem(4, GrindstoneMode::Max, 5));  // 9-13
    const auto same_min = variants(gem(4, GrindstoneMode::Min, 5));
    const auto same_exact = variants(gem(4, GrindstoneMode::Exact, 5, 11));
    const auto other = variants(gem(12, GrindstoneMode::Max, 4));  // Préc. 6-9
    show("B1 ATQ% meme stat max rarete 5", same_max);
    show("B1 ATQ% meme stat min rarete 5", same_min);
    show("B1 ATQ% meme stat exact 11", same_exact);
    show("B1 autre stat Prec. max rarete 4", other);
    REQUIRE(same_max.size() == 2);
    CHECK(same_max[1].rune.secondary[1].value.value == 13);  // max(7, 13)
    REQUIRE(same_min.size() == 2);
    CHECK(same_min[1].rune.secondary[1].value.value == 9);   // max(7, 9)
    REQUIRE(same_exact.size() == 2);
    CHECK(same_exact[1].rune.secondary[1].value.value == 11);
    REQUIRE(other.size() == 2);
    CHECK(other[1].rune.secondary[1].value.stat_id == 12);
    CHECK(other[1].rune.secondary[1].value.value == 9);
}

TEST_CASE("B2 gem rarity per stat, values from enchanted_gem.json", "[craft-proof]") {
    using swcore::GrindstoneMode;
    const auto ranges = swcore::load_craft_ranges(kMapping + "/craft/enchanted_gem.json");
    const auto hero = variants(gem(4, GrindstoneMode::Max, 4));
    const auto legend = variants(gem(4, GrindstoneMode::Max, 5));
    REQUIRE(hero.size() == 2);
    REQUIRE(legend.size() == 2);
    CHECK(hero[1].rune.secondary[1].value.value == ranges.at(4).at(4).second);
    CHECK(legend[1].rune.secondary[1].value.value == ranges.at(4).at(5).second);
    CHECK(hero[1].rune.applied_gems.at(0).rarity == 4);
    CHECK(legend[1].rune.applied_gems.at(0).rarity == 5);
}

TEST_CASE("B3 gem rules: one gem, no duplicate stat, enchanted line only",
          "[craft-proof]") {
    using swcore::GrindstoneMode;
    // VIT (ligne 2), Tx crit. (ligne 0), ATQ fixe (principale) : interdites.
    CHECK(variants(gem(8, GrindstoneMode::Max, 5)).size() == 1);
    CHECK(variants(gem(9, GrindstoneMode::Max, 5)).size() == 1);
    CHECK(variants(gem(3, GrindstoneMode::Max, 5)).size() == 1);

    // Toutes les gemmes possibles : seule la ligne enchantée (1) est regemmée.
    swcore::SolverConfig every;
    for (const int stat : {1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12})
        every.gems[stat] = {GrindstoneMode::Max, 5, 0};
    const auto all = variants(every);
    show("B3 toutes les gemmes rarete 5", all);
    CHECK(all.size() > 1);
    for (const auto& variant : all) {
        CHECK(variant.rune.applied_gems.size() <= 1);
        for (const auto& applied : variant.rune.applied_gems) CHECK(applied.line == 1);
        int enchanted = 0;
        for (const auto& line : variant.rune.secondary) enchanted += line.enchanted;
        CHECK(enchanted == 1);
    }

    // Meule perdue sur la ligne gemmée (5 -> 0), puis meulée à nouveau
    // (meulage complet : la version gemmée est aussi meulée).
    swcore::SolverConfig both = gem(2, GrindstoneMode::Max, 5);
    both.grindstones[2] = {GrindstoneMode::Max, 5, 0};
    const auto gem_then_grind = variants(both);
    show("B3 gemme PV% puis meule PV%", gem_then_grind);
    bool lost = false;
    bool reground = false;
    for (const auto& variant : gem_then_grind) {
        if (variant.gems != 1) continue;
        CHECK(variant.grinds == 1);  // pas de version gemmée non meulée
        lost = variant.rune.applied_gems.at(0).old_grind == 5 &&
               variant.rune.applied_grindstones.at(0).previous == 0;
        reground = variant.rune.secondary[1].value.stat_id == 2 &&
                   variant.rune.secondary[1].grind == 10;
    }
    CHECK(lost);
    CHECK(reground);

    // regem_existing = false : la rune déjà gemmée ne reçoit rien.
    auto frozen = gem(4, GrindstoneMode::Max, 5);
    frozen.gem_regem_existing = false;
    CHECK(variants(frozen).size() == 1);
}

namespace {

struct Raoq {
    swcore::RuneMapping mapping = swcore::RuneMapping::load(kMapping);
    std::vector<swcore::AccountMonster> account =
        swcore::load_account(kExport, mapping, ignore);
    std::vector<swcore::Rune> runes = swcore::load_runes(kExport, mapping, ignore);
    swcore::SetEffects effects =
        swcore::SetEffects::load(kMapping + "/runes/set_effects.json", ignore);
    swcore::SolverConfig config(const std::string& file) const {
        return swcore::SolverConfig::load(kRoot + "/tests/data/solver_configs/" + file, kMapping,
                                          ignore);
    }
    swcore::SolverReport solve(const swcore::SolverConfig& config) const {
        const auto it = std::find_if(
            account.begin(), account.end(), [&](const swcore::AccountMonster& m) {
                return m.unit_id == *config.pool.target_unit_id;
            });
        REQUIRE(it != account.end());
        return swcore::solve_runes(*it, runes, effects, config, kMapping, ignore);
    }
    const swcore::AccountMonster& target(const swcore::SolverConfig& config) const {
        return *std::find_if(account.begin(), account.end(),
                             [&](const swcore::AccountMonster& m) {
                                 return m.unit_id == *config.pool.target_unit_id;
                             });
    }
};

}  // namespace

TEST_CASE("C2 a speed minimum reachable only with craft on Raoq", "[craft-proof]") {
    const Raoq raoq;
    auto config = raoq.config("solver_raoq_atk_test.json");
    config.constraints.clear();
    config.objective.weights = {{"speed", 1.0}};
    config.limits.top_n = 1;

    // Vitesse maximale exacte sans craft.
    const auto raw = raoq.solve(config);
    REQUIRE_FALSE(raw.scenarios.at(0).builds.empty());
    const int raw_speed = raw.scenarios[0].builds.front().stats.total.speed;

    // Avec meules et gemmes VIT (Legend, max).
    auto crafted = config;
    crafted.grindstones[8] = {swcore::GrindstoneMode::Max, 5, 0};
    crafted.gems[8] = {swcore::GrindstoneMode::Max, 5, 0};
    const auto fast = raoq.solve(crafted);
    REQUIRE_FALSE(fast.scenarios.at(0).builds.empty());
    const int crafted_speed = fast.scenarios[0].builds.front().stats.total.speed;
    std::cout << "\n[C2] VIT max sans craft : " << raw_speed
              << " ; avec craft : " << crafted_speed << '\n';
    REQUIRE(crafted_speed > raw_speed);

    // Objectif ATQ, VIT >= vitesse max sans craft + 1.
    config.objective.weights = {{"attack", 1.0}};
    config.constraints["speed"].minimum = raw_speed + 1;
    crafted.objective.weights = {{"attack", 1.0}};
    crafted.constraints["speed"].minimum = raw_speed + 1;
    const auto without = raoq.solve(config);
    const auto with = raoq.solve(crafted);
    CHECK(without.scenarios.at(0).builds.empty());
    REQUIRE_FALSE(with.scenarios.at(0).builds.empty());
    const auto& build = with.scenarios[0].builds.front();
    CHECK(build.stats.total.speed >= raw_speed + 1);
    CHECK(build.grind_cost + build.gem_cost > 0);
    // Les mêmes runes, sans le craft simulé, ne tiennent pas la contrainte.
    swcore::AccountMonster real = raoq.target(config);
    real.runes.assign(build.actual_runes.begin(), build.actual_runes.end());
    for (auto& rune : real.runes) rune.occupancy = swcore::RuneOccupancy::Equipped;
    const int real_speed = swcore::calculate_monster_stats(real, raoq.effects).total.speed;
    std::cout << "[C2] build trouve : VIT " << build.stats.total.speed
              << " avec craft, " << real_speed << " sans (meules "
              << build.grind_cost << ", gemmes " << build.gem_cost << ")\n";
    CHECK(real_speed <= raw_speed);
}

namespace {

swcore::Rune rune_with(std::uint64_t id, int slot, int primary, int primary_value,
                       std::vector<std::array<int, 3>> subs) {
    swcore::Rune rune;
    rune.rune_id = id;
    rune.slot = slot;
    rune.stars = 6;
    rune.upgrade_level = 15;
    rune.set_id = 17;
    rune.occupancy = swcore::RuneOccupancy::Inventory;
    const auto kind = [](int stat) {
        return stat == 2 || stat == 4 || stat == 6 ? swcore::StatValueKind::Percent
                                                   : swcore::StatValueKind::Flat;
    };
    rune.primary = swcore::StatValue{swcore::StatType::Unknown, kind(primary), primary,
                                     primary_value};
    for (const auto& [stat, value, grind] : subs) {
        swcore::SecondaryStat line;
        line.value = {swcore::StatType::Unknown, kind(stat), stat, value};
        line.grind = grind;
        rune.secondary.push_back(line);
    }
    return rune;
}

swcore::AccountMonster small_target() {
    swcore::AccountMonster target;
    target.con = 648;
    target.attack = 801;
    target.defense = 582;
    target.speed = 108;
    target.crit_rate = 15;
    target.crit_damage = 50;
    target.resistance = 15;
    return target;
}

}  // namespace

TEST_CASE("C3 the crafted optimum uses a rune that is not the raw best",
          "[craft-proof]") {
    // Slot 1 : A = ATQ% 9 déjà meulé à 10 (19 % brut, rien à meuler) ;
    //          B = ATQ% 8 + ATQ 30 (8 % brut, 18 % meulé Legend max).
    const std::vector<swcore::Rune> pool{
        rune_with(101, 1, 3, 160, {{4, 9, 10}}),
        rune_with(102, 1, 3, 160, {{4, 8, 0}, {3, 30, 0}}),
        rune_with(2, 2, 4, 63, {}), rune_with(3, 3, 5, 160, {}),
        rune_with(4, 4, 4, 63, {}), rune_with(5, 5, 1, 2448, {}),
        rune_with(6, 6, 4, 63, {})};
    swcore::SolverConfig config;
    config.scenarios = {{"Six", {{17, 6}}, false}};
    config.objective.weights["attack"] = 1;
    config.limits.top_n = 1;
    const auto target = small_target();

    const auto raw = swcore::solve_runes(target, pool, {}, config, kMapping);
    config.grindstones[4] = {swcore::GrindstoneMode::Max, 5, 0};
    const auto crafted = swcore::solve_runes(target, pool, {}, config, kMapping);
    REQUIRE_FALSE(raw.scenarios.at(0).builds.empty());
    REQUIRE_FALSE(crafted.scenarios.at(0).builds.empty());
    CHECK(raw.scenarios[0].builds.front().runes[0].rune_id == 101);
    CHECK(crafted.scenarios[0].builds.front().runes[0].rune_id == 102);

    // Force brute : toutes les combinaisons (A, B brute, B meulée) sur le slot 1.
    double best = 0;
    for (const auto& [rune, grind] : {std::pair{pool[0], 10}, std::pair{pool[1], 0},
                                      std::pair{pool[1], 10}}) {
        auto candidate = target;
        auto chosen = rune;
        chosen.secondary[0].grind = grind;
        candidate.runes = {chosen, pool[2], pool[3], pool[4], pool[5], pool[6]};
        for (auto& r : candidate.runes) r.occupancy = swcore::RuneOccupancy::Equipped;
        best = std::max(best, static_cast<double>(
                                  swcore::calculate_monster_stats(candidate, {})
                                      .total.attack));
    }
    CHECK(crafted.scenarios[0].builds.front().score == best);
}

TEST_CASE("C4 a set scenario valid only with craft is not dropped upstream",
          "[craft-proof]") {
    // VIT brute max : 108 + 6 + 6 = 120 ; avec meules VIT Legend max : jusqu'à 130.
    const std::vector<swcore::Rune> pool{
        rune_with(1, 1, 3, 160, {{8, 6, 0}}), rune_with(2, 2, 4, 63, {{8, 6, 0}}),
        rune_with(3, 3, 5, 160, {}), rune_with(4, 4, 4, 63, {}),
        rune_with(5, 5, 1, 2448, {}), rune_with(6, 6, 4, 63, {})};
    swcore::SolverConfig config;
    config.scenarios = {{"Six", {{17, 6}}, false}};
    config.objective.weights["attack"] = 1;
    config.constraints["speed"].minimum = 125;
    config.limits.top_n = 1;
    const auto target = small_target();

    const auto raw = swcore::solve_runes(target, pool, {}, config, kMapping);
    REQUIRE(raw.scenarios.size() == 1);
    CHECK(raw.scenarios[0].builds.empty());

    config.grindstones[8] = {swcore::GrindstoneMode::Max, 5, 0};
    const auto crafted = swcore::solve_runes(target, pool, {}, config, kMapping);
    REQUIRE(crafted.scenarios.size() == 1);
    CHECK(crafted.scenarios[0].nodes > 0);
    REQUIRE_FALSE(crafted.scenarios[0].builds.empty());
    // Meulage complet : les deux lignes VIT sont meulées (108 + 11 + 11 = 130).
    CHECK(crafted.scenarios[0].builds.front().stats.total.speed == 130);
    CHECK(crafted.scenarios[0].builds.front().grind_cost == 2);
    // Avec un plafond d'une meule : le choix des meules revient (125, 1 meule).
    config.max_new_grinds = 1;
    const auto capped = swcore::solve_runes(target, pool, {}, config, kMapping);
    REQUIRE_FALSE(capped.scenarios.at(0).builds.empty());
    CHECK(capped.scenarios[0].builds.front().stats.total.speed == 125);
    CHECK(capped.scenarios[0].builds.front().grind_cost == 1);
    config.max_new_grinds.reset();
    // Borne max de la contrainte : calculée avec les variantes meulées.
    CHECK(crafted.scenarios[0].constraint_upper_bounds.at("speed") >= 130);
    CHECK(raw.scenarios[0].constraint_upper_bounds.at("speed") == 120);
}

TEST_CASE("Raoq ATK config with full Legend/max craft keeps 3740", "[craft-proof]") {
    const Raoq raoq;
    const auto report = raoq.solve(raoq.config("solver_raoq_atk_craft.json"));
    REQUIRE_FALSE(report.scenarios.at(0).builds.empty());
    CHECK(report.scenarios[0].builds.front().score == 3740);
}
