#pragma once

// Modèle "donjon" chargé une fois depuis les données SWARFARM :
// Dungeon -> Level -> Wave -> Enemy (stats + compétences résolues).
//   dungeons.json : id, name, category, levels
//   levels.json   : waves[].enemies[] (monster = id SWARFARM)
//   monsters.json : skills (ids) -> skills.json
// Des données SWARFARM sont connues comme peu fiables (flag aoe, skills sans
// nom, cristaux/tours sans multiplicateur) : data/game/monsters/skills_overrides.json
// corrige une compétence par id après le chargement.
//
// Fusion "other_skill" : dans skills.json, la partie dégâts de certaines
// compétences est une entrée séparée, rattachée à aucun monstre (used_on
// vide), qui pointe vers la compétence principale par other_skill. Exemple :
// Tempest de Teshar, 1181 (stun + recharge, sans multiplicateur) <- 2982
// ("3.7*{ATK}", AoE). Au chargement, si la principale n'a pas de
// multiplicateur, elle reprend celui de l'entrée secondaire (multiplicateur,
// formule brute, AoE, nombre de coups) et ses effets absents ; les skill-ups
// restent ceux de la principale. Une principale qui a déjà un multiplicateur
// n'est pas touchée (l'entrée secondaire est alors une variante, ex. éveil 2).
// Ordre : fusion, puis overrides (la correction manuelle gagne).
//
// Trous restants : DungeonData::damage_gaps() liste les compétences de
// monstres qui frappent d'après leur description mais restent sans
// multiplicateur après fusion et overrides. Ce sont les cas à compléter à la
// main dans skills_overrides.json (liste visible dans la page Donjons).
// Détection par la description : active commençant par "Attack", passive
// contenant "attacks all enemies", "attacks the enemy", etc. Candidats à
// vérifier, pas une certitude.

#include "swcore/reference_db.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace swcore {

// Bonus d'un niveau de compétence ("Damage +{0}%", "Cooltime Turn -{0}"...).
struct SkillUpgrade {
    std::string effect;  // gabarit SWARFARM
    double amount = 0;
};

// Effet d'une compétence (buff, débuff, barre...) tel que décrit par SWARFARM.
struct SkillEffect {
    std::string name;     // "Remove Buff", "Defense Break"...
    double chance = 0;    // % (100 = garanti)
    int quantity = 0;     // durée, valeur ou nombre selon l'effet
    bool self_effect = false;
    bool aoe = false;
    bool on_death = false;  // déclenché quand un ennemi meurt (ex. recharge réinitialisée)
};

struct SkillData {
    int id = 0;
    int com2us_id = 0;
    std::string name;
    int slot = 0;
    std::optional<int> cooltime;  // nullopt : pas de recharge
    int hits = 0;
    bool aoe = false;
    bool passive = false;
    int max_level = 1;
    std::string multiplier_formula;      // texte SWARFARM ("{ATK}*(4.0*...)")
    std::string multiplier_formula_raw;  // forme brute SWARFARM ("[[\"ATK\", \"*\", 3.7]]")
    std::string description;             // texte du jeu (anglais, SWARFARM)
    std::vector<SkillUpgrade> upgrades;
    std::vector<SkillEffect> effects;
    std::optional<int> other_skill;  // compétence principale (entrée secondaire)
    std::vector<int> used_on;        // monstres SWARFARM qui ont cette compétence
    // Entrées secondaires fusionnées dans celle-ci (ids skills.json).
    std::vector<int> merged_from;
    // Correction appliquée depuis skills_overrides.json (texte de la note).
    bool overridden = false;
    std::string override_note;
};

struct DungeonEnemy {
    int swarfarm_id = 0;          // monster de levels.json
    bool known = false;           // présent dans monsters.json
    int com2us_id = 0;
    Element element = Element::Unknown;
    std::string name;             // nom SWARFARM, ou "Monstre <id>"
    int level = 0;
    int stars = 0;
    int hp = 0;
    int attack = 0;
    int defense = 0;
    int speed = 0;
    int resist = 0;
    int accuracy_bonus = 0;
    int crit_bonus = 0;
    int crit_damage_reduction = 0;
    std::vector<int> skill_ids;   // compétences résolues (voir DungeonData::skill)
};

struct DungeonWave {
    std::vector<DungeonEnemy> enemies;
};

struct DungeonLevel {
    int id = 0;
    int dungeon_id = 0;
    int floor = 0;
    std::optional<std::string> difficulty;  // souvent null dans SWARFARM
    std::vector<DungeonWave> waves;
    bool has_waves() const { return !waves.empty(); }
};

struct Dungeon {
    int id = 0;
    std::string name;
    std::string category;
    bool enabled = true;
    std::vector<int> level_ids;
};

// Correction d'une compétence (skills_overrides.json) : seuls les champs
// présents remplacent la donnée SWARFARM.
struct SkillOverride {
    std::optional<std::string> name;
    std::optional<bool> aoe;
    std::optional<std::string> multiplier_formula;
    std::optional<std::optional<int>> cooltime;  // valeur, ou null = aucune recharge
    std::optional<int> hits;
    std::optional<std::vector<SkillEffect>> effects;
    std::string note;
};

class DungeonData {
public:
    // swarfarm_directory : dungeons.json, levels.json, skills.json.
    // overrides : skills_overrides.json (absent : aucune correction).
    // Fichier absent ou illisible : avertissement, modèle vide ou partiel.
    static DungeonData load(const std::filesystem::path& swarfarm_directory,
                            const std::filesystem::path& overrides,
                            const ReferenceDB& reference, WarningSink warnings = {});

    const std::vector<Dungeon>& dungeons() const { return dungeons_; }
    // Catégories dans l'ordre de dungeons.json.
    const std::vector<std::string>& categories() const { return categories_; }
    const Dungeon* dungeon(int id) const;
    const DungeonLevel* level(int id) const;
    const SkillData* skill(int id) const;
    // Au moins un étage du donjon a des vagues (sinon "données indisponibles").
    bool has_wave_data(const Dungeon& dungeon) const;
    bool empty() const { return dungeons_.empty(); }
    std::size_t overrides_applied() const { return overrides_applied_; }
    // Compétences principales complétées par une entrée other_skill.
    std::size_t skills_merged() const { return skills_merged_; }
    // Compétences de monstres qui frappent mais sans multiplicateur (triées par id).
    const std::vector<int>& damage_gaps() const { return damage_gaps_; }

private:
    std::vector<Dungeon> dungeons_;
    std::vector<std::string> categories_;
    std::unordered_map<int, std::size_t> dungeon_index_;
    std::unordered_map<int, DungeonLevel> levels_;
    std::unordered_map<int, SkillData> skills_;
    std::size_t overrides_applied_ = 0;
    std::size_t skills_merged_ = 0;
    std::vector<int> damage_gaps_;
};

// Lecture de skills_overrides.json : { "skills": { "<id>": { ... } } }.
std::map<int, SkillOverride> load_skill_overrides(const std::filesystem::path& path,
                                                  WarningSink warnings = {});
void apply_skill_override(SkillData& skill, const SkillOverride& override);

// Fusionne une entrée secondaire (other_skill) dans la compétence principale.
// Renvoie false (rien changé) si la principale a déjà un multiplicateur ou si
// la secondaire n'en a pas.
bool merge_other_skill(SkillData& main, const SkillData& part);

// La compétence frappe d'après sa description, mais n'a pas de multiplicateur.
bool is_damage_gap(const SkillData& skill);

}  // namespace swcore
