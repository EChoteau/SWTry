#pragma once

// Optimisation d'une team (5 monstres) avec le solveur mono-monstre :
// 1. passage glouton dans l'ordre de runage (ancres de vitesse, supports, puis
//    nukers par poids c_i décroissant) : chaque monstre est runé sous ses
//    contraintes (fenêtre de VIT calculée avec les VIT déjà choisies, taux
//    critique cible, sets du preset) ; les runes prises ne sont plus disponibles ;
// 2. descente coordonnée (3 passes au plus, arrêt si une passe n'améliore
//    rien) : on libère les runes d'un monstre, on recalcule ses contraintes,
//    on le re-rune, et on garde le build seulement si le score de team monte
//    et que les contraintes tiennent.
// Un monstre verrouillé garde son build et ses runes. Pas de recherche jointe.

#include "swcore/locks.hpp"
#include "swcore/set_effects.hpp"
#include "swcore/solver.hpp"
#include "swcore/solver_config.hpp"
#include "swcore/team_analysis.hpp"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>

namespace swcore {

struct TeamOptimizerSettings {
    // Sets par monstre (noms de rune_sets.json) ; vide : tous sets.
    std::map<std::string, std::vector<std::string>> sets;
    std::set<std::string> locked;                // monstres verrouillés (noms de la config)
    std::map<int, GrindstoneRule> grindstones;   // vide : pas de meules
    std::map<int, GrindstoneRule> gems;          // vide : pas de gemmes
    int craft_min_level = 12;
    int minimum_rune_level = 12;
    int minimum_stars = 6;
    std::int64_t time_budget_ms = 8000;          // par recherche mono-monstre
    int max_passes = 3;
};

struct TeamOptimizerInput {
    TeamAnalysisInput analysis;   // team, données, étage, mechanics (box ignorée)
    const std::vector<AccountMonster>* box = nullptr;
    const std::vector<Rune>* runes = nullptr;
    const SetEffects* effects = nullptr;
    std::filesystem::path mapping_directory;
    const LockTable* locks = nullptr;
    const std::unordered_map<int, std::string>* set_names = nullptr;
};

struct TeamOptimizerMember {
    std::string name;
    bool locked = false;
    bool owned = false;
    bool changed = false;                  // build différent des runes actuelles
    std::optional<SolverBuild> build;      // dernier build retenu
    SolverConfig config;                   // config de la dernière recherche retenue
    std::string scenario;
    std::string note;                      // ex. "aucun build sous les contraintes"
};

struct TeamOptimizerResult {
    std::vector<AccountMonster> box;       // box hypothétique (builds appliqués)
    std::vector<TeamOptimizerMember> members;  // ordre de la config
    std::map<std::string, std::uint64_t> units;  // exemplaire de chaque membre (unit_id)
    double score_before = 0;
    double score_after = 0;
    bool constraints_before = false;
    bool constraints_after = false;
    int passes = 0;                        // passes de descente effectuées
    bool cancelled = false;
    std::vector<std::string> log;          // étapes (affichées dans l'écran team)
};

// Sets du preset -> scénario du solveur (pièces = pièces requises x
// occurrences). nullopt + problème si un set est inconnu ou si le total ne
// fait pas 6 pièces ; liste vide -> "Tous sets".
std::optional<SetScenario> scenario_from_sets(const std::vector<std::string>& names,
                                              const std::unordered_map<int, std::string>& set_names,
                                              const SetEffects& effects, std::string* problem);

// Copie du monstre équipé des runes du build (stats recalculées).
void equip_build(AccountMonster& unit, const SolverBuild& build, const SetEffects& effects);

TeamOptimizerResult optimize_team(const TeamOptimizerInput& input,
                                  const TeamOptimizerSettings& settings, std::stop_token stop,
                                  const std::function<void(const std::string&)>& progress = {});

}  // namespace swcore
