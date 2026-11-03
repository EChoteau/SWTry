#pragma once

#include "swcore/account.hpp"
#include "swcore/reference_db.hpp"
#include "swcore/runes.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace swcore {

struct SolverBuild;

// Verrou = réservation d'une rune ou d'un artefact pour un monstre
// (reserved_for = unit_id). reserved_for = 0 : réservé à personne (exclu de
// toutes les recherches). Le nom, le set et le slot ne servent qu'à l'affichage.
enum class LockKind { Rune, Artifact };

struct Lock {
    LockKind kind = LockKind::Rune;
    std::uint64_t id = 0;
    std::uint64_t reserved_for = 0;
    std::string monster_name;
    int set_id = 0;
    int slot = 0;

    friend bool operator==(const Lock&, const Lock&) = default;
};

enum class LockState { None, Partial, Full };

// Résultat du rapprochement avec un nouvel export.
struct LockReconcile {
    int orphans = 0;  // objets disparus : verrous retirés
    int moved = 0;    // objets verrouillés qui ont changé de monstre
};

class LockTable {
public:
    // Fichier absent : table vide. Fichier illisible : table vide + avertissement.
    static LockTable load(const std::filesystem::path& path, WarningSink warnings = {});
    static LockTable parse(const std::string& json_text, WarningSink warnings = {});
    std::string to_json() const;
    // Écriture atomique (fichier temporaire puis renommage).
    bool save(const std::filesystem::path& path) const;

    const Lock* find(LockKind kind, std::uint64_t id) const;
    void set(const Lock& lock);  // ajoute ou remplace
    bool remove(LockKind kind, std::uint64_t id);
    const std::vector<Lock>& locks() const { return locks_; }
    bool empty() const { return locks_.empty(); }

    // Retire les verrous dont l'objet a disparu ; compte ceux qui ont changé de monstre.
    LockReconcile reconcile(const std::vector<Rune>& runes,
                            const std::vector<AccountMonster>& monsters);

    friend bool operator==(const LockTable&, const LockTable&) = default;

private:
    std::vector<Lock> locks_;  // trié par (kind, id)
};

// Monstre qui équipe actuellement la rune (0 : inventaire).
std::uint64_t rune_owner(const Rune& rune);

// Respect des verrous : rune utilisable par ce monstre (non verrouillée, ou
// réservée à lui).
bool rune_usable_by(const LockTable& locks, const Rune& rune, std::uint64_t unit_id);
// Même règle pour les artefacts (pour la future recherche d'artefacts).
bool artifact_usable_by(const LockTable& locks, const Artifact& artifact,
                        std::uint64_t unit_id);

// État des runes ET artefacts équipés par le monstre.
LockState monster_lock_state(const LockTable& locks, const AccountMonster& monster);

// Verrouille / déverrouille ce que le monstre équipe (réservé à ce monstre).
void lock_monster(LockTable& locks, const AccountMonster& monster, const std::string& name,
                  bool runes, bool artifacts);
void unlock_monster(LockTable& locks, const AccountMonster& monster, bool runes,
                    bool artifacts);

// Verrouille une rune pour le monstre qui l'équipe (0 si inventaire).
void lock_rune_for_owner(LockTable& locks, const Rune& rune, const std::string& name);
void lock_artifact_for_owner(LockTable& locks, const Artifact& artifact,
                             const std::string& name);

// Valide un build : réserve ses runes (et/ou les artefacts du monstre) au
// monstre. Ses réservations existantes sur les mêmes slots sont d'abord
// libérées (une réservation par slot) ; elles sont renvoyées.
struct LockBuildResult {
    std::vector<Lock> released;
    int locked = 0;
};
LockBuildResult lock_build(LockTable& locks, const AccountMonster& monster,
                           const SolverBuild& build, const std::string& name,
                           bool runes = true, bool artifacts = false);
void unlock_build(LockTable& locks, const AccountMonster& monster, const SolverBuild& build,
                  bool runes = true, bool artifacts = false);
// Tout (dans le périmètre demandé) est réservé à ce monstre.
bool build_locked_for(const LockTable& locks, const AccountMonster& monster,
                      const SolverBuild& build, bool runes, bool artifacts);

// Écriture atomique d'un fichier texte (temporaire puis renommage).
bool write_text_atomic(const std::filesystem::path& path, const std::string& text);

}  // namespace swcore
