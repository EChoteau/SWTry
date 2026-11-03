// Logique du GUI sans ImGui : vue des stats d'un monstre (tableau et fiche),
// instantané des builds sauvegardés, coût du comptage des runes candidates.

#include <catch2/catch_test_macros.hpp>

#include "monster_view.hpp"
#include "snapshot.hpp"

#include "swcore/account.hpp"
#include "swcore/solver.hpp"
#include "swcore/solver_config.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace {

const std::string kRoot{SW_PROJECT_SOURCE_DIR};
const std::string kMapping = kRoot + "/data/mapping";
const std::string kExport = kRoot + "/tests/data/Qui-Gon-21196058-2026-11-02_001212.json";

struct Data {
    swcore::RuneMapping mapping = swcore::RuneMapping::load(kMapping);
    swcore::SetEffects effects = swcore::SetEffects::load(kMapping + "/set_effects.json");
    std::vector<swcore::AccountMonster> monsters = load();
    std::vector<swcore::Rune> runes = swcore::load_runes(kExport, mapping, {});

    std::vector<swcore::AccountMonster> load() const {
        auto result = swcore::load_account(kExport, mapping, {});
        // Comme le GUI (DataStore) : stats avec sets.
        for (auto& monster : result)
            monster.stats = swcore::calculate_monster_stats(monster, effects);
        return result;
    }
};

std::array<int, 8> values(const swcore::StatValues& s) {
    return {s.hp, s.attack, s.defense, s.speed, s.crit_rate, s.crit_damage,
            s.resistance, s.accuracy};
}

}  // namespace

TEST_CASE("Monster view: table row and sheet use the swcore stats function") {
    const Data data;
    REQUIRE_FALSE(data.monsters.empty());
    int without_runes = 0;
    int without_artifacts = 0;
    for (const auto& monster : data.monsters) {
        const auto view = swgui::monster_stats_view(monster);
        const auto expected = swcore::calculate_monster_stats(monster, data.effects);
        CHECK(view.current == values(expected.total));
        CHECK(view.base == values(expected.white));
        const auto derived = swcore::calculate_derived_metrics(expected.total);
        CHECK(view.current_metrics[0] == derived.expected_damage);
        CHECK(view.current_metrics[1] == derived.effective_hp);
        CHECK(view.current_metrics[2] == derived.effective_hp_broken_def);
        int slots = 0;
        for (const bool present : view.rune_slots) slots += present ? 1 : 0;
        CHECK(slots == view.runes);
        if (view.runes == 0) {
            ++without_runes;
            CHECK_FALSE(view.efficiency.has_value());
        }
        if (view.artifacts == 0) ++without_artifacts;
    }
    // Cas limites présents dans l'export de test (fiches avec emplacements vides).
    CHECK(without_runes > 0);
    CHECK(without_artifacts > 0);
}

TEST_CASE("Saved build snapshot: runes and monsters round trip exactly") {
    const Data data;
    for (const auto& rune : data.runes) {
        const auto back = swgui::rune_from_json(swgui::rune_to_json(rune));
        CHECK(swgui::same_rune(rune, back));
        CHECK(back.occupancy == rune.occupancy);
        CHECK(back.source_unit_id == rune.source_unit_id);
    }
    for (const auto& monster : data.monsters) {
        auto back = swgui::monster_from_json(swgui::monster_to_json(monster));
        back.stats = swcore::calculate_monster_stats(back, data.effects);
        CHECK(values(back.stats.total) == values(monster.stats.total));
        CHECK(back.artifacts.size() == monster.artifacts.size());
        CHECK(back.runes.size() == monster.runes.size());
    }
    // Une rune modifiée n'est plus "la même" (bandeau à la réouverture).
    REQUIRE_FALSE(data.runes.empty());
    auto changed = data.runes.front();
    changed.upgrade_level += 1;
    CHECK_FALSE(swgui::same_rune(data.runes.front(), changed));
}

TEST_CASE("Candidate counts with grindstones and gems enabled stay fast", "[timing]") {
    const Data data;
    const auto config = swcore::SolverConfig::load(
        kRoot + "/tests/data/solver_raoq_atk_craft.json", kMapping);
    const auto started = std::chrono::steady_clock::now();
    const auto counts = swcore::count_candidate_runes(data.runes, config);
    const auto elapsed = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    std::cout << "[timing] runes candidates par slot (meules + gemmes actives) : "
              << elapsed << " ms\n";
    std::size_t total = 0;
    for (const auto count : counts) total += count;
    CHECK(total > 0);
}
