#pragma once

#include "swcore/reference_db.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
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
};

struct SetRequirement {
    int set_id = 0;
    int pieces = 0;
};

struct SetScenario {
    std::string name;
    std::vector<SetRequirement> sets;
    bool any_set = false;
};

struct MainStatSelection {
    bool all = false;
    std::vector<int> stat_ids;
};

struct StatConstraint {
    std::optional<double> minimum;
    std::optional<double> maximum;
};

struct GrindstoneRule {
    GrindstoneMode mode = GrindstoneMode::Off;
    int rarity = 0;
    int exact = 0;  // valeur imposée en mode Exact
};

struct ObjectiveConfig {
    std::string mode = "optimize";
    std::map<std::string, double> weights;
    std::map<std::string, double> caps;
    std::vector<std::string> sort_metrics;
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
    ObjectiveConfig objective;
    SolverLimits limits;
    // Erreurs de configuration bloquantes (ex. valeur exacte hors plage).
    std::vector<std::string> errors;

    static SolverConfig load(const std::filesystem::path& path,
                             const std::filesystem::path& mapping_directory,
                             WarningSink warnings = {});
};

}  // namespace swcore
