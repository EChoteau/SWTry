#include <catch2/catch_test_macros.hpp>

#include "swcore/equip.hpp"
#include "swcore/locks.hpp"
#include "swcore/solver.hpp"
#include "swcore/solver_config.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kA = 100;
constexpr std::uint64_t kB = 200;
const std::string kMapping = std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping";

swcore::Rune rune(std::uint64_t id, int slot, int primary, int value, std::uint64_t owner) {
    swcore::Rune result;
    result.rune_id = id;
    result.slot = slot;
    result.stars = 6;
    result.upgrade_level = 15;
    result.set_id = 17;
    result.occupancy = owner ? swcore::RuneOccupancy::Equipped : swcore::RuneOccupancy::Inventory;
    result.source_unit_id = owner;
    result.occupied_id = owner;
    result.primary = swcore::StatValue{swcore::StatType::Unknown,
                                       primary == 2 || primary == 4 || primary == 6
                                           ? swcore::StatValueKind::Percent
                                           : swcore::StatValueKind::Flat,
                                       primary, value};
    return result;
}

swcore::AccountMonster monster(std::uint64_t unit_id) {
    swcore::AccountMonster result;
    result.unit_id = unit_id;
    result.con = 648;
    result.attack = 801;
    result.defense = 582;
    result.speed = 108;
    result.crit_rate = 15;
    result.crit_damage = 50;
    result.resistance = 15;
    return result;
}

// A porte 31..36, B porte 11..16 (plus fortes), inventaire 22.
swcore::AccountState account() {
    swcore::AccountState state;
    const int primaries[] = {3, 4, 5, 4, 1, 4};
    for (int slot = 1; slot <= 6; ++slot) {
        const auto i = static_cast<std::size_t>(slot - 1);
        state.runes.push_back(rune(30 + static_cast<std::uint64_t>(slot), slot, primaries[i],
                                   10, kA));
        state.runes.push_back(rune(10 + static_cast<std::uint64_t>(slot), slot, primaries[i],
                                   100, kB));
    }
    state.runes.push_back(rune(22, 2, 4, 50, 0));
    auto a = monster(kA);
    auto b = monster(kB);
    for (const auto& r : state.runes) {
        if (r.source_unit_id == kA) a.runes.push_back(r);
        if (r.source_unit_id == kB) b.runes.push_back(r);
    }
    swcore::Artifact artifact;
    artifact.rid = 501;
    artifact.slot = 1;
    artifact.occupied_id = kA;
    a.artifacts.push_back(artifact);
    a.stats = swcore::calculate_monster_stats(a, {});
    b.stats = swcore::calculate_monster_stats(b, {});
    state.monsters = {a, b};
    return state;
}

const swcore::AccountMonster& find(const swcore::AccountState& state, std::uint64_t id) {
    return *std::find_if(state.monsters.begin(), state.monsters.end(),
                         [&](const swcore::AccountMonster& m) { return m.unit_id == id; });
}

std::map<int, std::uint64_t> slots(const swcore::AccountMonster& m) {
    std::map<int, std::uint64_t> result;
    for (const auto& r : m.runes) result[r.slot] = r.rune_id;
    return result;
}

// Empreinte comparable d'un état (propriétaire de chaque rune, runes et
// artefacts de chaque monstre, artefacts déséquipés).
std::string signature(const swcore::AccountState& state) {
    std::string text;
    auto runes = state.runes;
    std::sort(runes.begin(), runes.end(),
              [](const auto& a, const auto& b) { return a.rune_id < b.rune_id; });
    for (const auto& r : runes)
        text += std::to_string(r.rune_id) + ":" + std::to_string(swcore::rune_owner(r)) + ";";
    for (const auto& m : state.monsters) {
        text += "|" + std::to_string(m.unit_id);
        for (const auto& [slot, id] : slots(m))
            text += " " + std::to_string(slot) + "=" + std::to_string(id);
        for (const auto& a : m.artifacts) text += " a" + std::to_string(a.rid);
        text += " hp" + std::to_string(m.stats.total.hp);
    }
    for (const auto& a : state.spare_artifacts) text += "|spare" + std::to_string(a.rid);
    return text;
}

swcore::SolverBuild build_with(const swcore::AccountState& state,
                               const std::map<int, std::uint64_t>& wanted) {
    swcore::SolverBuild build;
    for (std::size_t slot = 0; slot < 6; ++slot) {
        const auto it = wanted.find(static_cast<int>(slot + 1));
        const std::uint64_t id = it != wanted.end() ? it->second : 30 + slot + 1;
        for (const auto& r : state.runes)
            if (r.rune_id == id) build.actual_runes[slot] = build.runes[slot] = r;
    }
    return build;
}

}  // namespace

