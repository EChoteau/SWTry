#pragma once

#include "swcore/locks.hpp"
#include "swcore/reference_db.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace swcore {

// "roll" d'une meule (ou gemme) simulée, g0 = meule actuelle de la ligne :
// Min / Max : valeur min / max de la rareté, nouvelle meule = max(g0, valeur) ;
// Exact : valeur N imposée (dans [min, max] de la rareté), même si N < g0.
enum class GrindstoneMode {
    Off,
    Min,
    Max,
    Exact,
};

// Niveau auquel la stat principale des runes est projetée pour le calcul.
// 0 = "current" (runes telles quelles), sinon 12 ou 15.
constexpr int kProjectionCurrent = 0;

struct PoolConfig {
    std::optional<std::uint64_t> target_unit_id;
    std::vector<std::uint64_t> locked_unit_ids;
    bool inventory_only = false;
    int minimum_rune_level = 0;
    int minimum_stars = 0;
    int projection_level = 15;

    friend bool operator==(const PoolConfig&, const PoolConfig&) = default;
};

struct SetRequirement {
    int set_id = 0;
    int pieces = 0;

    friend bool operator==(const SetRequirement&, const SetRequirement&) = default;
};

struct SetScenario {
    std::string name;
    std::vector<SetRequirement> sets;
    bool any_set = false;

    friend bool operator==(const SetScenario&, const SetScenario&) = default;
};

// Nom d'un scénario généré depuis ses sets : "Fatal 4 + Blade 2" (ordre de la
// config) ; "Tous sets" pour any_set. Sert quand aucun nom n'a été saisi.
std::string scenario_label(const SetScenario& scenario,
                           const std::unordered_map<int, std::string>& set_names);
// Nom affiché : le nom saisi s'il existe, sinon le nom généré.
std::string scenario_display_name(const SetScenario& scenario,
                                  const std::unordered_map<int, std::string>& set_names);

struct MainStatSelection {
    bool all = false;
    std::vector<int> stat_ids;

    friend bool operator==(const MainStatSelection&, const MainStatSelection&) = default;
};

struct StatConstraint {
    std::optional<double> minimum;
    std::optional<double> maximum;

    friend bool operator==(const StatConstraint&, const StatConstraint&) = default;
};

struct GrindstoneRule {
    GrindstoneMode mode = GrindstoneMode::Off;
    int rarity = 0;
    int exact = 0;  // valeur imposée en mode Exact

    friend bool operator==(const GrindstoneRule&, const GrindstoneRule&) = default;
};

struct ObjectiveConfig {
    std::string mode = "optimize";
    std::map<std::string, double> weights;
    std::map<std::string, double> caps;
    std::vector<std::string> sort_metrics;

    friend bool operator==(const ObjectiveConfig&, const ObjectiveConfig&) = default;
};

struct SolverLimits {
    int top_n = 50;  // 0 = tous les builds valides
    std::int64_t time_budget_ms = 0;
    std::uint64_t node_limit = 0;
    int max_builds_per_scenario = 50;
    bool one_build_per_set_composition = false;
    // Mode top_n = 0 : plafond de builds conservés, score minimum optionnel,
    // et nombre de builds détaillés (runes complètes) dans ScenarioReport::builds.
    std::uint64_t max_results = 5'000'000;
    std::optional<double> min_score;
    std::size_t detailed_builds = 50;

    friend bool operator==(const SolverLimits&, const SolverLimits&) = default;
};

// Verrous (réservations) : respect = une rune verrouillée pour un autre monstre
// (ou pour personne) est exclue ; ignore = aucun effet. keep_target_locked :
// les runes verrouillées pour le monstre cible sont figées dans leur slot.
enum class LockMode { Respect, Ignore };

struct LockSettings {
    LockMode mode = LockMode::Respect;
    bool keep_target_locked = false;

    friend bool operator==(const LockSettings&, const LockSettings&) = default;
};

struct SolverConfig {
    PoolConfig pool;
    std::vector<SetScenario> scenarios;
    std::vector<int> allowed_sets;
    std::array<MainStatSelection, 7> main_stats;
    std::map<std::string, StatConstraint> constraints;
    // Meules simulées : règle par stat meulable (1-6, 8). Vide = désactivé.
    std::map<int, GrindstoneRule> grindstones;
    std::optional<int> max_new_grinds;  // nullopt = illimité
    // Gemmes simulées : stats autorisées pour la gemme (enchanted_gem.json).
    std::map<int, GrindstoneRule> gems;
    std::optional<int> max_new_gems;  // nullopt = illimité
    // false : une rune déjà gemmée ne reçoit pas de nouvelle gemme.
    bool gem_regem_existing = true;
    // Meules/gemmes simulées seulement sur les runes de ce niveau réel ou plus.
    int craft_min_level = 12;
    LockSettings locks;
    // Table des verrous (CLI : --locks ; GUI : data/user/locks.json). Pas dans le
    // JSON de config : la table vit à part et survit aux nouveaux exports.
    LockTable lock_table;
    ObjectiveConfig objective;
    SolverLimits limits;
    // Erreurs de configuration bloquantes (ex. valeur exacte hors plage).
    std::vector<std::string> errors;

    static SolverConfig load(const std::filesystem::path& path,
                             const std::filesystem::path& mapping_directory,
                             WarningSink warnings = {});
    // Même parser que load(), depuis un texte JSON.
    static SolverConfig parse(const std::string& json_text,
                              const std::filesystem::path& mapping_directory,
                              WarningSink warnings = {});
    // JSON relu à l'identique par load()/parse() (aller-retour exact, hors
    // allowed_sets, errors et limits.detailed_builds qui n'ont pas de clé JSON).
    std::string to_json() const;

    friend bool operator==(const SolverConfig&, const SolverConfig&) = default;
};

}  // namespace swcore
