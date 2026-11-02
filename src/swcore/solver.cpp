#include "swcore/solver.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace swcore {
namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct Candidate {
    Rune rune;
};

struct SearchState {
    const AccountMonster& target;
    const SetEffects& set_effects;
    const SolverConfig& config;
    std::vector<std::vector<Candidate>> candidates;
    std::array<int, 6> slot_order{};
    SetScenario scenario;
    ScenarioReport report;
    std::array<Rune, 6> selected{};
    Clock::time_point started;
    Clock::time_point deadline;
    WarningSink warnings;
    Clock::time_point next_progress;
    bool stopped = false;
};

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

bool locked(const Rune& rune, const PoolConfig& pool) {
    return std::find(pool.locked_unit_ids.begin(), pool.locked_unit_ids.end(),
                     rune.source_unit_id) != pool.locked_unit_ids.end();
}

bool main_stat_allowed(const Rune& rune, int slot,
                       const SolverConfig& config) {
    if (slot == 1 || slot == 3 || slot == 5) return true;
    const auto& selection = config.main_stats[slot];
    if (selection.all || selection.stat_ids.empty()) return true;
    return rune.primary &&
           std::find(selection.stat_ids.begin(), selection.stat_ids.end(),
                     rune.primary->stat_id) != selection.stat_ids.end();
}

bool set_allowed(const Rune& rune, const SetScenario& scenario) {
    if (scenario.any_set || scenario.sets.empty()) return true;
    return std::any_of(
        scenario.sets.begin(), scenario.sets.end(),
        [&](const SetRequirement& requirement) {
            return rune.set_id == requirement.set_id;
        });
}

bool set_scenario_matches(const std::array<Rune, 6>& runes,
                          const SetScenario& scenario) {
    if (scenario.any_set) return true;
    std::map<int, int> counts;
    for (const auto& rune : runes) ++counts[rune.set_id];
    for (const auto& requirement : scenario.sets)
        if (counts[requirement.set_id] < requirement.pieces) return false;
    return true;
}

Rune apply_grindstones(Rune rune, const SolverConfig& config,
                       const std::filesystem::path& mapping_directory,
                       WarningSink warnings) {
    if (rune.source_name.empty()) {
        rune.source_name =
            rune.occupancy == RuneOccupancy::Inventory ? "inventaire"
                                                       : "monstre";
    }
    std::ifstream input(mapping_directory / "grindstone.json");
    if (!input) {
        warn(warnings, "Solver: unable to open grindstone mapping");
        return rune;
    }
    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        warn(warnings, std::string("Solver: invalid grindstone mapping: ") +
                          error.what());
        return rune;
    }
    for (const auto& [id_text, rule] : config.grindstones) {
        if (rule.mode == GrindstoneMode::Off) continue;
        const auto mapping = root.find(std::to_string(id_text));
        if (mapping == root.end() || !mapping->contains("range")) continue;
        const auto rarity = mapping->at("range").find(std::to_string(rule.rarity));
        if (rarity == mapping->at("range").end()) continue;
        int value = rule.mode == GrindstoneMode::Min
                        ? rarity->at("min").get<int>()
                        : rarity->at("max").get<int>();
        const int stat_id = id_text;
        for (auto& secondary : rune.secondary) {
            if (secondary.value.stat_id != stat_id) continue;
            value = std::max(value, secondary.grind);
            rune.applied_grindstones.push_back({stat_id, value});
            secondary.grind = value;
        }
    }
    return rune;
}

StatBlock build_stats(const AccountMonster& target,
                      const std::array<Rune, 6>& selected,
                      const SetEffects& effects) {
    AccountMonster candidate = target;
    candidate.runes.assign(selected.begin(), selected.end());
    for (auto& rune : candidate.runes)
        rune.occupancy = RuneOccupancy::Equipped;
    return calculate_monster_stats(candidate, effects);
}

