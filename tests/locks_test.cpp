#include <catch2/catch_test_macros.hpp>

#include "swcore/locks.hpp"
#include "swcore/solver.hpp"
#include "swcore/solver_config.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kA = 100;  // monstre cible
constexpr std::uint64_t kB = 200;  // autre monstre
const std::string kMapping = std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping";

swcore::Rune rune(std::uint64_t id, int slot, int primary, int value, std::uint64_t owner,
                  int set_id = 17, int level = 15) {
    swcore::Rune result;
    result.rune_id = id;
    result.slot = slot;
    result.stars = 6;
    result.upgrade_level = level;
    result.set_id = set_id;
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
    swcore::AccountMonster target;
    target.unit_id = unit_id;
    target.con = 648;
    target.attack = 801;
    target.defense = 582;
    target.speed = 108;
    target.crit_rate = 15;
    target.crit_damage = 50;
    target.resistance = 15;
    return target;
}

// Deux runes par slot : la "forte" (id 1x) équipée sur B, la "faible" (id 2x)
// en inventaire.
std::vector<swcore::Rune> pool() {
    const int primaries[] = {3, 4, 5, 4, 1, 4};
    const int strong[] = {160, 63, 160, 63, 2448, 63};
    const int weak[] = {100, 40, 100, 40, 1500, 40};
    std::vector<swcore::Rune> runes;
    for (int slot = 1; slot <= 6; ++slot) {
        const auto i = static_cast<std::size_t>(slot - 1);
        runes.push_back(rune(10 + static_cast<std::uint64_t>(slot), slot, primaries[i],
                             strong[i], kB));
        runes.push_back(rune(20 + static_cast<std::uint64_t>(slot), slot, primaries[i],
                             weak[i], 0));
    }
    return runes;
}

swcore::SolverConfig config(bool any_set = false) {
    swcore::SolverConfig result;
    result.scenarios = {any_set ? swcore::SetScenario{"Tous", {}, true}
                                : swcore::SetScenario{"Six", {{17, 6}}, false}};
    result.objective.weights = {{"attack", 1.0}, {"hp", 0.1}};
    result.limits.top_n = 1;
    return result;
}

swcore::Lock rune_lock(std::uint64_t id, std::uint64_t reserved_for) {
    return {swcore::LockKind::Rune, id, reserved_for, "", 17, 0};
}

std::optional<swcore::SolverBuild> best(const swcore::AccountMonster& target,
                                        const std::vector<swcore::Rune>& runes,
                                        const swcore::SolverConfig& solver_config) {
    const auto report = swcore::solve_runes(target, runes, {}, solver_config, kMapping);
    if (report.scenarios.empty() || report.scenarios[0].builds.empty()) return std::nullopt;
    return report.scenarios[0].builds.front();
}

bool uses(const swcore::SolverBuild& build, std::uint64_t id) {
    return std::any_of(build.runes.begin(), build.runes.end(),
                       [&](const swcore::Rune& r) { return r.rune_id == id; });
}

}  // namespace

TEST_CASE("Lock table: set, find, remove and locks.json round trip") {
    swcore::LockTable table;
    table.set({swcore::LockKind::Rune, 42, kA, "Raoq", 8, 2});
    table.set({swcore::LockKind::Artifact, 7, 0, "", 0, 1});
    table.set({swcore::LockKind::Rune, 42, kB, "Lapis", 8, 2});  // remplace
    REQUIRE(table.locks().size() == 2);
    REQUIRE(table.find(swcore::LockKind::Rune, 42) != nullptr);
    CHECK(table.find(swcore::LockKind::Rune, 42)->reserved_for == kB);
    CHECK(table.find(swcore::LockKind::Rune, 7) == nullptr);
    CHECK(swcore::LockTable::parse(table.to_json()) == table);

    const auto path = std::filesystem::temp_directory_path() / "swtry_locks_test.json";
    REQUIRE(table.save(path));
    REQUIRE(table.save(path));  // réécriture sur un fichier existant
    CHECK(swcore::LockTable::load(path) == table);
    std::filesystem::remove(path);
    CHECK(swcore::LockTable::load(path).empty());  // fichier absent : table vide

    CHECK(table.remove(swcore::LockKind::Rune, 42));
    CHECK_FALSE(table.remove(swcore::LockKind::Rune, 42));
}

TEST_CASE("Lock table: orphan locks removed, moved objects counted") {
    swcore::LockTable table;
    table.set(rune_lock(11, kB));   // toujours sur B
    table.set(rune_lock(12, kA));   // sur B maintenant : a changé de monstre
    table.set(rune_lock(999, kA));  // disparue
    auto owner = monster(kB);
    owner.artifacts.push_back({});
    owner.artifacts.back().rid = 5;
    table.set({swcore::LockKind::Artifact, 5, kB, "", 0, 1});
    table.set({swcore::LockKind::Artifact, 6, kB, "", 0, 2});  // disparu
    const auto result = table.reconcile(pool(), {owner});
    CHECK(result.orphans == 2);
    CHECK(result.moved == 1);
    CHECK(table.locks().size() == 3);
    CHECK(table.find(swcore::LockKind::Rune, 999) == nullptr);
}

