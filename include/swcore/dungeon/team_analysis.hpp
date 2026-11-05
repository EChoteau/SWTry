#pragma once

// Analyse d'une team contre un étage, par simulation pure : les stats sont
// calculées sur une liste de runes quelconque (build candidat de
// l'optimiseur, runes actuelles pour comparaison, ou aucune rune), jamais
// sur l'état "équipé" seul.
// - stats de combat (runes, sets, artefacts, tours, leader) ;
// - plan de vitesse joint (VIT minimales requises, sans égalité) ;
// - ordre des tours (ATB), dégâts du script, pire / meilleur cas, verdict ;
// - survie au premier coup du boss ; score de team ; ce qui manque.

#include "swcore/account/account.hpp"
#include "swcore/dungeon/atb.hpp"
#include "swcore/dungeon/dungeon.hpp"
#include "swcore/runes/set_effects.hpp"
#include "swcore/dungeon/team_config.hpp"
#include "swcore/account/tower_bonuses.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace swcore {

// Bonus de combat d'un monstre (en plus des runes) : tours + leader.
struct CombatBonus {
    double hp_percent = 0;       // % des PV de base
    double attack_percent = 0;   // % de l'ATQ de base (tour ATQ + tour élément + leader)
    double defense_percent = 0;
    double speed_percent = 0;    // tour de VIT + leader VIT
    int crit_rate = 0;           // leader
    int crit_damage = 0;         // tour + leader
    int resistance = 0;
    int accuracy = 0;
};

CombatBonus combat_bonus(Element element, const TowerBonuses* towers,
                         const std::optional<LeaderSkill>& leader,
                         const std::string& dungeon_category);

// Stats de combat = stats des runes (base + runes + sets + artefacts) + bonus.
// Les parts en % de la base sont arrondies selon `rounding` ("ceil" par défaut,
// voir combat_stats dans team_analysis.cpp).
StatValues combat_stats(const StatValues& white, const StatValues& runes,
                        const CombatBonus& bonus, const std::string& rounding = "ceil");

// Stats "runes" d'un monstre avec une liste de runes quelconque (les runes
// équipées de `unit` sont ignorées ; artefacts et base conservés).
StatBlock stats_with_runes(const AccountMonster& unit, const std::vector<Rune>& runes,
                           const SetEffects& effects);

struct CombatSpeed {
    int base = 0;           // VIT de base (6* niv. 40)
    int runes = 0;          // base + runes + sets
    double tower_percent = 0;
    double leader_percent = 0;
    int bonus = 0;          // part tours + leader (arrondie)
    int combat = 0;         // VIT de combat analysée
    int minimum = 0;        // VIT de combat sans aucune rune (base + bonus)
};

// Taux critique : CR des runes requis pour 100 %.
struct CritTarget {
    bool needed = false;    // le monstre inflige des dégâts dans le script
    int base = 0;
    int leader = 0;         // si la zone / l'élément du leader s'applique
    int advantage = 0;      // avantage élémentaire contre le boss
    int required_runes = 0; // max(0, 100 - base - leader - advantage)
    int current_runes = 0;  // CR des runes analysées (total - base)
    bool satisfied() const { return !needed || current_runes >= required_runes; }
};

struct TeamMemberAnalysis {
    const TeamMonster* config = nullptr;
    const ReferenceMonster* reference = nullptr;  // null : id SWARFARM inconnu
    const AccountMonster* unit = nullptr;         // null : absent de la box
    Element element = Element::Unknown;
    std::map<int, const SkillData*> skills;       // par slot (1, 2, 3...)
    bool has_runes = false;   // runes analysées (build candidat ou runes actuelles)
    StatValues white;         // base 6* niv. 40
    StatValues runes;         // base + runes + sets + artefacts
    StatValues combat;        // + tours + leader : stats de combat
    CombatSpeed speed;
    CritTarget crit;
    SpeedWindow window;       // fenêtre stricte avec les VIT de combat des autres
    int plan_min = 0;         // VIT de combat minimale requise (plan joint)
    std::optional<int> plan_max;  // VIT maximale (plan) si bornée
    double coefficient = 0;   // poids c_i du score
};

// Dégâts d'une attaque supplémentaire (zone) sur un ennemi de la vague.
struct TargetDamage {
    std::string name;
    bool boss = false;
    double damage = 0;
    double hp_after = 0;      // PV restants de cet ennemi
    bool kills = false;
    // Stun : chance de réussite (RÉS de la cible contre PRÉC de l'attaquant),
    // ou nullopt si la cible y est insensible / pas de stun.
    std::optional<double> stun_chance;
    bool stunned = false;     // retenu dans ce scénario (meilleur cas)
    std::string stun_text;    // "insensible", "résisté (pire cas)", "étourdie"...
};

// Dégâts d'une action sur le boss. Une passive "attaque supplémentaire après
// ton attaque" (Tempest) est une ligne séparée, juste après l'action.
struct ActionDamage {
    int tick = 0;
    std::string actor;
    int skill = 0;
    int step = -1;           // action du script (-1 : hors script, IA auto)
    bool fallback = false;   // compétence de repli
    bool extra = false;      // attaque supplémentaire passive (ligne séparée)
    std::string skill_name;  // nom de la compétence (lignes "extra")
    double damage = 0;       // sur le boss
    double atk_damage = 0;   // part proportionnelle à l'ATQ (sur le boss)
    double boss_hp_after = 0;  // PV du boss après cette ligne
    bool crit = false;
    bool def_broken = false; // DEF du boss réduite pour ce coup
    bool atk_buff = false;
    bool kills = false;
    std::vector<TargetDamage> targets;  // lignes "extra" : chaque ennemi touché
    // Action principale d'un monstre à passive d'attaque supplémentaire :
    // "+ Tempest" ou "Tempest en recharge (...)".
    std::string passive;
    std::string note;
};

