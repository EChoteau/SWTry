#include "data_store.hpp"

#include "text_util.hpp"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace swgui {
namespace fs = std::filesystem;

namespace {

fs::path executable_directory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) return fs::path(buffer).parent_path();
#endif
    return fs::current_path();
}

// Une erreur bloquante parmi les messages de lecture (fichier absent, JSON).
std::string blocking_error(const std::vector<std::string>& messages) {
    for (const auto& message : messages)
        if (message.find("unable to open") != std::string::npos ||
            message.find("invalid JSON") != std::string::npos)
            return message;
    return {};
}

}  // namespace

fs::path find_data_root() {
    const fs::path exe = executable_directory();
    for (const auto& candidate :
         {fs::current_path(), exe, exe.parent_path(), exe.parent_path().parent_path()}) {
        std::error_code error;
        if (fs::is_directory(candidate / "data" / "mapping", error)) return candidate;
    }
    return fs::current_path();
}

DataStore::DataStore(fs::path root) : root_(std::move(root)) {
    const auto sink = [this](const std::string& message) { warnings.push_back(message); };
    mapping = swcore::RuneMapping::load(mapping_directory(), sink);
    effects = swcore::SetEffects::load(mapping_directory() / "set_effects.json", sink);
    reference = swcore::ReferenceDB::load(root_ / "data" / "swarfarm" / "monsters.json",
                                          sink);
    refresh_swarfarm();
    locks = swcore::LockTable::load(user_directory() / "locks.json", sink);
    equip_log = swcore::EquipLog::load(user_directory() / "equip_actions.json", sink);
}

bool DataStore::load_export(const fs::path& path) {
    std::vector<std::string> messages;
    const auto sink = [&](const std::string& message) { messages.push_back(message); };
    LoadedFile file;
    file.path = path;
    file.modified = file_date(path);
    auto loaded_monsters = swcore::load_account(path, mapping, sink);
    auto loaded_runes = swcore::load_runes(path, mapping, sink);
    file.error = blocking_error(messages);
    if (file.error.empty() && loaded_monsters.empty())
        file.error = "Aucun monstre dans ce fichier (est-ce bien un export SWEX ?)";
    warnings.insert(warnings.end(), messages.begin(), messages.end());
    if (!file.error.empty()) {
        export_file = std::move(file);
        return false;
    }

    // Comme swcli : stats avec sets, et origine lisible des runes équipées.
    exported = swcore::AccountState{std::move(loaded_monsters), std::move(loaded_runes), {}};
    for (auto& monster : exported.monsters)
        monster.stats = swcore::calculate_monster_stats(monster, effects);
    swcore::name_rune_sources(exported, reference);

    // Équipement virtuel : autre compte = abandon ; déplacements déjà faits en
    // jeu retirés ; un seul avertissement groupé, jamais bloquant.
    const auto equip = swcore::reconcile_equip_log(equip_log, exported,
                                                   swcore::read_wizard_id(path));
    if (equip.dropped)
        warnings.push_back("Équipement virtuel abandonné : l'export est celui d'un autre compte");
    else if (equip.satisfied > 0 || equip.remaining > 0)
        warnings.push_back(std::to_string(equip.satisfied) +
                           " changements virtuels satisfaits, " +
                           std::to_string(equip.remaining) + " restent à faire en jeu");
    if (equip.dropped || equip.satisfied > 0) save_equip_log();
    recompute_effective_state();

    // Verrous : objets disparus retirés, changements de monstre signalés (un
    // seul avertissement groupé ; le chargement n'est jamais bloqué).
    const auto reconcile = locks.reconcile(exported.runes, exported.monsters);
    if (reconcile.orphans > 0 || reconcile.moved > 0) {
        warnings.push_back(std::to_string(reconcile.orphans) +
                           " verrous orphelins retirés, " +
                           std::to_string(reconcile.moved) +
                           " objets verrouillés ont changé de monstre");
        if (reconcile.orphans > 0) save_locks();
    }
    file.loaded = true;
    export_file = std::move(file);
    return true;
}

