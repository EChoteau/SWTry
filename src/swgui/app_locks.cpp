// Actions de verrouillage du GUI : toute la logique est dans swcore/locks ;
// ici, seulement le choix du nom affiché et l'écriture de locks.json. On
// verrouille un monstre ou un build entier, jamais une rune isolée à la main
// (l'API par objet de swcore reste pour la page "Équipement et verrous").

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
