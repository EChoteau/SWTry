#pragma once

// Bonus des tours (compétences d'invocateur / décorations) lus dans
// data/game/account/summoner_skills.json : tables par niveau (1 à 20), en % des
// stats de base, sauf les dégâts critiques (points de %).

#include "swcore/reference_db.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace swcore {

struct TowerBonuses {
    bool loaded = false;
    int level = 0;
    double hp = 0;           // % des PV de base
    double attack = 0;       // % de l'ATQ de base
    double defense = 0;      // % de la DEF de base
    double speed = 0;        // % de la VIT de base
    double crit_damage = 0;  // points de dégâts critiques
    std::map<Element, double> element_attack;  // % de l'ATQ de base, par élément
};

// Toutes les tours au même niveau (1 à 20). Fichier absent ou niveau hors
// table : bonus à 0 et avertissement.
TowerBonuses load_tower_bonuses(const std::filesystem::path& path, int level,
                                WarningSink warnings = {});

}  // namespace swcore