double metric_value(const SolverBuild& build, const std::string& name) {
    if (name == "hp" || name == "HP" || name == "PV") return build.stats.total.hp;
    if (name == "attack" || name == "ATK" || name == "ATQ")
        return build.stats.total.attack;
    if (name == "defense" || name == "DEF") return build.stats.total.defense;
    if (name == "speed" || name == "SPD" || name == "VIT")
        return build.stats.total.speed;
    if (name == "crit_rate" || name == "CRate")
        return build.stats.total.crit_rate;
    if (name == "crit_damage" || name == "CDmg")
        return build.stats.total.crit_damage;
    if (name == "resistance" || name == "RES") return build.stats.total.resistance;
    if (name == "accuracy" || name == "ACC") return build.stats.total.accuracy;
    if (name == "effective_hp") return build.metrics.effective_hp;
    if (name == "effective_hp_broken_def")
        return build.metrics.effective_hp_broken_def;
    if (name == "expected_damage") return build.metrics.expected_damage;
    if (name == "rune_efficiency") return build.metrics.rune_efficiency;
    return 0;
}

bool constraints_match(const SolverBuild& build,
                       const SolverConfig& config) {
    for (const auto& [name, constraint] : config.constraints) {
        const double value = metric_value(build, name);
        if (constraint.minimum && value < *constraint.minimum) return false;
        if (constraint.maximum && value > *constraint.maximum) return false;
    }
    return true;
}

bool validate_returned_build(const SolverBuild& build,
                             const SolverConfig& config) {
    return constraints_match(build, config);
}

double score_build(SolverBuild& build, const ObjectiveConfig& objective) {
    double score = 0;
    for (const auto& [name, weight] : objective.weights) {
        double value = metric_value(build, name);
        const auto cap = objective.caps.find(name);
        if (cap != objective.caps.end()) value = std::min(value, cap->second);
        score += value * weight;
    }
    return score;
}

bool better(const SolverBuild& left, const SolverBuild& right,
            const ObjectiveConfig& objective) {
    if (left.score != right.score) return left.score > right.score;
    for (const auto& metric : objective.sort_metrics) {
        const double l = metric_value(left, metric);
        const double r = metric_value(right, metric);
        if (l != r) return l > r;
    }
    return left.runes[0].rune_id < right.runes[0].rune_id;
}

bool dominates(const Rune& left, const Rune& right,
               const AccountMonster& target, const SetEffects& effects) {
    AccountMonster left_target = target;
    AccountMonster right_target = target;
    left_target.runes = {left};
    right_target.runes = {right};
    left_target.runes[0].occupancy = RuneOccupancy::Equipped;
    right_target.runes[0].occupancy = RuneOccupancy::Equipped;
    const auto left_stats = calculate_monster_stats(left_target, effects).total;
    const auto right_stats = calculate_monster_stats(right_target, effects).total;
    const bool not_worse =
        left_stats.hp >= right_stats.hp &&
        left_stats.attack >= right_stats.attack &&
        left_stats.defense >= right_stats.defense &&
        left_stats.speed >= right_stats.speed &&
        left_stats.crit_rate >= right_stats.crit_rate &&
        left_stats.crit_damage >= right_stats.crit_damage &&
        left_stats.resistance >= right_stats.resistance &&
        left_stats.accuracy >= right_stats.accuracy;
    const bool strictly_better =
        left_stats.hp > right_stats.hp ||
        left_stats.attack > right_stats.attack ||
        left_stats.defense > right_stats.defense ||
        left_stats.speed > right_stats.speed ||
        left_stats.crit_rate > right_stats.crit_rate ||
        left_stats.crit_damage > right_stats.crit_damage ||
        left_stats.resistance > right_stats.resistance ||
        left_stats.accuracy > right_stats.accuracy;
    return not_worse && strictly_better;
}

