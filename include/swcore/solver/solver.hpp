#pragma once

#include "swcore/account/account.hpp"
#include "swcore/solver/solver_config.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <optional>
#include <stop_token>
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
    Rune rune;              // rune réelle (en jeu)
    double efficiency = 0;  // efficacité réelle
    // Modifications simulées (meules, gemme) et efficacité après simulation.
    std::vector<AppliedGrindstone> grinds;
    std::vector<AppliedGem> gems;
    std::optional<double> simulated_efficiency;
};

struct BuildReport {
    std::uint64_t monster_unit_id = 0;
    int monster_master_id = 0;
    int monster_level = 0;
    std::string scenario;
    int rank = 0;
    double score = 0;
    // Niveau de projection utilisé si au moins une rune du build a été projetée
    // (0 sinon). Les stats sont projetées ; les runes affichées sont réelles.
    int projection_level = 0;
    bool simulated_craft = false;  // stats incluant des meules/gemmes simulées
    std::array<StatReport, 8> stats{};
    DerivedMetrics metrics;
    std::array<RuneView, 6> runes{};
    std::array<StatReport, 8> equipped_stats{};
};

struct SolverBuild {
    std::array<Rune, 6> runes{};         // runes utilisées pour le calcul (projetées)
    std::array<Rune, 6> actual_runes{};  // runes réelles (affichage, en jeu)
    std::array<int, 6> variants{};       // variante de craft de chaque rune
    StatBlock stats;
    DerivedMetrics metrics;
    double score = 0;
    double efficiency = 0;  // efficacité moyenne du build (tie-break)
    int grind_cost = 0;     // nouvelles meules simulées (tie-break : moins = mieux)
    int gem_cost = 0;       // nouvelles gemmes simulées (tie-break : moins = mieux)
    int projection_level = 0;  // > 0 si au moins une rune a été projetée
    std::string scenario;
};

// Build valide sous forme compacte (mode top_n = 0) : ids de runes par slot et
// variante de meules de chaque rune.
struct CompactBuild {
    std::array<std::uint64_t, 6> rune_ids{};
    std::array<int, 6> variants{};
    StatValues total;
    double score = 0;
    double effective_hp = 0;
    double efficiency = 0;
    int grind_cost = 0;
    int gem_cost = 0;
};

struct ScenarioReport {
    std::string name;
    std::vector<SolverBuild> builds;
    std::uint64_t nodes = 0;
    std::size_t candidates_before = 0;
    std::size_t candidates_after = 0;
    std::uint64_t raw_combinations = 0;
    std::int64_t elapsed_ms = 0;
    // Coupes de la recherche, par cause (branches abandonnées sans être explorées).
    std::uint64_t cuts_sets = 0;
    std::uint64_t cuts_objective = 0;
    std::uint64_t cuts_constraints = 0;
    std::uint64_t cuts_craft_budget = 0;  // max_new_grinds dépassé
    std::uint64_t lock_excluded = 0;      // runes exclues par verrou (tous slots)
    std::string skipped_reason;           // scénario impossible (slot figé)
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
        std::size_t locked_out = 0;  // exclues par verrou
    };
    std::array<SlotFilter, 6> slot_filters{};
    std::map<std::string, double> best_constraint_values;
    // Valeur maximale théorique de chaque contrainte minimale avec ce pool
    // (borne supérieure, chaque slot pris au mieux indépendamment).
    std::map<std::string, double> constraint_upper_bounds;
    // Mode top_n = 0 : tous les builds valides triés, leur nombre, et le
    // drapeau de plafond (limits.max_results atteint).
    bool all_builds_mode = false;
    std::vector<CompactBuild> all_builds;
    std::uint64_t valid_builds = 0;
    bool truncated = false;
    bool incomplete = false;
    std::string validation_error;
};

struct SolverReport {
    std::vector<ScenarioReport> scenarios;
    std::vector<std::string> limiting_constraints;
};

// Variante de craft d'une rune (meules et/ou gemme simulées) et ses coûts.
struct RuneCraftVariant {
    Rune rune;
    int grinds = 0;
    int gems = 0;
};

// Toutes les variantes de craft d'une rune selon la config (avant dominance),
// la rune telle quelle en premier. Pour vérification et diagnostic.
std::vector<RuneCraftVariant> list_craft_variants(
    const Rune& rune, const SolverConfig& config,
    const std::filesystem::path& mapping_directory);

// Runes candidates par slot (1..6) : mêmes filtres de pool que le solveur
// (verrous, monstres exclus, inventaire, niveau, étoiles, slots figés, stat
// principale, sets d'au moins
// un scénario), sans recherche.
std::array<std::size_t, 6> count_candidate_runes(const std::vector<Rune>& pool,
                                                 const SolverConfig& config);

// Runes exclues par les verrous pour le monstre cible de la config (résumé GUI).
std::size_t count_lock_excluded_runes(const std::vector<Rune>& pool,
                                      const SolverConfig& config);

// Détail complet d'un build compact (ids de runes + variantes de craft),
// recalculé avec la fonction de stats pure. nullopt si une rune est absente.
std::optional<SolverBuild> expand_build(const AccountMonster& target,
                                        const std::vector<Rune>& pool,
                                        const SetEffects& effects,
                                        const SolverConfig& config,
                                        const std::filesystem::path& mapping_directory,
                                        const CompactBuild& compact,
                                        const std::string& scenario);

// Nombre de meilleurs builds conservés par scénario (top_n > 0) : top_n,
// plafonné par max_builds_per_scenario.
std::size_t kept_builds_limit(const SolverLimits& limits);

BuildReport make_build_report(const AccountMonster& target,
                              const SolverBuild& build, int rank = 1);

// Progression lisible pendant une recherche (depuis un autre thread).
struct SolveProgress {
    std::atomic<std::int64_t> elapsed_ms{0};
    std::atomic<std::uint64_t> builds_found{0};
    std::atomic<bool> incomplete{false};
};

// Contrôle d'une recherche : annulation (stop_token) et progression.
// Une recherche annulée garde ses résultats partiels, marqués incomplets.
struct SolveControl {
    std::stop_token stop;
    SolveProgress* progress = nullptr;
};

SolverReport solve_runes(
    const AccountMonster& target,
    const std::vector<Rune>& pool,
    const SetEffects& set_effects,
    const SolverConfig& config,
    const std::filesystem::path& mapping_directory,
    WarningSink warnings = {});

SolverReport solve_runes(
    const AccountMonster& target,
    const std::vector<Rune>& pool,
    const SetEffects& set_effects,
    const SolverConfig& config,
    const std::filesystem::path& mapping_directory,
    WarningSink warnings,
    const SolveControl& control);

}  // namespace swcore
