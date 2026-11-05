#pragma once

#include "swcore/account/account.hpp"
#include "swcore/account/locks.hpp"
#include "swcore/reference_db.hpp"
#include "swcore/runes/runes.hpp"
#include "swcore/runes/set_effects.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace swcore {

struct SolverBuild;

// Équipement virtuel : on ne modifie pas le jeu. Une action déplace des runes
// et artefacts dans la VUE du logiciel (export + actions) et liste ce qu'il
// faut refaire en jeu. Ne touche jamais aux verrous.

// État du compte : monstres (avec runes et artefacts équipés), toutes les runes,
// artefacts déséquipés virtuellement.
struct AccountState {
    std::vector<AccountMonster> monsters;
    std::vector<Rune> runes;
    std::vector<Artifact> spare_artifacts;
};

enum class EquipScope { Both, Runes, Artifacts };

// Déplacement d'un objet : source / destination = unit_id (0 = inventaire).
struct EquipMove {
    LockKind kind = LockKind::Rune;
    std::uint64_t id = 0;
    std::uint64_t from = 0;
    std::uint64_t to = 0;
    int slot = 0;

    friend bool operator==(const EquipMove&, const EquipMove&) = default;
};

// Meules et gemmes du build à appliquer en jeu sur une rune (non appliquées
// dans les stats : équiper n'utilise que les runes réelles).
struct EquipCraftTask {
    std::uint64_t rune_id = 0;
    int slot = 0;
    std::vector<AppliedGrindstone> grinds;
    std::vector<AppliedGem> gems;
};

struct EquipAction {
    std::uint64_t id = 0;
    std::string date;
    std::uint64_t target = 0;
    EquipScope scope = EquipScope::Both;
    std::vector<EquipMove> moves;
    std::vector<EquipCraftTask> craft;
};

// Journal persistant : data/user/equip_actions.json (CLI : --equip-actions).
struct EquipLog {
    std::uint64_t wizard_id = 0;
    std::vector<EquipAction> actions;  // pile : la dernière s'annule en premier

    static EquipLog load(const std::filesystem::path& path, WarningSink warnings = {});
    static EquipLog parse(const std::string& json_text, WarningSink warnings = {});
    std::string to_json() const;
    bool save(const std::filesystem::path& path) const;  // écriture atomique
};

// Nom affiché de l'emplacement des runes ("Inventaire", "Raoq (1785...)").
void name_rune_sources(AccountState& state, const ReferenceDB& reference);

// État effectif = export + actions appliquées dans l'ordre (stats recalculées).
// Sans action : identique à l'export.
AccountState effective_state(const AccountState& exported,
                             const std::vector<EquipAction>& actions,
                             const SetEffects& effects, const ReferenceDB& reference);

// Action qui équipe le build sur le monstre cible : sur chaque slot, la rune
// actuelle retourne en inventaire et la rune réelle du build passe sur la
// cible (son ancien porteur garde un slot vide). Artefacts : ceux du build
// (aujourd'hui ceux du monstre, le solveur ne les optimise pas encore).
EquipAction make_equip_action(const AccountState& state, std::uint64_t target,
                              const SolverBuild& build, EquipScope scope);

// Le build est déjà en place sur la cible (runes et/ou artefacts).
bool build_equipped(const AccountState& state, std::uint64_t target,
                    const SolverBuild& build, EquipScope scope);

// Nombre de changements virtuels par monstre (source ou destination).
std::map<std::uint64_t, int> pending_changes(const std::vector<EquipAction>& actions);

// Rapprochement avec un nouvel export : autre compte (wizard_id) = tout
// abandonné ; sinon les déplacements déjà faits en jeu sont retirés.
struct EquipReconcile {
    bool dropped = false;  // wizard_id différent
    int satisfied = 0;
    int remaining = 0;
};
EquipReconcile reconcile_equip_log(EquipLog& log, const AccountState& exported,
                                   std::uint64_t wizard_id);

// wizard_id de l'export SW Exporter (0 si introuvable).
std::uint64_t read_wizard_id(const std::filesystem::path& export_path);

}  // namespace swcore
