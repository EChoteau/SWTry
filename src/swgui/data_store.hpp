#pragma once

#include "swcore/account.hpp"
#include "swcore/equip.hpp"
#include "swcore/locks.hpp"
#include "swcore/reference_db.hpp"
#include "swcore/runes.hpp"
#include "swcore/set_effects.hpp"
#include "swcore/storage_stacks.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace swgui {

// Fichier chargé : chemin, date, erreur éventuelle (affichée dans la page).
struct LoadedFile {
    std::filesystem::path path;
    std::string modified;
    std::string error;
    bool loaded = false;
};

struct SwarfarmStatus {
    std::size_t files = 0;
    std::string newest;     // date du fichier le plus récent
    bool monsters = false;  // monsters.json présent
};

// Toutes les données chargées, uniquement via les fonctions de swcore
// (mêmes chargements et même calcul des stats que swcli).
class DataStore {
public:
    explicit DataStore(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path mapping_directory() const { return root_ / "data" / "mapping"; }
    std::filesystem::path user_directory() const { return root_ / "data" / "user"; }

    bool load_export(const std::filesystem::path& path);
    bool load_storage(const std::filesystem::path& path);
    void refresh_swarfarm();

    LoadedFile export_file;
    LoadedFile storage_file;
    SwarfarmStatus swarfarm;

    swcore::RuneMapping mapping;
    swcore::ReferenceDB reference;
    swcore::SetEffects effects;
    // État effectif (export + équipement virtuel) : monsters et runes y renvoient.
    swcore::AccountState state;
    std::vector<swcore::AccountMonster>& monsters = state.monsters;
    std::vector<swcore::Rune>& runes = state.runes;
    swcore::StorageStacks storage;
    std::size_t artifact_count = 0;
    std::size_t generation = 0;  // incrémenté à chaque chargement réussi

    // Verrous (réservations), data/user/locks.json : indépendants des exports.
    swcore::LockTable locks;
    std::size_t locks_revision = 0;  // incrémenté à chaque changement
    void save_locks();               // écriture atomique

    // Équipement virtuel (data/user/equip_actions.json). monsters / runes
    // ci-dessus = état effectif (export + actions), utilisé partout.
    swcore::AccountState exported;   // export tel que chargé
    swcore::EquipLog equip_log;
    void recompute_effective_state();
    void save_equip_log();
    void apply_equip(swcore::EquipAction action);  // empile, enregistre, recalcule
    bool undo_equip();                             // dernière action seulement
    void reset_equip();

    // Avertissements de lecture, à afficher regroupés dans la barre d'état.
    std::vector<std::string> warnings;

private:
    std::filesystem::path root_;
};

// Dossier contenant data/mapping : dossier courant, puis dossier de l'exécutable
// et ses parents.
std::filesystem::path find_data_root();

}  // namespace swgui
