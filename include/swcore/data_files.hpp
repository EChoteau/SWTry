#pragma once

// Emplacement de chaque fichier de données (voir data/README.md).
// Seul endroit à modifier si un fichier de données change de place.

namespace swcore::data_files {

// --- Dossiers, relatifs à la racine du projet ------------------------------
inline constexpr const char* game_dir = "data/game";          // données du jeu (versionnées)
inline constexpr const char* swarfarm_dir = "data/swarfarm";  // généré par swsync
inline constexpr const char* teams_dir = "data/teams";        // un sous-dossier par donjon
inline constexpr const char* user_dir = "data/user";          // réglages locaux (non versionnés)
inline constexpr const char* i18n_en = "data/i18n/en.json";

// --- Fichiers, relatifs à game_dir -----------------------------------------
// Runes
inline constexpr const char* rune_sets = "runes/rune_sets.json";
inline constexpr const char* rune_stats = "runes/rune_effectTypes.json";
inline constexpr const char* rune_mainstat = "runes/rune_mainstat.json";
inline constexpr const char* rune_quality = "runes/rune_quality.json";
inline constexpr const char* set_effects = "runes/set_effects.json";
// Meules et gemmes
inline constexpr const char* grindstone = "craft/grindstone.json";
inline constexpr const char* enchanted_gem = "craft/enchanted_gem.json";
// Artefacts
inline constexpr const char* artifact_types = "artifacts/artifact_types.json";
inline constexpr const char* artifact_rank = "artifacts/artifact_rank.json";
inline constexpr const char* artifact_main = "artifacts/artifact_effectTypes_main.json";
inline constexpr const char* artifact_sub = "artifacts/artifact_effectTypes_sub.json";
// Monstres
inline constexpr const char* monster_attributes = "monsters/monster_attributes.json";
inline constexpr const char* monster_archetypes = "monsters/monster_archetypes.json";
inline constexpr const char* skills_overrides = "monsters/skills_overrides.json";
// Compte (bonus d'invocateur, tours)
inline constexpr const char* account_bonuses = "account/account_bonuses.json";
inline constexpr const char* summoner_skills = "account/summoner_skills.json";
// Combat (formules, ATB, hypothèses)
inline constexpr const char* mechanics = "combat/mechanics.json";

}  // namespace swcore::data_files
