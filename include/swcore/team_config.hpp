#pragma once

// Configs de teams écrites à la main (data/teams/*.json, un fichier par team)
// et paramètres de mécaniques incertaines (data/mechanics.json), modifiables
// sans recompiler. Aucun calcul ici : chargement, validation, possession.

#include "swcore/account.hpp"
#include "swcore/reference_db.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace swcore {

struct TeamMonster {
    std::string name;      // nom affiché (config)
    int swarfarm_id = 0;   // id SWARFARM (monsters.json "id")
    std::string role;      // speed_anchor, support, nuker...
    int priority = 0;      // ordre de runage
    // Compétences imposées (ids skills.json), ex. arbre d'Homonculus choisi en
    // jeu. Vide : compétences de monsters.json.
    std::vector<int> skill_overrides;
};

// Une action du script contre le boss.
struct ScriptStep {
    std::string actor;                 // nom d'un monstre de la team
    int skill = 0;                     // 1, 2, 3...
    std::optional<int> fallback_skill; // si la compétence n'est pas disponible
    std::string target;                // "" ou "previous_actor"
    std::vector<std::string> effects;  // "def_break_guaranteed", "atb_boost_100"...
};

struct TeamConfig {
    std::filesystem::path file;
    std::string id;
    std::string name;
    int dungeon_id = 0;
    std::vector<int> level_ids;
    std::string leader;  // nom d'un monstre de la team ("" : aucun)
    std::vector<TeamMonster> monsters;
    std::vector<ScriptStep> boss_script;
    // "set1" / "set2" -> monstre -> sets conseillés (noms de rune_sets.json).
    std::map<std::string, std::map<std::string, std::vector<std::string>>> rune_presets;
    std::string notes;
    // Problèmes de validation (monstre inconnu, acteur absent, set inconnu...).
    std::vector<std::string> problems;

    const TeamMonster* monster(const std::string& name) const;
    bool matches(int dungeon, int level) const;
};

// Lit un fichier ; les erreurs bloquantes donnent nullopt + avertissement.
std::optional<TeamConfig> load_team_config(const std::filesystem::path& file,
                                           WarningSink warnings = {});
// Tous les *.json du dossier, triés par nom de fichier.
std::vector<TeamConfig> load_team_configs(const std::filesystem::path& directory,
                                          WarningSink warnings = {});
// Vérifie la config contre les données (monstres SWARFARM, noms de sets) et
// remplit TeamConfig::problems.
void validate_team_config(TeamConfig& team, const ReferenceDB& reference,
                          const std::unordered_map<int, std::string>& set_names);

// Possession dans la box (monstres de l'export) : par com2us_id.
struct TeamOwnership {
    std::vector<std::string> missing;  // noms des monstres absents de la box
    bool complete() const { return missing.empty(); }
};
TeamOwnership team_ownership(const TeamConfig& team, const ReferenceDB& reference,
                             const std::vector<AccountMonster>& box);

// Valeur de mechanics.json : null = hypothèse non vérifiée.
struct MechanicValue {
    enum class Kind { Null, Number, Bool, Text };
    Kind kind = Kind::Null;
    double number = 0;
    bool flag = false;
    std::string text;
};

// Clés aplaties par points : "def_formula.b", "per_monster.Teshar.tempest_cooldown".
class Mechanics {
public:
    static Mechanics load(const std::filesystem::path& file, WarningSink warnings = {});
    static Mechanics parse(const std::string& json_text, WarningSink warnings = {});

    bool loaded() const { return loaded_; }
    bool has(const std::string& key) const { return values_.contains(key); }
    // nullopt si absent, null ou d'un autre type.
    std::optional<double> number(const std::string& key) const;
    std::optional<bool> flag(const std::string& key) const;
    std::optional<std::string> text(const std::string& key) const;
    // Clés à null : hypothèses non vérifiées (le calcul prendra le pire cas).
    std::vector<std::string> unverified() const;
    const std::map<std::string, MechanicValue>& values() const { return values_; }

private:
    std::map<std::string, MechanicValue> values_;
    bool loaded_ = false;
};

}  // namespace swcore
