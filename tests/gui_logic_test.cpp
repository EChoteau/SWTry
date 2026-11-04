// Logique du GUI sans ImGui : vue des stats d'un monstre (tableau et fiche),
// instantané des builds sauvegardés, coût du comptage des runes candidates.

#include <catch2/catch_test_macros.hpp>

#include "gui_logic.hpp"
#include "monster_view.hpp"
#include "snapshot.hpp"

#include "swcore/account.hpp"
#include "swcore/solver.hpp"
#include "swcore/solver_config.hpp"

#include <algorithm>
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

// --- Tableau de résultats : pagination et tri ---

namespace {

// Lignes synthétiques : score décroissant par paquets (beaucoup d'égalités),
// scénario alterné.
std::vector<swgui::ResultRow> synthetic_rows(std::size_t count) {
    std::vector<swgui::ResultRow> rows(count);
    for (std::size_t i = 0; i < count; ++i) {
        rows[i].score = static_cast<double>((count - i) / 3);
        rows[i].total.speed = static_cast<int>((i * 7919) % 300);
        rows[i].scenario = static_cast<std::uint16_t>(i % 2);
        rows[i].index = static_cast<std::uint32_t>(i);
    }
    return rows;
}

using Range = std::pair<std::size_t, std::size_t>;

}  // namespace