// Un scénario (pire ou meilleur cas).
struct CaseOutcome {
    AtbResult atb;
    std::vector<AtbEvent> events;       // étourdissements / morts injectés dans l'ATB
    std::vector<std::string> stun_notes;  // tours ennemis perdus et leur impact
    std::vector<ActionDamage> actions;  // tours alliés jusqu'au premier tour du boss
    double boss_hp = 0;
    double damage = 0;
    double atk_damage = 0;
    bool kills = false;
    double remaining = 0;
    double remaining_percent = 0;
    double required_factor = 1;  // facteur sur ATQ x (1 + DC) des attaquants
    std::size_t attacks = 0;
    std::optional<std::string> counter;
    bool attack_tower_before_boss = false;
    bool defense_tower_before_boss = false;
};

struct SurvivalLine {
    std::string member;
    int hp = 0;
    double hit = 0;
    double crit_hit = 0;
    bool survives = false;
    bool survives_crit = false;
};

struct TeamAnalysis {
    bool valid = false;
    std::vector<TeamMemberAnalysis> members;  // ordre de la config
    std::vector<AtbUnit> units;               // leader, team, ennemis (VIT analysées)
    std::vector<std::size_t> member_unit;
    std::size_t boss_unit = 0;
    std::vector<AtbStep> script;              // pire cas
    const DungeonEnemy* boss = nullptr;
    bool all_runes = false;                   // chaque membre a des runes analysées

    // Plan de vitesse joint (indépendant des runes analysées).
    SpeedPlan plan;
    AtbResult plan_atb;                       // ordre des tours avec les VIT du plan
    std::vector<std::string> relative;        // contraintes relatives du plan

    // Simulation avec les VIT et stats analysées.
    PlanCheck speed_check;                    // vérification stricte
    AtbResult atb;                            // = worst.atb
    CaseOutcome worst;
    CaseOutcome best;
    std::vector<SurvivalLine> survival;
    std::string boss_skill;
    bool survival_matters = true;
    double score = 0;                         // dégâts au pire cas, critiques supposés
    bool constraints_ok = false;              // plan strict + CR + runes pour tous
    std::vector<std::string> missing;
    std::vector<std::string> unverified;
    std::vector<std::string> notes;
    // Hypothèses de la config affichées avec le verdict (passive prête sur le boss...).
    std::vector<std::string> hypotheses;
};

struct TeamAnalysisInput {
    const TeamConfig* team = nullptr;
    const DungeonData* data = nullptr;
    const Dungeon* dungeon = nullptr;
    const DungeonLevel* level = nullptr;
    const ReferenceDB* reference = nullptr;
    const std::vector<AccountMonster>* box = nullptr;  // null : aucun export
    const Mechanics* mechanics = nullptr;
    const SetEffects* effects = nullptr;
    const TowerBonuses* towers = nullptr;
    // Build candidat de chaque membre (nom -> runes de calcul).
    std::map<std::string, std::vector<Rune>> builds;
    // Membre sans build : runes actuelles (comparaison) ou aucune rune.
    bool use_current_runes = false;
    // VIT de combat imposées (optimiseur : membres pas encore runés ; tests).
    std::map<std::string, int> assumed_speeds;
    std::map<std::string, std::uint64_t> pinned_units;  // nom -> unit_id
    bool compute_windows = true;
};

TeamAnalysis analyze_team(const TeamAnalysisInput& input);

// Exemplaire de la box retenu pour un monstre : le plus de runes, puis le
// niveau, puis les étoiles. nullptr si absent.
const AccountMonster* choose_box_unit(const std::vector<AccountMonster>& box, int com2us_id);

bool has_element_advantage(Element attacker, Element defender);

bool leader_applies(const LeaderSkill& leader, const std::string& dungeon_category,
                    Element member_element);

double evaluate_skill_formula(const std::string& formula,
                              const std::map<std::string, double>& variables,
                              std::vector<std::string>* unknown = nullptr);

double skill_damage_bonus(const SkillData& skill);
// Recharge effective au niveau max (cooltime - réductions de skill-up).
std::optional<int> skill_cooldown(const SkillData& skill);

// Passive "attaque supplémentaire après ton attaque pendant ton tour" (Tempest
// de Teshar) : passive avec multiplicateur dont la description dit "after you
// attack" / "after attacking". Règles du moteur (génériques) :
// - déclenchée après chaque tour du monstre où il attaque (S1/S2, y compris un
//   tour obtenu par une augmentation de barre), si elle n'est pas en recharge ;
// - ordre : dégâts de la compétence -> morts -> attaque supplémentaire (zone :
//   tous les ennemis vivants, boss compris, chacun avec sa DEF) -> morts ->
//   effets (stun contre la RÉS de chaque cible) ;
// - recharge = skill_cooldown, en tours du monstre ; réinitialisée si un
//   ennemi meurt d'une de ses attaques et que la passive a un effet
//   "Reduce Cooltime" on_death.
bool is_after_attack_passive(const SkillData& skill);

}  // namespace swcore
