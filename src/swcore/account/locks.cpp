#include "swcore/account/locks.hpp"

#include "swcore/solver/solver.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace swcore {

namespace {

using json = nlohmann::json;
constexpr int kLocksVersion = 1;

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

bool key_less(const Lock& lock, LockKind kind, std::uint64_t id) {
    return lock.kind != kind ? lock.kind < kind : lock.id < id;
}

}  // namespace

LockTable LockTable::parse(const std::string& json_text, WarningSink warnings) {
    LockTable table;
    try {
        const json root = json::parse(json_text);
        for (const auto& item : root.value("locks", json::array())) {
            Lock lock;
            lock.kind = item.value("type", std::string{"rune"}) == "artifact"
                            ? LockKind::Artifact
                            : LockKind::Rune;
            lock.id = item.value("id", std::uint64_t{0});
            lock.reserved_for = item.value("reserved_for_unit_id", std::uint64_t{0});
            lock.monster_name = item.value("monster_name", std::string{});
            lock.set_id = item.value("set_id", 0);
            lock.slot = item.value("slot", 0);
            if (lock.id != 0) table.set(lock);
        }
    } catch (const std::exception& error) {
        warn(warnings, std::string("Locks: invalid JSON: ") + error.what());
        return {};
    }
    return table;
}

LockTable LockTable::load(const std::filesystem::path& path, WarningSink warnings) {
    std::ifstream input(path);
    if (!input) return {};
    std::stringstream buffer;
    buffer << input.rdbuf();
    return parse(buffer.str(), std::move(warnings));
}

std::string LockTable::to_json() const {
    json items = json::array();
    for (const auto& lock : locks_)
        items.push_back({{"type", lock.kind == LockKind::Artifact ? "artifact" : "rune"},
                         {"id", lock.id},
                         {"reserved_for_unit_id", lock.reserved_for},
                         {"monster_name", lock.monster_name},
                         {"set_id", lock.set_id},
                         {"slot", lock.slot}});
    return json{{"version", kLocksVersion}, {"locks", items}}.dump(2);
}

bool write_text_atomic(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << text << '\n';
        if (!output) return false;
    }
    std::filesystem::rename(temporary, path, error);  // remplace l'ancien fichier
    if (error) {
        // Certains environnements refusent de renommer sur un fichier existant.
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    return !error;
}

bool LockTable::save(const std::filesystem::path& path) const {
    return write_text_atomic(path, to_json());
}

const Lock* LockTable::find(LockKind kind, std::uint64_t id) const {
    const auto it = std::lower_bound(
        locks_.begin(), locks_.end(), std::pair{kind, id},
        [](const Lock& lock, const std::pair<LockKind, std::uint64_t>& key) {
            return key_less(lock, key.first, key.second);
        });
    return it != locks_.end() && it->kind == kind && it->id == id ? &*it : nullptr;
}

void LockTable::set(const Lock& lock) {
    const auto it = std::lower_bound(
        locks_.begin(), locks_.end(), lock,
        [](const Lock& a, const Lock& b) { return key_less(a, b.kind, b.id); });
    if (it != locks_.end() && it->kind == lock.kind && it->id == lock.id) *it = lock;
    else locks_.insert(it, lock);
}

bool LockTable::remove(LockKind kind, std::uint64_t id) {
    const auto* found = find(kind, id);
    if (!found) return false;
    locks_.erase(locks_.begin() + (found - locks_.data()));
    return true;
}

LockReconcile LockTable::reconcile(const std::vector<Rune>& runes,
                                   const std::vector<AccountMonster>& monsters) {
    std::unordered_map<std::uint64_t, std::uint64_t> rune_owners;
    for (const auto& rune : runes) rune_owners[rune.rune_id] = rune_owner(rune);
    std::unordered_map<std::uint64_t, std::uint64_t> artifact_owners;
    for (const auto& monster : monsters)
        for (const auto& artifact : monster.artifacts)
            artifact_owners[artifact.rid] = monster.unit_id;
    LockReconcile result;
    std::vector<Lock> kept;
    for (const auto& lock : locks_) {
        const auto& owners = lock.kind == LockKind::Rune ? rune_owners : artifact_owners;
        const auto it = owners.find(lock.id);
        if (it == owners.end()) {
            ++result.orphans;
            continue;
        }
        if (it->second != lock.reserved_for) ++result.moved;
        kept.push_back(lock);
    }
    locks_ = std::move(kept);
    return result;
}

std::uint64_t rune_owner(const Rune& rune) {
    return rune.occupancy == RuneOccupancy::Equipped ? rune.source_unit_id : 0;
}

bool rune_usable_by(const LockTable& locks, const Rune& rune, std::uint64_t unit_id) {
    const Lock* lock = locks.find(LockKind::Rune, rune.rune_id);
    return !lock || (lock->reserved_for != 0 && lock->reserved_for == unit_id);
}