TEST_CASE("Equip: no action gives exactly the export") {
    const auto state = account();
    CHECK(signature(swcore::effective_state(state, {}, {}, {})) == signature(state));
}

TEST_CASE("Equip: runes move to the target, old runes freed, empty slot on the old owner") {
    const auto state = account();
    auto build = build_with(state, {{1, 11}, {2, 22}});
    // Meule simulée sur la rune du slot 1 : tâche à faire en jeu, pas dans les stats.
    build.runes[0].applied_grindstones.push_back({4, 10, 0, 5, 0});
    for (const auto scope : {swcore::EquipScope::Both, swcore::EquipScope::Runes}) {
        const auto action = swcore::make_equip_action(state, kA, build, scope);
        REQUIRE(action.moves.size() == 4);
        CHECK(action.moves[0] == swcore::EquipMove{swcore::LockKind::Rune, 31, kA, 0, 1});
        CHECK(action.moves[1] == swcore::EquipMove{swcore::LockKind::Rune, 11, kB, kA, 1});
        CHECK(action.moves[2] == swcore::EquipMove{swcore::LockKind::Rune, 32, kA, 0, 2});
        CHECK(action.moves[3] == swcore::EquipMove{swcore::LockKind::Rune, 22, 0, kA, 2});
        REQUIRE(action.craft.size() == 1);
        CHECK(action.craft[0].rune_id == 11);

        const auto after = swcore::effective_state(state, {action}, {}, {});
        const auto& a = find(after, kA);
        const auto& b = find(after, kB);
        CHECK(slots(a).at(1) == 11);
        CHECK(slots(a).at(2) == 22);
        CHECK_FALSE(slots(b).contains(1));  // slot vide chez l'ancien porteur
        for (const auto& r : after.runes)
            if (r.rune_id == 31 || r.rune_id == 32)
                CHECK(r.occupancy == swcore::RuneOccupancy::Inventory);
        // Stats de la cible = fonction de stats sur ses nouvelles runes RÉELLES.
        auto expected = monster(kA);
        expected.runes = a.runes;
        expected.artifacts = a.artifacts;
        CHECK(a.stats.total.attack == swcore::calculate_monster_stats(expected, {}).total.attack);
        CHECK(swcore::build_equipped(after, kA, build, scope));
    }
    // Artefacts seuls : le build ne change aucun artefact.
    CHECK(swcore::make_equip_action(state, kA, build, swcore::EquipScope::Artifacts)
              .moves.empty());
}

TEST_CASE("Equip: applying then undoing restores the previous state exactly") {
    const auto exported = account();
    std::vector<swcore::EquipAction> actions;
    std::vector<std::string> before;
    const std::vector<std::map<int, std::uint64_t>> wanted = {
        {{1, 11}, {2, 22}}, {{3, 13}, {1, 31}}, {{2, 12}, {4, 14}, {6, 16}}};
    auto current = exported;
    for (const auto& slots_wanted : wanted) {
        before.push_back(signature(current));
        actions.push_back(swcore::make_equip_action(current, kA,
                                                    build_with(current, slots_wanted),
                                                    swcore::EquipScope::Both));
        current = swcore::effective_state(exported, actions, {}, {});
    }
    // Annulations successives (pile) : chaque état précédent revient à l'identique.
    while (!actions.empty()) {
        actions.pop_back();
        CHECK(signature(swcore::effective_state(exported, actions, {}, {})) == before.back());
        before.pop_back();
    }
}

