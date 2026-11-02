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

enum class GrindstoneMode {
    Off,
    Min,
    Max,
};

struct PoolConfig {
    std::optional<std::uint64_t> target_unit_id;
    std::vector<std::uint64_t> locked_unit_ids;
    bool inventory_only = false;
    int minimum_rune_level = 0;
    int minimum_stars = 0;
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
};

struct ObjectiveConfig {
    std::string mode = "optimize";
    std::map<std::string, double> weights;
    std::map<std::string, double> caps;
    std::vector<std::string> sort_metrics;
};

struct SolverLimits {
    int top_n = 50;
    std::int64_t time_budget_ms = 0;
    std::uint64_t node_limit = 0;
    int max_builds_per_scenario = 50;
    bool one_build_per_set_composition = false;
};

struct SolverConfig {
    PoolConfig pool;
    std::vector<SetScenario> scenarios;
    std::vector<int> allowed_sets;
    std::array<MainStatSelection, 7> main_stats;
    std::map<std::string, StatConstraint> constraints;
    std::map<int, GrindstoneRule> grindstones;
    bool allow_extended_grindstone_stats = false;
    ObjectiveConfig objective;
    SolverLimits limits;

    static SolverConfig load(const std::filesystem::path& path,
                             const std::filesystem::path& mapping_directory,
                             WarningSink warnings = {});
};

}  // namespace swcore
