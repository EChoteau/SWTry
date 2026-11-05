#pragma once

// Ordre des tours (barre d'attaque, ATB), déterministe, sans aléa.
//
// Règles :
// - chaque tic : barre += tick_percent % de la VIT de combat ;
// - après l'incrément, si un ou plusieurs monstres sont >= 100, UN SEUL joue :
//   la barre la plus haute (égalité : ordre de départage, avertissement) ; sa
//   barre retombe à 0 ;
// - augmentation de barre = ajout (+100, pas "mise à 100") ; réduction =
//   soustraction, plancher 0.
// Le script contre le boss est suivi comme une sous-suite : un tour de l'acteur
// de la prochaine action du script exécute cette action ; tout autre tour
// allié est un tour "en plus" (sans effet). Un tour de plus avant le boss est
// donc accepté, mais l'ordre des actions du script doit être respecté.
//
// Référence (tests manuels, à ne pas exécuter ici) : VIT de combat Prilea 226,
// Teshar 167, Konamiya 167, Luna 155, Homonculus 153 ; Géant 90, tour droite
// 121, tour gauche 90 ; Konamiya S2 -> +100 à Teshar ; Luna S3 -> +100 à
// elle-même. Séquence attendue : t7 Prilea, t9 Teshar, t10 Konamiya, t11
// Teshar (123,4), t12 Luna (130,2), t13 Homonculus (139,2), t14 Luna (121,7),
// t15 tour droite, t16 Prilea, t17 Géant.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace swcore {

struct AtbUnit {
    std::string name;
    bool ally = true;
    int speed = 0;   // VIT de combat
    bool boss = false;
};

// Effet de barre d'une action du script.
struct AtbBoost {
    enum class Target { Self, PreviousAlly, Ally };
    Target target = Target::Self;
    std::size_t ally = 0;  // Target::Ally : index de l'unité
    double amount = 0;     // +amount (négatif : réduction, plancher 0)
};

struct AtbStep {
    std::size_t actor = 0;  // index dans les unités
    int skill = 0;
    std::vector<AtbBoost> boosts;
};

struct AtbTurn {
    int tick = 0;
    std::size_t actor = 0;
    double bar = 0;   // barre au moment de jouer (après l'incrément du tic)
    int step = -1;    // action du script exécutée, -1 : tour en plus
    bool tie = false; // égalité de barre départagée par l'ordre
    std::vector<std::size_t> tied;  // unités à égalité avec celle qui joue
    bool stunned = false;  // tour perdu (étourdi) : barre remise à 0 sans agir
};

// Conséquence d'un tour sur les autres unités, appliquée juste après le
// `actor_turn`-ième tour (0 = premier) de `actor` : unités étourdies (elles
// sautent leur prochain tour) et unités mortes (elles ne jouent plus).
struct AtbEvent {
    std::size_t actor = 0;
    int actor_turn = 0;
    std::vector<std::size_t> stunned;
    std::vector<std::size_t> killed;
    bool operator==(const AtbEvent&) const = default;
};

struct AtbResult {
    std::vector<AtbTurn> turns;
    // Barres de chaque unité après l'incrément de chaque tic (tic 1 = index 0).
    std::vector<std::vector<double>> bars;
    std::optional<std::size_t> boss_turn;  // index dans turns du 1er tour du boss
    std::size_t steps_done_before_boss = 0;
    bool script_before_boss = false;  // toutes les actions avant le 1er tour du boss
    std::vector<std::string> warnings;  // égalités (ordre non garanti)
};

struct AtbOptions {
    double tick_percent = 7.0;
    int turns_after_boss = 5;   // tours simulés après le premier tour du boss
    int max_ticks = 400;
    // Étourdissements / morts (analyse des dégâts) ; vide pour le plan de VIT.
    std::vector<AtbEvent> events;
};

// Les unités sont données dans l'ordre de départage (index croissant = joue
// en premier à barre égale).
AtbResult simulate_atb(const std::vector<AtbUnit>& units, const std::vector<AtbStep>& script,
                       const AtbOptions& options = {});

// Vérification stricte d'un vecteur de VIT contre le script :
// - toutes les actions avant le premier tour du boss ;
// - ordre respecté : un allié qui a encore une action à faire plus loin dans
//   le script ne doit pas jouer avant l'action en attente (sinon l'acteur de
//   l'action en attente est trop lent) ;
// - aucune égalité de barre pour une action du script (au moins 1 point
//   d'écart, y compris contre les ennemis).
// Si ça ne passe pas, `raise` désigne l'unité dont il faut monter la VIT.
struct PlanCheck {
    bool ok = false;
    std::optional<std::size_t> raise;
    std::string reason;
};
PlanCheck check_speed_plan(const std::vector<AtbUnit>& units, const std::vector<AtbStep>& script,
                           const AtbOptions& options = {});

// Plan de vitesse joint : on part des VIT données (les VIT minimales possibles
// des alliés) et on monte de 1 la VIT de l'unité fautive jusqu'à ce que
// check_speed_plan passe. speeds : VIT de chaque unité (ordre des unités).
struct SpeedPlan {
    bool found = false;
    std::vector<int> speeds;
    int iterations = 0;
    std::string reason;  // dernière cause d'échec si non trouvé
};
SpeedPlan plan_speeds(std::vector<AtbUnit> units, const std::vector<AtbStep>& script,
                      const AtbOptions& options = {}, int speed_cap = 400);

// Fenêtre de VIT d'une unité alliée, les autres fixes : intervalle de VIT
// contigu (autour de la VIT actuelle) pour lequel check_speed_plan passe
// (vérification stricte). lower_open / upper_open : bornes du balayage
// atteintes (pas de limite trouvée).
struct SpeedWindow {
    bool found = false;
    int min = 0;
    int max = 0;
    bool lower_open = false;
    bool upper_open = false;
    bool current_inside = false;
};

SpeedWindow speed_window(std::vector<AtbUnit> units, const std::vector<AtbStep>& script,
                         std::size_t unit, const AtbOptions& options = {}, int sweep_min = 1,
                         int sweep_max = 400);

}  // namespace swcore
