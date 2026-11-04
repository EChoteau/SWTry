#pragma once

// Analyse d'une team contre un étage :
// - VIT de combat, ordre des tours (ATB), fenêtres de VIT, taux critique cible ;
// - dégâts du script sur le boss, pire / meilleur cas, verdict, ce qui manque ;
// - survie au premier coup du boss ; score de team (à maximiser).
// Déterministe : l'aléa porte seulement sur les replis de compétences, les
// effets aléatoires (résistés ou non) et les hypothèses null de mechanics.json.

#include "swcore/account.hpp"
#include "swcore/atb.hpp"
#include "swcore/dungeon.hpp"
#include "swcore/team_config.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace swcore {

// VIT de combat = VIT totale (base + runes + sets) + bonus de leader et de
// tour de VIT (en % de la base), arrondi selon mechanics.json.
struct CombatSpeed {
    int base = 0;           // VIT de base (6* niv. 40)
    int total = 0;          // base + runes + sets (comme le tableau Monstres)
    double leader_percent = 0;
    double tower_percent = 0;
    int bonus = 0;          // part leader + tour (arrondie)
    int combat = 0;         // valeur utilisée
    bool manual = false;    // VIT saisie à la main dans l'écran team
};

// Taux critique : CR des runes requis pour 100 %.
struct CritTarget {
    bool needed = false;    // le monstre inflige des dégâts dans le script
    int base = 0;
    int leader = 0;         // si la zone / l'élément du leader s'applique
    int advantage = 0;      // avantage élémentaire contre le boss
    int required_runes = 0; // max(0, 100 - base - leader - advantage)
    int current_runes = 0;  // CR actuel des runes (total - base)
    bool satisfied() const { return !needed || current_runes >= required_runes; }
};

struct TeamMemberAnalysis {
    const TeamMonster* config = nullptr;
    const ReferenceMonster* reference = nullptr;  // null : id SWARFARM inconnu
    const AccountMonster* unit = nullptr;         // null : absent de la box
    Element element = Element::Unknown;
    StatValues white;   // stats de base (6* niv. 40)
    StatValues total;   // stats actuelles (runes, sets, artefacts) ; base si absent
    std::map<int, const SkillData*> skills;       // par slot (1, 2, 3...)
    CombatSpeed speed;
    CritTarget crit;
    SpeedWindow window;
    // Poids c_i du score : dégâts (part ATQ) / (ATQ x (1 + DC)).
    double coefficient = 0;
};

// Dégâts d'une action sur le boss.
struct ActionDamage {
    int tick = 0;
    std::string actor;
    int skill = 0;
    bool fallback = false;   // compétence de repli
    bool extra_turn = false; // tour en plus (S1)
    double damage = 0;
    double atk_damage = 0;   // part proportionnelle à l'ATQ
    bool crit = false;
    bool def_broken = false; // DEF du boss réduite pour ce coup
    bool atk_buff = false;
    bool kills = false;
    std::string note;
};

// Un scénario (pire ou meilleur cas).
struct CaseOutcome {
    AtbResult atb;
    std::vector<ActionDamage> actions;  // jusqu'au premier tour du boss
    double boss_hp = 0;
    double damage = 0;
    double atk_damage = 0;
    bool kills = false;
    double remaining = 0;
    double remaining_percent = 0;
    // Facteur sur ATQ x (1 + DC) des attaquants pour tuer (1 si déjà mort).
    double required_factor = 1;
    std::size_t attacks = 0;            // attaques reçues par le boss
    std::optional<std::string> counter; // contre-attaque avant son tour
    bool attack_tower_before_boss = false;
    bool defense_tower_before_boss = false;
};

// Premier coup du boss sur chaque monstre.
struct SurvivalLine {
    std::string member;
    int hp = 0;
    double hit = 0;
    double crit_hit = 0;
    bool survives = false;
    bool survives_crit = false;
};

struct TeamAnalysis {
    bool valid = false;                 // étage avec vagues et boss trouvé
    std::vector<TeamMemberAnalysis> members;  // ordre de la config
    std::vector<AtbUnit> units;         // ordre de départage (leader, team, ennemis)
    std::vector<std::size_t> member_unit;  // index d'unité de chaque membre
    std::size_t boss_unit = 0;
    std::vector<AtbStep> script;        // pire cas (sert aux fenêtres)
    const DungeonEnemy* boss = nullptr;
    AtbResult atb;                      // = worst.atb
    CaseOutcome worst;
    CaseOutcome best;
    std::vector<SurvivalLine> survival;
    std::string boss_skill;             // compétence du boss retenue (pire cas)
    bool survival_matters = true;       // faux : personne ne survit, tuer avant
    // Score de team (pire cas, coups critiques supposés) : à maximiser.
    double score = 0;
    // Toutes les contraintes : script avant le boss (pire cas) et taux critique.
    bool constraints_ok = false;
    std::vector<std::string> missing;   // ce qui manque (verdict)
    // Contraintes relatives détectées sur les bornes des fenêtres.
    std::vector<std::string> relative;
    // Hypothèses non vérifiées utilisées et limites du calcul.
    std::vector<std::string> unverified;
    std::vector<std::string> notes;
};

struct TeamAnalysisInput {
    const TeamConfig* team = nullptr;
    const DungeonData* data = nullptr;
    const Dungeon* dungeon = nullptr;
    const DungeonLevel* level = nullptr;
    const ReferenceDB* reference = nullptr;
    const std::vector<AccountMonster>* box = nullptr;  // null : aucun export
    const Mechanics* mechanics = nullptr;
    std::map<std::string, int> manual_speeds;  // nom (config) -> VIT de combat
    // Exemplaire imposé (nom -> unit_id) ; sinon choose_box_unit.
    std::map<std::string, std::uint64_t> pinned_units;
    bool compute_windows = true;               // faux : plus rapide (optimiseur)
};

TeamAnalysis analyze_team(const TeamAnalysisInput& input);

// Exemplaire de la box retenu pour un monstre : le plus de runes, puis le
// niveau, puis les étoiles. nullptr si absent.
const AccountMonster* choose_box_unit(const std::vector<AccountMonster>& box, int com2us_id);

// Avantage élémentaire (Eau > Feu > Vent > Eau ; Lumière <-> Ténèbres).
bool has_element_advantage(Element attacker, Element defender);

// Le leader skill s'applique-t-il dans ce contenu à ce monstre ?
bool leader_applies(const LeaderSkill& leader, const std::string& dungeon_category,
                    Element member_element);

// Évalue une formule SWARFARM ("{ATK}*(4.0*{Target Current HP %} + 5.0)").
// Variable ou identifiant inconnu : 0, ajouté à `unknown`.
double evaluate_skill_formula(const std::string& formula,
                              const std::map<std::string, double>& variables,
                              std::vector<std::string>* unknown = nullptr);

// Bonus de dégâts des skill-ups (somme des "Damage +x%"), en %.
double skill_damage_bonus(const SkillData& skill);

}  // namespace swcore