bool check_budget(SearchState& state) {
    if (state.config.limits.time_budget_ms <= 0) return false;
    const auto now = Clock::now();
    if (now >= state.deadline) {
        state.stopped = true;
        state.report.incomplete = true;
        return true;
    }
    if (now >= state.next_progress) {
        if (state.warnings) {
            state.warnings("[solver-progress] scenario=" + state.report.name +
                           " nodes=" + std::to_string(state.report.nodes) +
                           " score=" +
                           (state.report.builds.empty()
                                ? std::string{"-"}
                                : std::to_string(state.report.builds.front().score)) +
                           " elapsed_ms=" +
                           std::to_string(std::chrono::duration_cast<
                                          std::chrono::milliseconds>(
                                          now - state.started)
                                              .count()));
        }
        state.next_progress = now + std::chrono::seconds(1);
    }
    return false;
}

bool prune_dominated(std::vector<Candidate>& candidates,
                     const AccountMonster& target,
                     const SetEffects& effects, SearchState& state) {
    std::vector<Candidate> kept;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if ((i % 10000) == 0 && check_budget(state)) return false;
        bool dominated_candidate = false;
        for (std::size_t j = 0; j < candidates.size(); ++j) {
            if (i == j || candidates[i].rune.set_id != candidates[j].rune.set_id ||
                !candidates[i].rune.primary || !candidates[j].rune.primary ||
                candidates[i].rune.primary->stat_id !=
                    candidates[j].rune.primary->stat_id)
                continue;
            if (dominates(candidates[j].rune, candidates[i].rune, target,
                          effects)) {
                dominated_candidate = true;
                break;
            }
        }
        if (!dominated_candidate) kept.push_back(candidates[i]);
    }
    candidates = std::move(kept);
    return true;
}

void insert_build(SearchState& state, SolverBuild build) {
    const auto recalculated =
        build_stats(state.target, build.runes, state.set_effects);
    if (recalculated.total.hp != build.stats.total.hp ||
        recalculated.total.attack != build.stats.total.attack ||
        recalculated.total.defense != build.stats.total.defense ||
        recalculated.total.speed != build.stats.total.speed ||
        recalculated.total.crit_rate != build.stats.total.crit_rate ||
        recalculated.total.crit_damage != build.stats.total.crit_damage ||
        recalculated.total.resistance != build.stats.total.resistance ||
        recalculated.total.accuracy != build.stats.total.accuracy) {
        state.report.validation_error =
            "internal stat recalculation mismatch";
        state.stopped = true;
        return;
    }
    build.stats = recalculated;
    build.metrics = calculate_derived_metrics(build.stats.total);
    for (const auto& [name, constraint] : state.config.constraints) {
        const double value = metric_value(build, name);
        (void)constraint;
        auto& best = state.report.best_constraint_values[name];
        if (best == 0 || value > best) best = value;
    }
    if (!constraints_match(build, state.config)) return;
    build.score = score_build(build, state.config.objective);
    state.report.builds.push_back(std::move(build));
    std::stable_sort(state.report.builds.begin(), state.report.builds.end(),
                     [&](const SolverBuild& left, const SolverBuild& right) {
                         return better(left, right, state.config.objective);
                     });
    const auto limit = static_cast<std::size_t>(
        std::max(state.config.limits.top_n,
                 state.config.limits.max_builds_per_scenario));
    if (state.report.builds.size() > limit)
        state.report.builds.resize(limit);
}