TEST_CASE("Locks: reserved for B excluded for A, usable by B; reserved for nobody; ignore") {
    const auto runes = pool();
    for (const bool any_set : {false, true}) {
        INFO(any_set ? "any_set" : "sets fixes");
        auto solver_config = config(any_set);
        solver_config.lock_table.set(rune_lock(11, kB));
        // Sans verrou, les runes fortes (sur B) sont choisies par tout le monde.
        CHECK(uses(*best(monster(kA), runes, config(any_set)), 11));

        solver_config.pool.target_unit_id = kA;
        const auto for_a = best(monster(kA), runes, solver_config);
        REQUIRE(for_a);
        CHECK_FALSE(uses(*for_a, 11));
        CHECK(uses(*for_a, 21));
        CHECK(swcore::count_lock_excluded_runes(runes, solver_config) == 1);
        const auto report = swcore::solve_runes(monster(kA), runes, {}, solver_config, kMapping);
        CHECK(report.scenarios[0].lock_excluded == 1);
        CHECK(report.scenarios[0].slot_filters[0].locked_out == 1);

        const auto for_b = best(monster(kB), runes, solver_config);
        REQUIRE(for_b);
        CHECK(uses(*for_b, 11));

        auto nobody = solver_config;
        nobody.lock_table.set(rune_lock(11, 0));
        CHECK_FALSE(uses(*best(monster(kA), runes, nobody), 11));
        CHECK_FALSE(uses(*best(monster(kB), runes, nobody), 11));

        auto ignored = nobody;
        ignored.locks.mode = swcore::LockMode::Ignore;
        CHECK(uses(*best(monster(kA), runes, ignored), 11));
        CHECK(swcore::count_lock_excluded_runes(runes, ignored) == 0);
    }
}

TEST_CASE("Locks: keep_target_locked freezes slots and drops impossible scenarios") {
    auto runes = pool();
    // Rune faible +3 du slot 2 équipée sur A et verrouillée pour A.
    runes.push_back(rune(32, 2, 4, 10, kA, 17, 3));
    auto solver_config = config();
    solver_config.pool.minimum_rune_level = 12;
    solver_config.lock_table.set(rune_lock(32, kA));

    const auto free_build = best(monster(kA), runes, solver_config);
    REQUIRE(free_build);
    CHECK_FALSE(uses(*free_build, 32));  // non figée : trop faible et +3 < 12

    solver_config.locks.keep_target_locked = true;
    const auto frozen = best(monster(kA), runes, solver_config);
    REQUIRE(frozen);
    CHECK(frozen->runes[1].rune_id == 32);  // figée, malgré le niveau minimum
    const auto counts = swcore::count_candidate_runes(runes, [&] {
        auto counted = solver_config;
        counted.pool.target_unit_id = kA;
        return counted;
    }());
    CHECK(counts[1] == 1);

    // Rune figée d'un autre set : le scénario 6 x set 17 est écarté, proprement.
    auto other_set = runes;
    other_set.back().set_id = 5;
    std::vector<std::string> warnings;
    const auto report = swcore::solve_runes(
        monster(kA), other_set, {}, solver_config, kMapping,
        [&](const std::string& warning) { warnings.push_back(warning); });
    REQUIRE(report.scenarios.size() == 1);
    CHECK(report.scenarios[0].builds.empty());
    CHECK_FALSE(report.scenarios[0].skipped_reason.empty());
    CHECK(std::any_of(warnings.begin(), warnings.end(), [](const std::string& warning) {
        return warning.find("frozen locked runes") != std::string::npos;
    }));
    // Tous sets : toujours possible, avec la rune figée.
    const auto any = best(monster(kA), other_set, [&] {
        auto any_config = solver_config;
        any_config.scenarios = {{"Tous", {}, true}};
        return any_config;
    }());
    REQUIRE(any);
    CHECK(any->runes[1].rune_id == 32);
}