bool artifact_usable_by(const LockTable& locks, const Artifact& artifact,
                        std::uint64_t unit_id) {
    const Lock* lock = locks.find(LockKind::Artifact, artifact.rid);
    return !lock || (lock->reserved_for != 0 && lock->reserved_for == unit_id);
}

LockState monster_lock_state(const LockTable& locks, const AccountMonster& monster) {
    int total = 0;
    int locked = 0;
    for (const auto& rune : monster.runes) {
        if (rune.occupancy != RuneOccupancy::Equipped) continue;
        ++total;
        locked += locks.find(LockKind::Rune, rune.rune_id) ? 1 : 0;
    }
    for (const auto& artifact : monster.artifacts) {
        ++total;
        locked += locks.find(LockKind::Artifact, artifact.rid) ? 1 : 0;
    }
    if (locked == 0) return LockState::None;
    return locked == total ? LockState::Full : LockState::Partial;
}

void lock_monster(LockTable& locks, const AccountMonster& monster, const std::string& name,
                  bool runes, bool artifacts) {
    if (runes)
        for (const auto& rune : monster.runes)
            if (rune.occupancy == RuneOccupancy::Equipped)
                locks.set({LockKind::Rune, rune.rune_id, monster.unit_id, name, rune.set_id,
                           rune.slot});
    if (artifacts)
        for (const auto& artifact : monster.artifacts)
            locks.set({LockKind::Artifact, artifact.rid, monster.unit_id, name, 0,
                       artifact.slot});
}

void unlock_monster(LockTable& locks, const AccountMonster& monster, bool runes,
                    bool artifacts) {
    if (runes)
        for (const auto& rune : monster.runes)
            if (rune.occupancy == RuneOccupancy::Equipped)
                locks.remove(LockKind::Rune, rune.rune_id);
    if (artifacts)
        for (const auto& artifact : monster.artifacts)
            locks.remove(LockKind::Artifact, artifact.rid);
}

void lock_rune_for_owner(LockTable& locks, const Rune& rune, const std::string& name) {
    locks.set({LockKind::Rune, rune.rune_id, rune_owner(rune), name, rune.set_id, rune.slot});
}

void lock_artifact_for_owner(LockTable& locks, const Artifact& artifact,
                             const std::string& name) {
    locks.set({LockKind::Artifact, artifact.rid, artifact.occupied_id, name, 0, artifact.slot});
}

LockBuildResult lock_build(LockTable& locks, const AccountMonster& monster,
                           const SolverBuild& build, const std::string& name,
                           bool runes, bool artifacts) {
    LockBuildResult result;
    // Une seule réservation par slot (et par type d'artefact) pour ce monstre :
    // on libère d'abord ses réservations existantes sur les mêmes emplacements.
    const auto release = [&](LockKind kind, int slot, std::uint64_t keep_id) {
        std::vector<Lock> doomed;
        for (const auto& lock : locks.locks())
            if (lock.kind == kind && lock.reserved_for == monster.unit_id &&
                lock.slot == slot && lock.id != keep_id)
                doomed.push_back(lock);
        for (const auto& lock : doomed) {
            locks.remove(lock.kind, lock.id);
            result.released.push_back(lock);
        }
    };
    if (runes)
        for (const auto& rune : build.runes) {
            if (rune.rune_id == 0) continue;
            release(LockKind::Rune, rune.slot, rune.rune_id);
            locks.set({LockKind::Rune, rune.rune_id, monster.unit_id, name, rune.set_id,
                       rune.slot});
            ++result.locked;
        }
    // Artefacts du build : ceux du monstre (le solveur ne les optimise pas encore).
    if (artifacts)
        for (const auto& artifact : monster.artifacts) {
            release(LockKind::Artifact, artifact.slot, artifact.rid);
            locks.set({LockKind::Artifact, artifact.rid, monster.unit_id, name, 0,
                       artifact.slot});
            ++result.locked;
        }
    return result;
}

void unlock_build(LockTable& locks, const AccountMonster& monster, const SolverBuild& build,
                  bool runes, bool artifacts) {
    if (runes)
        for (const auto& rune : build.runes) locks.remove(LockKind::Rune, rune.rune_id);
    if (artifacts)
        for (const auto& artifact : monster.artifacts)
            locks.remove(LockKind::Artifact, artifact.rid);
}

bool build_locked_for(const LockTable& locks, const AccountMonster& monster,
                      const SolverBuild& build, bool runes, bool artifacts) {
    const auto reserved = [&](LockKind kind, std::uint64_t id) {
        const Lock* lock = locks.find(kind, id);
        return lock && lock->reserved_for == monster.unit_id;
    };
    if (runes)
        for (const auto& rune : build.runes)
            if (rune.rune_id != 0 && !reserved(LockKind::Rune, rune.rune_id)) return false;
    if (artifacts)
        for (const auto& artifact : monster.artifacts)
            if (!reserved(LockKind::Artifact, artifact.rid)) return false;
    return true;
}

}  // namespace swcore
