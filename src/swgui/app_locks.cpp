// Actions de verrouillage du GUI : toute la logique est dans swcore/locks ;
// ici, seulement le choix du nom affiché et l'écriture de locks.json.

#include "app.hpp"

#include "swcore/locks.hpp"

namespace swgui {

std::string App::owner_name(std::uint64_t unit_id) const {
    if (unit_id == 0) return "Inventaire";
    const auto* monster = find_monster(unit_id);
    return monster ? monster_name(monster->unit_master_id) : std::to_string(unit_id);
}

void App::locks_changed() {
    data_.save_locks();
    monster_sort_dirty_ = true;  // la colonne "Verrou" peut être triée
}

void App::toggle_rune_lock(const swcore::Rune& rune) {
    if (!data_.locks.remove(swcore::LockKind::Rune, rune.rune_id))
        swcore::lock_rune_for_owner(data_.locks, rune, owner_name(swcore::rune_owner(rune)));
    locks_changed();
}

void App::toggle_artifact_lock(const swcore::Artifact& artifact) {
    if (!data_.locks.remove(swcore::LockKind::Artifact, artifact.rid))
        swcore::lock_artifact_for_owner(data_.locks, artifact,
                                        owner_name(artifact.occupied_id));
    locks_changed();
}

void App::set_monster_locks(const swcore::AccountMonster& monster, bool lock, bool runes,
                            bool artifacts) {
    if (lock)
        swcore::lock_monster(data_.locks, monster, monster_name(monster.unit_master_id), runes,
                             artifacts);
    else
        swcore::unlock_monster(data_.locks, monster, runes, artifacts);
    locks_changed();
}

}  // namespace swgui

namespace swgui {

void App::toggle_rune_lock_for(const swcore::Rune& rune, std::uint64_t unit_id) {
    if (!data_.locks.remove(swcore::LockKind::Rune, rune.rune_id))
        data_.locks.set({swcore::LockKind::Rune, rune.rune_id, unit_id, owner_name(unit_id),
                         rune.set_id, rune.slot});
    locks_changed();
}

}  // namespace swgui
