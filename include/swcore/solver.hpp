#pragma once

#include "swcore/account.hpp"
#include "swcore/solver_config.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace swcore {

struct DerivedMetrics {
    double effective_hp = 0;
    double effective_hp_broken_def = 0;
    double expected_damage = 0;
    double rune_efficiency = 0;
    bool rune_efficiency_available = false;
};

DerivedMetrics calculate_derived_metrics(const StatValues& stats,
                                         double rune_efficiency = 0);
double calculate_rune_efficiency(const Rune& rune);
double calculate_build_efficiency(const std::array<Rune, 6>& runes);

struct StatReport {
    int white = 0;
    int green = 0;
    int total = 0;
};

struct RuneView {
    Rune rune;
    double efficiency = 0;
};

struct BuildReport {
    std::uint64_t monster_unit_id = 0;
    int monster_master_id = 0;
    int monster_level = 0;
    std::string scenario;
    int rank = 0;
    double score = 0;
    std::array<StatReport, 8> stats{};
    DerivedMetrics metrics;
    std::array<RuneView, 6> runes{};
    std::array<StatReport, 8> equipped_stats{};
};

struct SolverBuild {
    std::array<Rune, 6> runes{};
    StatBlock stats;
    DerivedMetrics metrics;
    double score = 0;
    std::string scenario;
};

struct ScenarioReport {
    std::string name;
    std::vector<SolverBuild> builds;
    std::uint64_t nodes = 0;
    std::size_t candidates_before = 0;
    std::size_t candidates_after = 0;
    std::uint64_t raw_combinations = 0;
    std::int64_t elapsed_ms = 0;
    struct SlotFilter {
        std::size_t pool = 0;
        std::size_t inventory = 0;
        std::size_t target_equipped = 0;
        std::size_t other_equipped = 0;
        std::size_t after_level = 0;
        std::size_t after_stars = 0;
        std::size_t after_main_stat = 0;
        std::size_t after_set = 0;
        std::size_t after_pruning = 0;
    };
    std::array<SlotFilter, 6> slot_filters{};
    std::map<std::string, double> best_constraint_values;
    bool incomplete = false;
    std::string validation_error;
};

struct SolverReport {
    std::vector<ScenarioReport> scenarios;
    std::vector<std::string> limiting_constraints;
};

BuildReport make_build_report(const AccountMonster& target,
                              const SolverBuild& build, int rank = 1);

SolverReport solve_runes(
    const AccountMonster& target,
    const std::vector<Rune>& pool,
    const SetEffects& set_effects,
    const SolverConfig& config,
    const std::filesystem::path& mapping_directory,
    WarningSink warnings = {});

}  // namespace swcore