TEST_CASE("Pager: page count, clamp, page size and reset") {
    using swgui::Pager;
    CHECK(Pager::page_count(0, 25) == 1);  // 0 ligne : une page vide
    CHECK(Pager::page_count(1, 25) == 1);
    CHECK(Pager::page_count(25, 25) == 1);
    CHECK(Pager::page_count(26, 25) == 2);
    CHECK(Pager::page_count(1'000'000, 25) == 40'000);
    CHECK(Pager::page_count(1'000'000, 100) == 10'000);

    Pager pager;
    CHECK(pager.page_size == 25);  // défaut
    CHECK(pager.page == 1);

    // 0 ligne : la plage est vide, la page reste 1.
    pager.page = 7;
    pager.clamp(0);
    CHECK(pager.page == 1);
    CHECK(pager.range(0) == Range{0, 0});

    // 1 ligne.
    CHECK(pager.range(1) == Range{0, 1});

    // Clamp : aucune page vide possible.
    pager.page = 99;
    pager.clamp(60);
    CHECK(pager.page == 3);
    CHECK(pager.range(60) == Range{50, 60});
    pager.page = 0;
    pager.clamp(60);
    CHECK(pager.page == 1);
    // Même sans clamp explicite, la plage reste dans les lignes existantes.
    pager.page = 1000;
    CHECK(pager.range(60) == Range{50, 60});

    // Changer la taille de page ramène à la page 1.
    pager.page = 3;
    pager.set_page_size(10);
    CHECK(pager.page == 1);
    CHECK(pager.range(60) == Range{0, 10});
    pager.page = 6;
    CHECK(pager.range(60) == Range{50, 60});

    // 1 000 000 de lignes : dernière page pleine.
    pager.set_page_size(100);
    pager.page = 10'000;
    pager.clamp(1'000'000);
    CHECK(pager.range(1'000'000) == Range{999'900, 1'000'000});
    pager.reset();  // après un tri
    CHECK(pager.page == 1);
}

TEST_CASE("Pager: page buttons with ellipsis") {
    using V = std::vector<std::size_t>;
    CHECK(swgui::page_buttons(1, 1) == V{1});
    CHECK(swgui::page_buttons(3, 7) == V{1, 2, 3, 4, 5, 6, 7});
    CHECK(swgui::page_buttons(1, 40) == V{1, 2, 3, 4, 5, 0, 40});
    CHECK(swgui::page_buttons(20, 40) == V{1, 0, 18, 19, 20, 21, 22, 0, 40});
    CHECK(swgui::page_buttons(40, 40) == V{1, 0, 36, 37, 38, 39, 40});
    // La page courante est toujours présente.
    for (std::size_t page = 1; page <= 40; ++page) {
        const auto buttons = swgui::page_buttons(page, 40);
        CHECK(std::find(buttons.begin(), buttons.end(), page) != buttons.end());
    }
}

TEST_CASE("Result sort covers all rows and keeps the solver order on ties") {
    const std::vector<std::string> labels{"Fatal 4 + Lame 2"};
    for (const std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{1'000}}) {
        const auto rows = synthetic_rows(count);
        // Score décroissant : à score égal, ordre du solveur (index croissant).
        const auto order = swgui::sorted_result_order(rows, {{swgui::kColScore, false}}, labels);
        REQUIRE(order.size() == count);
        for (std::size_t i = 1; i < order.size(); ++i) {
            const auto& a = rows[order[i - 1]];
            const auto& b = rows[order[i]];
            CHECK(a.score >= b.score);
            if (a.score == b.score) CHECK(order[i - 1] < order[i]);
        }
        // Tri multi-colonnes : VIT croissante puis score décroissant.
        const auto multi = swgui::sorted_result_order(
            rows, {{swgui::kColSpeed, true}, {swgui::kColScore, false}}, labels);
        for (std::size_t i = 1; i < multi.size(); ++i) {
            const auto& a = rows[multi[i - 1]];
            const auto& b = rows[multi[i]];
            CHECK(a.total.speed <= b.total.speed);
            if (a.total.speed == b.total.speed) CHECK(a.score >= b.score);
        }
        // Sans clé : ordre du solveur.
        const auto none = swgui::sorted_result_order(rows, {}, labels);
        for (std::size_t i = 0; i < none.size(); ++i) CHECK(none[i] == i);
        // Filtre de scénario : sous-ensemble dans le même ordre.
        const auto only = swgui::filter_scenario(order, rows, 1);
        CHECK(only.size() == count / 2);
        for (const auto index : only) CHECK(rows[index].scenario == 1);
        CHECK(swgui::filter_scenario(order, rows, -1) == order);
    }
}

TEST_CASE("Result sort on 1 000 000 rows sorts every row, not only a page") {
    const std::vector<std::string> labels{"Fatal 4 + Lame 2"};
    const auto rows = synthetic_rows(1'000'000);
    const auto order = swgui::sorted_result_order(rows, {{swgui::kColSpeed, false}}, labels);
    REQUIRE(order.size() == rows.size());
    bool sorted = true;
    for (std::size_t i = 1; i < order.size() && sorted; ++i) {
        const auto& a = rows[order[i - 1]];
        const auto& b = rows[order[i]];
        sorted = a.total.speed > b.total.speed ||
                 (a.total.speed == b.total.speed && order[i - 1] < order[i]);
    }
    CHECK(sorted);
    // La dernière page contient bien les plus petites VIT.
    swgui::Pager pager;
    pager.page = swgui::Pager::page_count(order.size(), pager.page_size);
    const auto [first, last] = pager.range(order.size());
    CHECK(last - first == 25);
    CHECK(rows[order[last - 1]].total.speed == 0);
}

// --- Vue active : page du menu ou onglet ---

TEST_CASE("View state: menu pages and closable tabs") {
    enum { kData, kMonsters, kOptimizer };
    swgui::ViewState view{kData, 0};
    CHECK(view.page_active(kData));

    view.show_page(kOptimizer);
    CHECK(view.page_active(kOptimizer));
    CHECK_FALSE(view.page_active(kData));

    // Un onglet s'ouvre : la page n'est plus mise en évidence.
    view.show_tab(5);
    CHECK(view.tab_active(5));
    CHECK_FALSE(view.page_active(kOptimizer));

    // Clic sur le menu : la page s'affiche, l'onglet reste ouvert mais inactif.
    view.show_page(kMonsters);
    CHECK(view.page_active(kMonsters));
    CHECK_FALSE(view.tab_active(5));

    // Retour sur l'onglet, puis fermeture d'un AUTRE onglet : rien ne change.
    view.show_tab(5);
    view.tab_closed(7);
    CHECK(view.tab_active(5));

    // Fermer l'onglet actif revient à la dernière page affichée.
    view.tab_closed(5);
    CHECK(view.page_active(kMonsters));
    CHECK(view.tab == 0);

    // Fermer un onglet alors qu'une page est affichée : la page reste.
    view.show_tab(8);
    view.show_page(kData);
    view.tab_closed(8);
    CHECK(view.page_active(kData));
}

// --- Boutons de verrou de la fiche monstre ---

TEST_CASE("Monster sheet lock buttons: none, partial, all, empty monster") {
    const Data data;
    // Un monstre avec runes et artefacts, un sans rune, un sans artefact.
    const swcore::AccountMonster* full = nullptr;
    const swcore::AccountMonster* no_runes = nullptr;
    const swcore::AccountMonster* no_artifacts = nullptr;
    for (const auto& monster : data.monsters) {
        const auto view = swgui::monster_stats_view(monster);
        if (!full && view.runes == 6 && !monster.artifacts.empty()) full = &monster;
        if (!no_runes && view.runes == 0) no_runes = &monster;
        if (!no_artifacts && monster.artifacts.empty() && view.runes > 0) no_artifacts = &monster;
    }
    REQUIRE(full);
    REQUIRE(no_runes);
    REQUIRE(no_artifacts);
    using swcore::LockKind;
    using swcore::LockState;

    swcore::LockTable locks;
    auto runes = swgui::lock_scope_state(locks, *full, LockKind::Rune);
    auto artifacts = swgui::lock_scope_state(locks, *full, LockKind::Artifact);
    CHECK(runes.total == 6);
    CHECK(runes.locked == 0);
    CHECK_FALSE(runes.disabled());
    CHECK_FALSE(runes.all_locked());  // "Verrouiller les runes"
    CHECK(swgui::combined_lock_state(runes, artifacts) == LockState::None);

    // Runes seulement : partiel ; runes = "Déverrouiller", artefacts = "Verrouiller".
    swcore::lock_monster(locks, *full, "A", true, false);
    runes = swgui::lock_scope_state(locks, *full, LockKind::Rune);
    artifacts = swgui::lock_scope_state(locks, *full, LockKind::Artifact);
    CHECK(runes.all_locked());
    CHECK(runes.locked == 6);
    CHECK_FALSE(artifacts.all_locked());
    CHECK(swgui::combined_lock_state(runes, artifacts) == LockState::Partial);

    // Tout : verrouillé.
    swcore::lock_monster(locks, *full, "A", false, true);
    artifacts = swgui::lock_scope_state(locks, *full, LockKind::Artifact);
    CHECK(artifacts.all_locked());
    CHECK(artifacts.locked == static_cast<int>(full->artifacts.size()));
    CHECK(swgui::combined_lock_state(runes, artifacts) == LockState::Full);

    // Une rune réservée à un AUTRE monstre ne compte pas pour celui-ci : partiel,
    // et "Verrouiller les runes" verrouille le reste.
    const auto& rune = full->runes.front();
    locks.set({LockKind::Rune, rune.rune_id, full->unit_id + 1, "B", rune.set_id, rune.slot});
    runes = swgui::lock_scope_state(locks, *full, LockKind::Rune);
    CHECK(runes.locked == 5);
    CHECK_FALSE(runes.all_locked());
    CHECK(swgui::combined_lock_state(runes, artifacts) == LockState::Partial);
    swcore::lock_monster(locks, *full, "A", true, false);
    CHECK(swgui::lock_scope_state(locks, *full, LockKind::Rune).all_locked());

    // Déverrouiller les runes : plus aucune rune, artefacts intacts.
    swcore::unlock_monster(locks, *full, true, false);
    runes = swgui::lock_scope_state(locks, *full, LockKind::Rune);
    CHECK(runes.locked == 0);
    CHECK(swgui::lock_scope_state(locks, *full, LockKind::Artifact).all_locked());

    // Monstre sans rune : bouton runes grisé ; sans artefact : bouton artefacts grisé.
    const auto empty_runes = swgui::lock_scope_state(locks, *no_runes, LockKind::Rune);
    CHECK(empty_runes.disabled());
    CHECK_FALSE(empty_runes.all_locked());
    const auto empty_artifacts =
        swgui::lock_scope_state(locks, *no_artifacts, LockKind::Artifact);
    CHECK(empty_artifacts.disabled());
    CHECK_FALSE(empty_artifacts.all_locked());
    // Rien d'équipé du tout : état "Non verrouillé".
    CHECK(swgui::combined_lock_state(swgui::LockScopeState{}, swgui::LockScopeState{}) ==
          LockState::None);
}