void search(SearchState& state, std::size_t depth) {
    if (state.stopped) return;
    ++state.report.nodes;
    if ((state.report.nodes % 10000) == 0 && check_budget(state)) return;
    if (state.config.limits.node_limit &&
        state.report.nodes >= state.config.limits.node_limit) {
        state.stopped = true;
        return;
    }
    check_budget(state);
    if (state.stopped) return;
    if (depth == 6) {
        if (!set_scenario_matches(state.selected, state.scenario)) {
            return;
        }
        const int remaining = 6 - static_cast<int>(depth);
        std::map<int, int> placed;
        for (std::size_t i = 0; i < depth; ++i)
            ++placed[state.selected[state.slot_order[i]].set_id];
        for (const auto& requirement : state.scenario.sets) {
            const int count = placed[requirement.set_id];
            if (count > requirement.pieces ||
                count + remaining < requirement.pieces)
                return;
        }
        SolverBuild build;
        build.runes = state.selected;
        build.scenario = state.report.name;
        build.stats = build_stats(state.target, state.selected, state.set_effects);
        build.metrics = calculate_derived_metrics(build.stats.total);
        insert_build(state, std::move(build));
        return;
    }
    for (const auto& candidate : state.candidates[depth]) {
        bool duplicate = false;
        for (std::size_t i = 0; i < depth; ++i)
            duplicate |= state.selected[state.slot_order[i]].rune_id ==
                         candidate.rune.rune_id;
        if (duplicate) continue;
        const int slot = state.slot_order[depth];
        state.selected[slot] = candidate.rune;
        search(state, depth + 1);
        if (state.stopped) return;
    }
}

}  // namespace

DerivedMetrics calculate_derived_metrics(const StatValues& stats,
                                         double rune_efficiency) {
    DerivedMetrics result;
    result.effective_hp =
        std::floor(stats.hp * (1140.0 + 3.5 * stats.defense) / 1000.0);
    result.effective_hp_broken_def =
        std::floor(stats.hp * (1140.0 + 1.05 * stats.defense) / 1000.0);
    result.expected_damage = std::floor(
        stats.attack * (1.0 + std::min(stats.crit_rate, 100) / 100.0 *
                                   stats.crit_damage / 100.0));
    result.rune_efficiency = rune_efficiency;
    // The exact SW Optimizer efficiency formula is intentionally not guessed.
    // It becomes available when its source formula is added to the mappings.
    result.rune_efficiency_available = false;
    return result;
}

namespace {

double max_substat(int stat_id) {
    switch (stat_id) {
    case 1: return 375;
    case 2:
    case 4:
    case 6:
    case 11:
    case 12: return 8;
    case 3: return 20;
    case 5: return 20;
    case 8:
    case 9: return 6;
    case 10: return 7;
    default: return 0;
    }
}

double efficiency_component(int stat_id, int value, bool innate) {
    const double maximum = max_substat(stat_id);
    if (maximum <= 0) return 0;
    return static_cast<double>(value) /
           (5.0 * (innate && stat_id == 3 ? 40.0 : maximum));
}

}  // namespace

double calculate_rune_efficiency(const Rune& rune) {
    double value = rune.primary ? 1.0 : 0.0;
    if (rune.innate)
        value += efficiency_component(rune.innate->stat_id,
                                      rune.innate->value, true);
    for (const auto& substat : rune.secondary)
        value += efficiency_component(
            substat.value.stat_id, substat.value.value + substat.grind, false);
    return std::floor(value / 2.8 * 10000.0) / 100.0;
}

double calculate_build_efficiency(const std::array<Rune, 6>& runes) {
    double total = 0;
    for (const auto& rune : runes) total += calculate_rune_efficiency(rune);
    return std::floor(total / 6.0 * 100.0) / 100.0;
}

int stat_at(const StatValues& stats, std::size_t index) {
    switch (index) {
    case 0: return stats.hp;
    case 1: return stats.attack;
    case 2: return stats.defense;
    case 3: return stats.speed;
    case 4: return stats.crit_rate;
    case 5: return stats.crit_damage;
    case 6: return stats.resistance;
    case 7: return stats.accuracy;
    default: return 0;
    }
}