bool DataStore::load_storage(const fs::path& path) {
    std::vector<std::string> messages;
    const auto sink = [&](const std::string& message) { messages.push_back(message); };
    LoadedFile file;
    file.path = path;
    file.modified = file_date(path);
    auto loaded = swcore::StorageStacks::load(path, sink);
    file.error = blocking_error(messages);
    warnings.insert(warnings.end(), messages.begin(), messages.end());
    if (!file.error.empty()) {
        storage_file = std::move(file);
        return false;
    }
    storage = std::move(loaded);
    file.loaded = true;
    ++generation;
    storage_file = std::move(file);
    return true;
}

void DataStore::refresh_swarfarm() {
    swarfarm = {};
    const fs::path directory = root_ / "data" / "swarfarm";
    std::error_code error;
    if (!fs::is_directory(directory, error)) return;
    fs::file_time_type newest{};
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        if (!entry.is_regular_file(error) || entry.path().extension() != ".json")
            continue;
        ++swarfarm.files;
        if (entry.path().filename() == "monsters.json") swarfarm.monsters = true;
        const auto stamp = entry.last_write_time(error);
        if (!error && stamp > newest) {
            newest = stamp;
            swarfarm.newest = file_date(entry.path());
        }
    }
}

void DataStore::start_dungeon_load() {
    if (dungeon_state_ || dungeon_future_.valid()) return;  // une seule fois
    // Copies : le chargement ne touche à rien du thread de l'interface.
    dungeon_future_ = std::async(
        std::launch::async,
        [root = root_, reference = reference, set_names = mapping.set_names]() {
            DungeonState state;
            const auto sink = [&](const std::string& message) {
                state.warnings.push_back(message);
            };
            state.data = swcore::DungeonData::load(
                root / "data" / "swarfarm", root / "data" / "overrides" / "skills_overrides.json",
                reference, sink);
            state.teams = swcore::load_team_configs(root / "data" / "teams", sink);
            for (auto& team : state.teams)
                swcore::validate_team_config(team, reference, set_names);
            state.mechanics = swcore::Mechanics::load(root / "data" / "mechanics.json", sink);
            return state;
        });
}

const DataStore::DungeonState* DataStore::dungeons() {
    if (!dungeon_state_ && dungeon_future_.valid() &&
        dungeon_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        dungeon_state_ = dungeon_future_.get();
        warnings.insert(warnings.end(), dungeon_state_->warnings.begin(),
                        dungeon_state_->warnings.end());
    }
    return dungeon_state_ ? &*dungeon_state_ : nullptr;
}

}  // namespace swgui

namespace swgui {

void DataStore::save_locks() {
    ++locks_revision;
    if (!locks.save(user_directory() / "locks.json"))
        warnings.push_back("Impossible d'écrire data/user/locks.json");
}

}  // namespace swgui

namespace swgui {

void DataStore::recompute_effective_state() {
    // État effectif = export + actions : utilisé partout (monstres, pool, verrous).
    state = swcore::effective_state(exported, equip_log.actions, effects, reference);
    artifact_count = 0;
    for (const auto& monster : monsters) artifact_count += monster.artifacts.size();
    ++generation;
}

void DataStore::save_equip_log() {
    if (!equip_log.save(user_directory() / "equip_actions.json"))
        warnings.push_back("Impossible d'écrire data/user/equip_actions.json");
}

void DataStore::apply_equip(swcore::EquipAction action) {
    std::uint64_t next = 1;
    for (const auto& existing : equip_log.actions) next = std::max(next, existing.id + 1);
    action.id = next;
    equip_log.actions.push_back(std::move(action));
    save_equip_log();
    recompute_effective_state();
}

bool DataStore::undo_equip() {
    if (equip_log.actions.empty()) return false;
    equip_log.actions.pop_back();
    save_equip_log();
    recompute_effective_state();
    return true;
}

void DataStore::reset_equip() {
    equip_log.actions.clear();
    save_equip_log();
    recompute_effective_state();
}

}  // namespace swgui