TEST_CASE("Equip: equip_actions.json round trip and reconciliation with a new export") {
    const auto state = account();
    swcore::EquipLog log;
    log.wizard_id = 21196058;
    auto build = build_with(state, {{1, 11}, {2, 22}});
    build.runes[1].applied_gems.push_back({0, 8, 5, 0, false, 4, 13, 5});
    log.actions.push_back(swcore::make_equip_action(state, kA, build, swcore::EquipScope::Both));
    log.actions.back().id = 1;
    log.actions.back().date = "2026-11-03 10:00";

    const auto back = swcore::EquipLog::parse(log.to_json());
    REQUIRE(back.actions.size() == 1);
    CHECK(back.wizard_id == log.wizard_id);
    CHECK(back.actions[0].moves == log.actions[0].moves);
    CHECK(back.actions[0].date == log.actions[0].date);
    REQUIRE(back.actions[0].craft.size() == 1);
    CHECK(back.actions[0].craft[0].gems.size() == 1);

    // Nouvel export : la rune 11 est déjà sur A (fait en jeu), le reste non.
    auto next = state;
    for (auto& r : next.runes)
        if (r.rune_id == 11) {
            r.source_unit_id = kA;
            r.occupied_id = kA;
        }
    auto same_account = log;
    const auto result = swcore::reconcile_equip_log(same_account, next, 21196058);
    CHECK_FALSE(result.dropped);
    CHECK(result.satisfied == 1);
    CHECK(result.remaining == 3);
    REQUIRE(same_account.actions.size() == 1);
    CHECK(same_account.actions[0].moves.size() == 3);

    auto other_account = log;
    const auto dropped = swcore::reconcile_equip_log(other_account, next, 999);
    CHECK(dropped.dropped);
    CHECK(other_account.actions.empty());
}

TEST_CASE("Equip and locks are independent") {
    const auto state = account();
    swcore::LockTable locks;
    locks.set({swcore::LockKind::Rune, 11, kB, "B", 17, 1});
    const auto before = locks;
    const auto build = build_with(state, {{1, 11}});
    const auto action = swcore::make_equip_action(state, kA, build, swcore::EquipScope::Both);
    const auto after = swcore::effective_state(state, {action}, {}, {});
    CHECK(locks == before);  // équiper ne touche pas aux verrous
    CHECK(slots(find(after, kA)).at(1) == 11);  // rune verrouillée pour B déplacée quand même

    // Verrouiller sans équiper ne change pas l'état effectif.
    const auto signature_before = signature(swcore::effective_state(state, {}, {}, {}));
    swcore::lock_build(locks, find(state, kA), build, "A", true, true);
    CHECK(signature(swcore::effective_state(state, {}, {}, {})) == signature_before);
}

TEST_CASE("Equip: the solver works on the effective state (pool, origins, inventory)") {
    const auto exported = account();
    const auto build = build_with(exported, {{1, 11}, {2, 22}});
    const auto action = swcore::make_equip_action(exported, kA, build, swcore::EquipScope::Both);
    const auto state = swcore::effective_state(exported, {action}, {}, {});

    // B cherche sur l'état effectif : 31 et 32 (libérées par A) sont en inventaire,
    // 11 et 22 sont maintenant sur A.
    swcore::SolverConfig config;
    config.scenarios = {{"Tous", {}, true}};
    config.objective.weights = {{"attack", 1.0}, {"hp", 0.1}};
    config.limits.top_n = 1;
    config.pool.inventory_only = false;
    const auto report = swcore::solve_runes(find(state, kB), state.runes, {}, config, kMapping);
    REQUIRE(report.scenarios.size() == 1);
    const auto& filters = report.scenarios[0].slot_filters;
    CHECK(filters[0].inventory == 1);  // 31
    CHECK(filters[1].inventory == 1);  // 32 (22 est maintenant sur A)
    CHECK(filters[0].other_equipped == 1);  // 11, maintenant sur A

    // Force brute sur toutes les runes de l'état effectif.
    std::array<std::vector<swcore::Rune>, 6> by_slot;
    for (const auto& r : state.runes)
            by_slot[static_cast<std::size_t>(r.slot - 1)].push_back(r);
    const bool complete = std::all_of(by_slot.begin(), by_slot.end(),
                                      [](const auto& runes) { return !runes.empty(); });
    if (!complete) {
        CHECK(report.scenarios[0].builds.empty());
        return;
    }
    std::optional<double> expected;
    std::array<std::size_t, 6> index{};
    while (true) {
        auto candidate = monster(kB);
        for (std::size_t s = 0; s < 6; ++s) {
            candidate.runes.push_back(by_slot[s][index[s]]);
            candidate.runes.back().occupancy = swcore::RuneOccupancy::Equipped;
        }
        const auto total = swcore::calculate_monster_stats(candidate, {}).total;
        const double score = total.attack + 0.1 * total.hp;
        if (!expected || score > *expected) expected = score;
        std::size_t s = 0;
        while (s < 6 && ++index[s] == by_slot[s].size()) index[s++] = 0;
        if (s == 6) break;
    }
    REQUIRE_FALSE(report.scenarios[0].builds.empty());
    CHECK(report.scenarios[0].builds.front().score == *expected);
}