BuildReport make_build_report(const AccountMonster& target,
                              const SolverBuild& build, int rank) {
    BuildReport report;
    report.monster_unit_id = target.unit_id;
    report.monster_master_id = target.unit_master_id;
    report.monster_level = target.level;
    report.scenario = build.scenario;
    report.rank = rank;
    report.score = build.score;
    for (std::size_t i = 0; i < report.stats.size(); ++i) {
        const int white = stat_at(build.stats.white, i);
        const int total = stat_at(build.stats.total, i);
        report.stats[i] = {white, total - white, total};
    }
    report.metrics = build.metrics;
    report.metrics.rune_efficiency = calculate_build_efficiency(build.runes);
    report.metrics.rune_efficiency_available = true;
    for (std::size_t i = 0; i < report.runes.size(); ++i)
        report.runes[i] = {build.runes[i],
                           calculate_rune_efficiency(build.runes[i])};
    for (std::size_t i = 0; i < report.equipped_stats.size(); ++i) {
        const int white = stat_at(target.stats.white, i);
        const int total = stat_at(target.stats.total, i);
        report.equipped_stats[i] = {white, total - white, total};
    }
    return report;
}

SolverReport solve_runes(const AccountMonster& target,
                         const std::vector<Rune>& pool,
                         const SetEffects& set_effects,
                         const SolverConfig& config,
                         const std::filesystem::path& mapping_directory,
                         WarningSink warnings) {
    SolverReport report;
                         std::vector<SetScenario> scenarios = config.scenarios;
                         if (scenarios.empty() && !config.allowed_sets.empty()) {
                             for (const int primary : config.allowed_sets) {
                                 scenarios.push_back(
                                     {"set " + std::to_string(primary) +
                                          " + rien",
                                      {{primary, 4}}, false});
                                 for (const int secondary : config.allowed_sets) {
                                     if (secondary == primary) continue;
                                     scenarios.push_back(
                                         {"set " + std::to_string(primary) +
                                              " + set " +
                                              std::to_string(secondary),
                                          {{primary, 4}, {secondary, 2}},
                                          false});
                                 }
                             }
                             for (std::size_t i = 0; i < config.allowed_sets.size();
                                  ++i)
                                 for (std::size_t j = i + 1;
                                      j < config.allowed_sets.size(); ++j)
                                     for (std::size_t k = j + 1;
                                          k < config.allowed_sets.size(); ++k)
                                         scenarios.push_back(
                                             {"2 + 2 + 2",
                                              {{config.allowed_sets[i], 2},
                                               {config.allowed_sets[j], 2},
                                               {config.allowed_sets[k], 2}},
                                              false});
                         }
                         if (scenarios.empty())
                             warn(warnings,
                                  "Solver: no set scenarios configured; no search launched");
    for (const auto& scenario : scenarios) {
        if (scenario.any_set)
            warn(warnings,
                 "Solver: any_set scenario explores the unrestricted set pool "
                 "and is bounded only by the configured time limit");
        const auto started = Clock::now();
        SearchState state{target, set_effects, config, {}, {}, scenario, {},
                          {}, started,
                          started + std::chrono::milliseconds(
                                        std::max<std::int64_t>(
                                            0, config.limits.time_budget_ms)),
                          warnings, started + std::chrono::seconds(1), false};
        state.report.name = scenario.name.empty() ? "scenario" : scenario.name;
        for (int slot = 1; slot <= 6; ++slot) {
            std::vector<Candidate> candidates;
            auto& diagnostic = state.report.slot_filters[static_cast<std::size_t>(slot - 1)];
            for (const auto& source : pool) {
                if (source.slot != slot) continue;
                ++diagnostic.pool;
                if (source.occupancy == RuneOccupancy::Inventory)
                    ++diagnostic.inventory;
                else if (source.source_unit_id == target.unit_id)
                    ++diagnostic.target_equipped;
                else
                    ++diagnostic.other_equipped;
                if (locked(source, config.pool) ||
                    (config.pool.inventory_only &&
                     source.occupancy != RuneOccupancy::Inventory))
                    continue;
                if (source.upgrade_level < config.pool.minimum_rune_level)
                    continue;
                ++diagnostic.after_level;
                if (source.stars < config.pool.minimum_stars) continue;
                ++diagnostic.after_stars;
                if (!main_stat_allowed(source, slot, config)) continue;
                ++diagnostic.after_main_stat;
                if (!set_allowed(source, scenario)) continue;
                ++diagnostic.after_set;
                candidates.push_back({apply_grindstones(source, config,
                                                        mapping_directory,
                                                        warnings)});
            }
            state.report.candidates_before += diagnostic.after_set;
            if (state.report.raw_combinations == 0)
                state.report.raw_combinations = 1;
            state.report.raw_combinations *= candidates.size();
            if (!prune_dominated(candidates, target, set_effects, state))
                break;
            std::stable_sort(candidates.begin(), candidates.end(),
                             [&](const Candidate& left, const Candidate& right) {
                                 SolverBuild l;
                                 SolverBuild r;
                                 std::array<Rune, 6> left_runes{};
                                 std::array<Rune, 6> right_runes{};
                                 left_runes[0] = left.rune;
                                 right_runes[0] = right.rune;
                                 l.stats = build_stats(target, left_runes,
                                                       set_effects);
                                 r.stats = build_stats(target, right_runes,
                                                       set_effects);
                                 l.metrics = calculate_derived_metrics(l.stats.total);
                                 r.metrics = calculate_derived_metrics(r.stats.total);
                                 l.runes[0] = left.rune;
                                 r.runes[0] = right.rune;
                                 const double ls = score_build(l, config.objective);
                                 const double rs = score_build(r, config.objective);
                                 if (ls != rs) return ls > rs;
                                 return left.rune.rune_id < right.rune.rune_id;
                             });
            diagnostic.after_pruning = candidates.size();
            state.report.candidates_after += diagnostic.after_pruning;
            state.candidates.push_back(std::move(candidates));
        }
        if (state.stopped) {
            state.report.elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    Clock::now() - state.started)
                    .count();
            report.scenarios.push_back(std::move(state.report));
            if (warnings) {
                const auto& completed = report.scenarios.back();
                warnings("[solver-scenario] " + completed.name +
                         " candidates=" +
                         std::to_string(completed.candidates_before) +
                         "->" + std::to_string(completed.candidates_after) +
                         " nodes=" + std::to_string(completed.nodes) +
                         " elapsed_ms=" + std::to_string(completed.elapsed_ms) +
                         (completed.incomplete ? " incomplete" : ""));
            }
            break;
        }
        std::vector<std::size_t> order{0, 1, 2, 3, 4, 5};
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t left, std::size_t right) {
                             return state.candidates[left].size() <
                                    state.candidates[right].size();
                         });
        std::vector<std::vector<Candidate>> reordered(6);
        for (std::size_t i = 0; i < order.size(); ++i)
            reordered[i] = std::move(state.candidates[order[i]]);
        state.candidates = std::move(reordered);
        for (std::size_t i = 0; i < order.size(); ++i)
            state.slot_order[i] = static_cast<int>(order[i]);
        search(state, 0);
        for (const auto& build : state.report.builds) {
            if (!validate_returned_build(build, config)) {
                state.report.validation_error =
                    "returned build violates a configured constraint";
                state.report.builds.clear();
                break;
            }
        }
        state.report.elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - state.started)
                .count();
        if (state.report.builds.empty())
            report.limiting_constraints.push_back(state.report.name);
        report.scenarios.push_back(std::move(state.report));
        if (warnings) {
            const auto& completed = report.scenarios.back();
            warnings("[solver-scenario] " + completed.name +
                     " candidates=" +
                     std::to_string(completed.candidates_before) +
                     "->" + std::to_string(completed.candidates_after) +
                     " nodes=" + std::to_string(completed.nodes) +
                     " elapsed_ms=" + std::to_string(completed.elapsed_ms) +
                     (completed.incomplete ? " incomplete" : ""));
        }
    }
    return report;
}

}  // namespace swcore