TEST_CASE("Locks: solver matches a brute force over the allowed runes") {
    auto runes = pool();
    runes.push_back(rune(33, 3, 5, 120, kA));  // sur A
    swcore::LockTable table;
    table.set(rune_lock(11, kB));   // réservée à B
    table.set(rune_lock(24, 0));    // réservée à personne
    table.set(rune_lock(33, kA));   // réservée à A
    table.set(rune_lock(15, kA));   // sur B mais réservée à A
    for (const bool keep : {false, true}) {
        INFO(keep ? "slots figés" : "verrous respectés");
        auto solver_config = config(true);
        solver_config.lock_table = table;
        solver_config.locks.keep_target_locked = keep;
        // Force brute : runes utilisables par A ; slots figés = rune réservée à A.
        std::array<std::vector<swcore::Rune>, 6> by_slot;
        for (const auto& candidate : runes)
            if (swcore::rune_usable_by(table, candidate, kA))
                by_slot[static_cast<std::size_t>(candidate.slot - 1)].push_back(candidate);
        if (keep)
            for (auto& slot_runes : by_slot) {
                const auto locked = std::find_if(
                    slot_runes.begin(), slot_runes.end(), [&](const swcore::Rune& r) {
                        return table.find(swcore::LockKind::Rune, r.rune_id) != nullptr;
                    });
                if (locked != slot_runes.end()) slot_runes = {*locked};
            }
        std::optional<double> expected;
        std::array<std::size_t, 6> index{};
        while (true) {
            auto candidate = monster(kA);
            candidate.runes.clear();
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
        const auto build = best(monster(kA), runes, solver_config);
        REQUIRE(build);
        CHECK(build->score == *expected);
        CHECK_FALSE(uses(*build, 11));
        CHECK_FALSE(uses(*build, 24));
        if (keep) {
            CHECK(build->runes[2].rune_id == 33);
            CHECK(build->runes[4].rune_id == 15);
        }
    }
}

TEST_CASE("Locks: CLI --locks file and in-memory table give the same result") {
    const auto runes = pool();
    swcore::LockTable table;
    table.set(rune_lock(11, kB));
    table.set(rune_lock(13, 0));
    const auto path = std::filesystem::temp_directory_path() / "swtry_cli_locks.json";
    REQUIRE(table.save(path));
    auto gui = config();
    gui.lock_table = table;              // GUI : table en mémoire (locks.json)
    auto cli = config();
    cli.lock_table = swcore::LockTable::load(path);  // CLI : --locks <fichier>
    std::filesystem::remove(path);
    const auto a = best(monster(kA), runes, gui);
    const auto b = best(monster(kA), runes, cli);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->score == b->score);
    for (std::size_t slot = 0; slot < 6; ++slot)
        CHECK(a->runes[slot].rune_id == b->runes[slot].rune_id);
}

TEST_CASE("Locks: lock_build reserves the six runes to the monster") {
    swcore::LockTable table;
    const auto runes = pool();
    const auto build = best(monster(kA), runes, config());
    REQUIRE(build);
    swcore::lock_build(table, monster(kA), *build, "A");
    CHECK(table.locks().size() == 6);
    for (const auto& r : build->runes) {
        REQUIRE(table.find(swcore::LockKind::Rune, r.rune_id) != nullptr);
        CHECK(table.find(swcore::LockKind::Rune, r.rune_id)->reserved_for == kA);
        CHECK(swcore::rune_usable_by(table, r, kA));
        CHECK_FALSE(swcore::rune_usable_by(table, r, kB));
    }
    swcore::unlock_build(table, monster(kA), *build);
    CHECK(table.empty());
}

TEST_CASE("Locks: lock_build frees the monster's previous reservation on the same slot") {
    swcore::LockTable table;
    table.set({swcore::LockKind::Rune, 21, kA, "A", 17, 1});  // ancienne rune du slot 1
    table.set({swcore::LockKind::Rune, 99, kB, "B", 17, 1});  // réservation d'un autre monstre
    const auto build = best(monster(kA), pool(), config());
    REQUIRE(build);
    REQUIRE(build->runes[0].rune_id == 11);
    const auto result = swcore::lock_build(table, monster(kA), *build, "A");
    REQUIRE(result.released.size() == 1);
    CHECK(result.released[0].id == 21);
    CHECK(table.find(swcore::LockKind::Rune, 21) == nullptr);
    CHECK(table.find(swcore::LockKind::Rune, 99) != nullptr);  // pas touchée
    CHECK(result.locked == 6);
}

TEST_CASE("Locks: several runes reserved for the target in one slot give one warning") {
    auto runes = pool();
    runes.push_back(rune(41, 1, 3, 120, 0));    // inventaire, réservée à A
    runes.push_back(rune(42, 1, 3, 110, kA));   // équipée sur A, réservée à A
    auto solver_config = config(true);
    solver_config.locks.keep_target_locked = true;
    solver_config.lock_table.set({swcore::LockKind::Rune, 41, kA, "A", 17, 1});
    solver_config.lock_table.set({swcore::LockKind::Rune, 42, kA, "A", 17, 1});
    std::vector<std::string> warnings;
    const auto report = swcore::solve_runes(
        monster(kA), runes, {}, solver_config, kMapping,
        [&](const std::string& warning) { warnings.push_back(warning); });
    REQUIRE_FALSE(report.scenarios.at(0).builds.empty());
    CHECK(report.scenarios[0].builds.front().runes[0].rune_id == 42);  // celle qu'il équipe
    const auto count = std::count_if(warnings.begin(), warnings.end(), [](const auto& w) {
        return w.find("slot 1 : 2 runes verrouillées pour A, figé : 42") != std::string::npos;
    });
    CHECK(count == 1);
}
