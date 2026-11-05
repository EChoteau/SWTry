#pragma once

// Optimisation d'une team (5 monstres) avec le solveur mono-monstre, par
// simulation pure : chaque build candidat est évalué en simulant toute la
// team (stats de combat -> ATB -> script -> dégâts -> verdict) avec les builds
// candidats des autres ; les runes équipées en jeu ne servent jamais d'état.
//
// 1. Glouton dans l'ordre de runage (ancres de vitesse, supports, puis nukers
//    par poids c_i décroissant). Contraintes dures de chaque recherche :
//    fenêtre de VIT stricte recalculée avec les VIT des builds candidats des
//    autres (VIT du plan pour ceux pas encore runés), CR cible, sets.
// 2. Descente coordonnée (3 passes au plus, arrêt si une passe n'améliore
//    rien) : un candidat n'est gardé que s'il améliore, dans l'ordre :
//    contraintes tenues (plan strict + CR), script complet avant le boss, boss
//    mort au pire cas, dégâts au pire cas, puis dégâts au meilleur cas.
// Un monstre verrouillé garde ses runes actuelles. Pas de recherche jointe.

#include "swcore/account/locks.hpp"
#include "swcore/runes/set_effects.hpp"
#include "swcore/solver/solver.hpp"
#include "swcore/solver/solver_config.hpp"
#include "swcore/dungeon/team_analysis.hpp"

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
    TeamAnalysisInput analysis;   // team, données, étage, mechanics, effets, tours
    const std::vector<AccountMonster>* box = nullptr;
    const std::vector<Rune>* runes = nullptr;
    std::filesystem::path mapping_directory;
    const LockTable* locks = nullptr;
    const std::unordered_map<int, std::string>* set_names = nullptr;
};

struct TeamOptimizerMember {
    std::string name;
    bool locked = false;
    bool owned = false;
    bool changed = false;                  // runes différentes des runes actuelles
    std::optional<SolverBuild> build;      // build candidat retenu
    SolverConfig config;                   // config de la recherche retenue
    std::string scenario;
    std::string note;                      // ex. "VIT requise non atteinte"
};

struct TeamOptimizerResult {
    // Builds candidats (runes de calcul) de chaque membre : ceux de
    // l'optimisation, et les runes actuelles des membres verrouillés.
    std::map<std::string, std::vector<Rune>> builds;
    std::map<std::string, std::uint64_t> units;  // exemplaire de chaque membre
    std::vector<TeamOptimizerMember> members;    // ordre de la config
    bool constraints_ok = false;           // sur la simulation finale
    bool script_ok = false;
    bool kills = false;                    // boss mort au pire cas
    double worst_damage = 0;
    double best_damage = 0;
    int passes = 0;
    bool cancelled = false;
    std::vector<std::string> log;
};

// Sets du preset -> scénario du solveur (pièces = pièces requises x
// occurrences). nullopt + problème si un set est inconnu ou si le total ne
// fait pas 6 pièces ; liste vide -> "Tous sets".
std::optional<SetScenario> scenario_from_sets(const std::vector<std::string>& names,
                                              const std::unordered_map<int, std::string>& set_names,
                                              const SetEffects& effects, std::string* problem);

TeamOptimizerResult optimize_team(const TeamOptimizerInput& input,
                                  const TeamOptimizerSettings& settings, std::stop_token stop,
                                  const std::function<void(const std::string&)>& progress = {});

}  // namespace swcore
