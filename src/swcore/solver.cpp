#include "swcore/solver.hpp"

#include "swcore/craft_audit.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace swcore {

std::size_t kept_builds_limit(const SolverLimits& limits) {
    auto limit = static_cast<std::size_t>(std::max(limits.top_n, 1));
    if (limits.max_builds_per_scenario > 0)
        limit = std::min(limit, static_cast<std::size_t>(limits.max_builds_per_scenario));
    return limit;
}

namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct Candidate {
    Rune rune;     // rune utilisée pour le calcul (principale projetée, meules)
    Rune display;  // même rune avec son niveau et sa principale réels
    int cost = 0;      // nombre de nouvelles meules simulées
    int gem_cost = 0;  // nouvelle gemme simulée (0 ou 1)
    int variant = 0;   // variante de craft (0 = rune telle quelle)
};

// Hypothèses de craft, isolées pour être changées facilement.
// Gemme : garder la stat de la ligne remplacée est autorisé.
constexpr bool kGemMayKeepReplacedStat = true;
// Gemme : la stat de l'innée est interdite (jamais observée dans l'audit).
constexpr bool kGemMayUseInnateStat = false;
// Gemme : la meule de la ligne remplacée est perdue.
constexpr bool kGemDropsGrind = true;

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
    bool all_mode = false;  // limits.top_n == 0
    // Recherche générique : variante choisie par slot et craft engagé.
    // (En fin de structure : SearchState est initialisé par position.)
    std::array<int, 6> selected_variant{};
    int cost_used = 0;  // meules simulées déjà engagées
    int gems_used = 0;  // gemmes simulées déjà engagées
};

bool all_builds_mode(const SolverConfig& config) {
    return config.limits.top_n == 0;
}

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

Rune with_source_name(Rune rune) {
    if (rune.source_name.empty()) {
        rune.source_name =
            rune.occupancy == RuneOccupancy::Inventory ? "inventaire"
                                                       : "monstre";
    }
    return rune;
}

int grind_cost(const Rune& rune) {
    return static_cast<int>(rune.applied_grindstones.size());
}

int gem_cost(const Rune& rune) {
    return static_cast<int>(rune.applied_gems.size());
}

// Budgets de craft (max_new_grinds / max_new_gems) respectés ?
bool within_craft_budget(const SolverConfig& config, int grinds, int gems) {
    return (!config.max_new_grinds || grinds <= *config.max_new_grinds) &&
           (!config.max_new_gems || gems <= *config.max_new_gems);
}

// Sens de chaque stat (PV, ATQ, DEF, VIT, Tx crit., Dgts crit., RES, Préc.)
// pour la dominance : plus haut = mieux, sauf si une contrainte max ou un poids
// négatif porte sur la stat (ou une métrique qui en dépend) : il faut alors
// l'égalité, car plus haut peut être pire.
enum class Direction { Higher, Equal, Ignore };
using Directions = std::array<Direction, 8>;

std::vector<std::size_t> metric_inputs(const std::string& name) {
    if (name == "hp" || name == "HP" || name == "PV") return {0};
    if (name == "attack" || name == "ATK" || name == "ATQ") return {1};
    if (name == "defense" || name == "DEF") return {2};
    if (name == "speed" || name == "SPD" || name == "VIT") return {3};
    if (name == "crit_rate" || name == "CRate") return {4};
    if (name == "crit_damage" || name == "CDmg") return {5};
    if (name == "resistance" || name == "RES") return {6};
    if (name == "accuracy" || name == "ACC") return {7};
    if (name == "effective_hp" || name == "effective_hp_broken_def") return {0, 2};
    if (name == "expected_damage") return {1, 4, 5};
    return {};
}

Directions stat_directions(const SolverConfig& config) {
    Directions directions;
    directions.fill(Direction::Higher);
    const auto mark = [&](const std::string& name) {
        for (const auto index : metric_inputs(name))
            directions[index] = Direction::Equal;
    };
    for (const auto& [name, constraint] : config.constraints)
        if (constraint.maximum) mark(name);
    for (const auto& [name, weight] : config.objective.weights)
        if (weight < 0) mark(name);
    return directions;
}

// Même chose, mais les stats qui n'interviennent nulle part (objectif,
// contraintes, tri) sont ignorées : utilisé entre variantes d'une même rune.
Directions relevant_directions(const SolverConfig& config) {
    Directions directions = stat_directions(config);
    std::array<bool, 8> used{};
    const auto use = [&](const std::string& name) {
        for (const auto index : metric_inputs(name)) used[index] = true;
    };
    for (const auto& [name, constraint] : config.constraints) use(name);
    for (const auto& [name, weight] : config.objective.weights) use(name);
    for (const auto& name : config.objective.sort_metrics) use(name);
    for (std::size_t k = 0; k < 8; ++k)
        if (!used[k]) directions[k] = Direction::Ignore;
    return directions;
}

std::array<int, 8> stat_array(const StatValues& s) {
    return {s.hp, s.attack, s.defense, s.speed, s.crit_rate, s.crit_damage,
            s.resistance, s.accuracy};
}

// left domine right : jamais pire (égal là où le sens l'exige), coûts en
// meules et en gemmes <=, et strictement mieux sur une stat ou moins cher.
bool dominates_with_cost(const StatValues& left, const Candidate& left_cost,
                         const StatValues& right, const Candidate& right_cost,
                         const Directions& directions) {
    if (left_cost.cost > right_cost.cost ||
        left_cost.gem_cost > right_cost.gem_cost)
        return false;
    const auto l = stat_array(left);
    const auto r = stat_array(right);
    bool strictly = left_cost.cost < right_cost.cost ||
                    left_cost.gem_cost < right_cost.gem_cost;
    for (std::size_t k = 0; k < 8; ++k) {
        if (directions[k] == Direction::Ignore) continue;
        if (directions[k] == Direction::Equal) {
            if (l[k] != r[k]) return false;
        } else {
            if (l[k] < r[k]) return false;
            strictly |= l[k] > r[k];
        }
    }
    return strictly;
}

StatValues single_rune_total(const AccountMonster& target, const Rune& rune,
                             const SetEffects& effects) {
    AccountMonster one = target;
    one.runes = {rune};
    one.runes[0].occupancy = RuneOccupancy::Equipped;
    return calculate_monster_stats(one, effects).total;
}

// Valeur simulée selon le roll : Min/Max -> max(actuelle, borne) si la stat
// est conservée (sinon la borne) ; Exact -> N.
int rolled_value(const GrindstoneRule& rule, const std::pair<int, int>& range,
                 std::optional<int> current) {
    switch (rule.mode) {
    case GrindstoneMode::Min:
        return current ? std::max(*current, range.first) : range.first;
    case GrindstoneMode::Max:
        return current ? std::max(*current, range.second) : range.second;
    case GrindstoneMode::Exact: return rule.exact;
    case GrindstoneMode::Off: break;
    }
    return current.value_or(0);
}

const std::pair<int, int>* rule_range(const CraftRanges& ranges, int stat,
                                      const GrindstoneRule& rule) {
    const auto by_stat = ranges.find(stat);
    if (by_stat == ranges.end()) return nullptr;
    const auto range = by_stat->second.find(rule.rarity);
    return range == by_stat->second.end() ? nullptr : &range->second;
}

// Genre (fixe / %) d'une stat posée par gemme : comme le mapping des effets
// (les noms contenant '%' : PV%, ATQ%, DEF%), seules 2, 4 et 6 sont en %.
StatValueKind gem_kind(int stat) {
    return stat == 2 || stat == 4 || stat == 6 ? StatValueKind::Percent
                                               : StatValueKind::Flat;
}

// États "gemme" d'une rune : la rune telle quelle (coût 0), puis chaque gemme
// possible (coût 1) selon les règles de craft.
std::vector<Rune> gem_states(const Rune& rune, const SolverConfig& config,
                             const CraftRanges& ranges) {
    std::vector<Rune> states{rune};
    if (config.gems.empty()) return states;
    int enchanted = -1;
    for (std::size_t j = 0; j < rune.secondary.size(); ++j)
        if (rune.secondary[j].enchanted) enchanted = static_cast<int>(j);
    // gems.regem_existing = false : la ligne enchantée reste telle quelle.
    if (enchanted >= 0 && !config.gem_regem_existing) return states;
    for (std::size_t i = 0; i < rune.secondary.size(); ++i) {
        // Rune déjà gemmée : seule sa ligne enchantée peut être regemmée.
        if (enchanted >= 0 && static_cast<int>(i) != enchanted) continue;
        const auto& line = rune.secondary[i];
        for (const auto& [stat, rule] : config.gems) {
            if (rule.mode == GrindstoneMode::Off) continue;
            const auto* range = rule_range(ranges, stat, rule);
            if (!range) continue;
            if (rune.primary && rune.primary->stat != StatType::None &&
                rune.primary->stat_id == stat)
                continue;
            if (!kGemMayUseInnateStat && rune.innate &&
                rune.innate->stat != StatType::None && rune.innate->stat_id == stat)
                continue;
            bool elsewhere = false;
            for (std::size_t j = 0; j < rune.secondary.size(); ++j)
                elsewhere |= j != i && rune.secondary[j].value.stat_id == stat;
            if (elsewhere) continue;
            const bool same_stat = line.value.stat_id == stat;
            if (same_stat && enchanted < 0 && !kGemMayKeepReplacedStat) continue;
            const int value = rolled_value(
                rule, *range,
                same_stat ? std::optional<int>{line.value.value} : std::nullopt);
            if (same_stat && value == line.value.value) continue;  // aucun gain
            Rune gemmed = rune;
            auto& target_line = gemmed.secondary[i];
            gemmed.applied_gems.push_back(
                {static_cast<int>(i), line.value.stat_id, line.value.value,
                 line.grind, line.enchanted, stat, value, rule.rarity});
            target_line.value = {StatType::Unknown, gem_kind(stat), stat, value};
            target_line.enchanted = true;
            if (kGemDropsGrind) target_line.grind = 0;
            states.push_back(std::move(gemmed));
        }
    }
    return states;
}

// Toutes les variantes de craft d'une rune, avant dominance :
// (sans gemme | chaque gemme possible) x (chaque sous-ensemble de lignes dont la
// meule changerait). Coûts = nombre de meules et de gemmes.
std::vector<Candidate> enumerate_variants(const Rune& rune, int real_level,
                                          const SolverConfig& config,
                                          const CraftRanges& grind_ranges,
                                          const CraftRanges& gem_ranges) {
    const bool craftable = real_level >= config.craft_min_level;
    const auto states = craftable ? gem_states(rune, config, gem_ranges)
                                  : std::vector<Rune>{rune};
    std::vector<Candidate> variants;
    for (std::size_t s = 0; s < states.size(); ++s) {
        const Rune& base = states[s];
        struct Option {
            std::size_t line = 0;
            int value = 0;
            int rarity = 0;
        };
        std::vector<Option> options;
        if (craftable) {
            for (std::size_t j = 0; j < base.secondary.size(); ++j) {
                const auto& line = base.secondary[j];
                const int stat = line.value.stat_id;
                if (!is_grindable_stat(stat)) continue;
                const auto rule = config.grindstones.find(stat);
                if (rule == config.grindstones.end() ||
                    rule->second.mode == GrindstoneMode::Off)
                    continue;
                const auto* range = rule_range(grind_ranges, stat, rule->second);
                if (!range) continue;
                const int value = rolled_value(rule->second, *range, line.grind);
                if (value != line.grind)
                    options.push_back({j, value, rule->second.rarity});
            }
        }
        for (std::size_t mask = 0; mask < (std::size_t{1} << options.size()); ++mask) {
            Candidate candidate;
            candidate.rune = base;
            candidate.gem_cost = s == 0 ? 0 : 1;
            candidate.variant = static_cast<int>(variants.size());
            for (std::size_t bit = 0; bit < options.size(); ++bit) {
                if (!(mask & (std::size_t{1} << bit))) continue;
                auto& line = candidate.rune.secondary[options[bit].line];
                candidate.rune.applied_grindstones.push_back(
                    {line.value.stat_id, options[bit].value, line.grind,
                     options[bit].rarity, static_cast<int>(options[bit].line)});
                line.grind = options[bit].value;
                ++candidate.cost;
            }
            variants.push_back(std::move(candidate));
        }
    }
    return variants;
}

// Variantes de craft retenues pour la recherche : variantes dominées (stats
// pertinentes, coûts) et équivalentes retirées.
std::vector<Candidate> craft_variants(const Rune& rune, int real_level,
                                      const SolverConfig& config,
                                      const CraftRanges& grind_ranges,
                                      const CraftRanges& gem_ranges,
                                      const AccountMonster& target,
                                      const SetEffects& effects,
                                      const Directions& directions) {
    auto variants =
        enumerate_variants(rune, real_level, config, grind_ranges, gem_ranges);
    if (variants.size() == 1) return variants;
    std::vector<StatValues> totals;
    for (const auto& variant : variants)
        totals.push_back(single_rune_total(target, variant.rune, effects));
    // Équivalentes : mêmes coûts et mêmes stats pertinentes (on garde la première).
    const auto equivalent = [&](std::size_t a, std::size_t b) {
        if (variants[a].cost != variants[b].cost ||
            variants[a].gem_cost != variants[b].gem_cost)
            return false;
        const auto l = stat_array(totals[a]);
        const auto r = stat_array(totals[b]);
        for (std::size_t k = 0; k < 8; ++k)
            if (directions[k] != Direction::Ignore && l[k] != r[k]) return false;
        return true;
    };
    std::vector<Candidate> kept;
    for (std::size_t i = 0; i < variants.size(); ++i) {
        bool dominated = false;
        for (std::size_t j = 0; j < variants.size() && !dominated; ++j)
            dominated = j != i && (dominates_with_cost(totals[j], variants[j], totals[i],
                                                       variants[i], directions) ||
                                   (j < i && equivalent(i, j)));
        if (!dominated) kept.push_back(variants[i]);
    }
    return kept;
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

std::array<std::uint64_t, 6> sorted_ids(const std::array<std::uint64_t, 6>& ids) {
    auto result = ids;
    std::sort(result.begin(), result.end());
    return result;
}

std::array<std::uint64_t, 6> rune_ids(const std::array<Rune, 6>& runes) {
    std::array<std::uint64_t, 6> ids{};
    for (std::size_t i = 0; i < 6; ++i) ids[i] = runes[i].rune_id;
    return ids;
}

// Ordre déterministe : score décroissant, métriques de tri demandées, PV eff.
// décroissant, efficacité moyenne décroissante, puis ids de runes triés.
bool better(const SolverBuild& left, const SolverBuild& right,
            const ObjectiveConfig& objective) {
    if (left.score != right.score) return left.score > right.score;
    for (const auto& metric : objective.sort_metrics) {
        const double l = metric_value(left, metric);
        const double r = metric_value(right, metric);
        if (l != r) return l > r;
    }
    if (left.gem_cost != right.gem_cost) return left.gem_cost < right.gem_cost;
    if (left.grind_cost != right.grind_cost)
        return left.grind_cost < right.grind_cost;
    if (left.metrics.effective_hp != right.metrics.effective_hp)
        return left.metrics.effective_hp > right.metrics.effective_hp;
    if (left.efficiency != right.efficiency)
        return left.efficiency > right.efficiency;
    return sorted_ids(rune_ids(left.runes)) < sorted_ids(rune_ids(right.runes));
}

bool better_compact(const CompactBuild& left, const CompactBuild& right) {
    if (left.score != right.score) return left.score > right.score;
    if (left.gem_cost != right.gem_cost) return left.gem_cost < right.gem_cost;
    if (left.grind_cost != right.grind_cost)
        return left.grind_cost < right.grind_cost;
    if (left.effective_hp != right.effective_hp)
        return left.effective_hp > right.effective_hp;
    if (left.efficiency != right.efficiency)
        return left.efficiency > right.efficiency;
    if (left.rune_ids != right.rune_ids)
        return sorted_ids(left.rune_ids) < sorted_ids(right.rune_ids);
    return left.variants < right.variants;
}

// Mode top_n = 0 : enregistre un build valide (score minimum, plafond).
void record_compact(SearchState& state, CompactBuild build) {
    const auto& limits = state.config.limits;
    if (limits.min_score && build.score < *limits.min_score) {
        ++state.report.cuts_objective;
        return;
    }
    state.report.all_builds.push_back(build);
    ++state.report.valid_builds;
    if (limits.max_results &&
        state.report.valid_builds >= limits.max_results) {
        state.report.truncated = true;
        state.stopped = true;
    }
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

// Retire les candidats dominés (même set, même principale ; stats jamais pires
// selon leur sens, coût en meules inférieur ou égal).
bool prune_dominated(std::vector<Candidate>& candidates,
                     const AccountMonster& target,
                     const SetEffects& effects, const Directions& directions,
                     SearchState& state) {
    std::vector<StatValues> totals;
    totals.reserve(candidates.size());
    for (const auto& candidate : candidates)
        totals.push_back(single_rune_total(target, candidate.rune, effects));
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
            if (dominates_with_cost(totals[j], candidates[j], totals[i],
                                    candidates[i], directions)) {
                dominated_candidate = true;
                break;
            }
        }
        if (!dominated_candidate) kept.push_back(candidates[i]);
    }
    candidates = std::move(kept);
    return true;
}

void insert_build(SearchState& state, SolverBuild build,
                  const std::array<int, 6>& variants) {
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
    build.efficiency = calculate_build_efficiency(build.runes);
    build.grind_cost = 0;
    build.gem_cost = 0;
    for (const auto& rune : build.runes) {
        build.grind_cost += grind_cost(rune);
        build.gem_cost += gem_cost(rune);
    }
    if (state.all_mode) {
        record_compact(state, {rune_ids(build.runes), variants, build.stats.total,
                               build.score, build.metrics.effective_hp,
                               build.efficiency, build.grind_cost, build.gem_cost});
        return;
    }
    const auto position = std::upper_bound(
        state.report.builds.begin(), state.report.builds.end(), build,
        [&](const SolverBuild& left, const SolverBuild& right) {
            return better(left, right, state.config.objective);
        });
    state.report.builds.insert(position, std::move(build));
    const auto limit = kept_builds_limit(state.config.limits);
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
        insert_build(state, std::move(build), state.selected_variant);
        return;
    }
    for (const auto& candidate : state.candidates[depth]) {
        bool duplicate = false;
        for (std::size_t i = 0; i < depth; ++i)
            duplicate |= state.selected[state.slot_order[i]].rune_id ==
                         candidate.rune.rune_id;
        if (duplicate) continue;
        if (!within_craft_budget(state.config, state.cost_used + candidate.cost,
                                 state.gems_used + candidate.gem_cost)) {
            ++state.report.cuts_craft_budget;
            continue;
        }
        const int slot = state.slot_order[depth];
        state.selected[slot] = candidate.rune;
        state.selected_variant[slot] = candidate.variant;
        state.cost_used += candidate.cost;
        state.gems_used += candidate.gem_cost;
        search(state, depth + 1);
        state.cost_used -= candidate.cost;
        state.gems_used -= candidate.gem_cost;
        if (state.stopped) return;
    }
}

// ---------------------------------------------------------------------------
// Recherche rapide (scénarios à sets fixes dont les pièces totalisent 6).
// Les stats sont des sommes entières incrémentales ; l'arrondi (plafond du % sur
// le chiffre blanc) n'est appliqué qu'aux feuilles, avec la même formule que
// calculate_stat_block. Chaque build retenu repasse par insert_build, qui le
// recalcule avec la fonction de référence.
// ---------------------------------------------------------------------------

using Vec = std::array<std::int64_t, 8>;

enum class MetricId {
    None, Hp, Attack, Defense, Speed, CritRate, CritDamage, Resistance,
    Accuracy, EffectiveHp, EffectiveHpBrokenDef, ExpectedDamage, RuneEfficiency
};

MetricId metric_id(const std::string& name) {
    if (name == "hp" || name == "HP" || name == "PV") return MetricId::Hp;
    if (name == "attack" || name == "ATK" || name == "ATQ") return MetricId::Attack;
    if (name == "defense" || name == "DEF") return MetricId::Defense;
    if (name == "speed" || name == "SPD" || name == "VIT") return MetricId::Speed;
    if (name == "crit_rate" || name == "CRate") return MetricId::CritRate;
    if (name == "crit_damage" || name == "CDmg") return MetricId::CritDamage;
    if (name == "resistance" || name == "RES") return MetricId::Resistance;
    if (name == "accuracy" || name == "ACC") return MetricId::Accuracy;
    if (name == "effective_hp") return MetricId::EffectiveHp;
    if (name == "effective_hp_broken_def") return MetricId::EffectiveHpBrokenDef;
    if (name == "expected_damage") return MetricId::ExpectedDamage;
    if (name == "rune_efficiency") return MetricId::RuneEfficiency;
    return MetricId::None;
}

bool is_derived(MetricId id) {
    return id == MetricId::EffectiveHp || id == MetricId::EffectiveHpBrokenDef ||
           id == MetricId::ExpectedDamage || id == MetricId::RuneEfficiency;
}

double metric_of(MetricId id, const StatValues& s, const DerivedMetrics& d) {
    switch (id) {
    case MetricId::Hp: return s.hp;
    case MetricId::Attack: return s.attack;
    case MetricId::Defense: return s.defense;
    case MetricId::Speed: return s.speed;
    case MetricId::CritRate: return s.crit_rate;
    case MetricId::CritDamage: return s.crit_damage;
    case MetricId::Resistance: return s.resistance;
    case MetricId::Accuracy: return s.accuracy;
    case MetricId::EffectiveHp: return d.effective_hp;
    case MetricId::EffectiveHpBrokenDef: return d.effective_hp_broken_def;
    case MetricId::ExpectedDamage: return d.expected_damage;
    case MetricId::RuneEfficiency: return d.rune_efficiency;
    default: return 0;
    }
}

struct FastRune {
    Vec v{};          // contribution mise à l'échelle (voir run_fast_search)
    int set_index = 0;
    double efficiency = 0;  // calculate_rune_efficiency de la rune
};

struct FastCriterion {
    MetricId id = MetricId::None;
    std::optional<double> minimum;
    std::optional<double> maximum;
};

struct FastTerm {
    MetricId id = MetricId::None;
    double weight = 0;
    bool capped = false;
    double cap = 0;
};

std::int64_t floor_div100(std::int64_t n) {
    std::int64_t q = n / 100;
    if (n % 100 != 0 && n < 0) --q;
    return q;
}

struct FastContext {
    explicit FastContext(SearchState& state) : s(state) {}
    SearchState& s;
    std::array<std::vector<FastRune>, 6> level;
    std::array<Vec, 7> vmax{};
    std::array<Vec, 7> vmin{};
    std::vector<int> required;
    std::vector<int> count;
    std::vector<std::array<int, 7>> available;
    Vec base{};
    std::vector<FastCriterion> constraints;
    std::vector<FastTerm> terms;
    std::vector<double> best_constraint;
    bool need_derived = false;
    bool use_bounds = false;
    std::size_t limit = 0;
    std::array<std::size_t, 6> choice{};
    int cost_used = 0;  // nouvelles meules engagées dans la branche
    int gems_used = 0;  // nouvelles gemmes engagées dans la branche
};

// Stats totales (hors compétences d'invocateur) à partir de la somme V des
// contributions. PV/ATQ/DEF/VIT : 100*(blanc+artefacts) + V + blanc*sets, plafond
// de /100 ; autres stats : somme directe.
StatValues totals_from(const FastContext& c, const Vec& v) {
    std::array<int, 8> t{};
    for (std::size_t k = 0; k < 8; ++k)
        t[k] = static_cast<int>(k < 4 ? floor_div100(c.base[k] + v[k])
                                      : c.base[k] + v[k]);
    return {t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7]};
}

bool budget_tick(SearchState& st) {
    ++st.report.nodes;
    if ((st.report.nodes % 10000) == 0 && check_budget(st)) return true;
    if (st.config.limits.node_limit &&
        st.report.nodes >= st.config.limits.node_limit) {
        st.stopped = true;
        return true;
    }
    return false;
}

bool list_full(const FastContext& c) {
    return c.s.report.builds.size() >= c.limit;
}

// Score en dessous duquel une branche ne peut plus rien apporter : moins bon
// build conservé (liste pleine) ou score minimum (mode tous les builds).
std::optional<double> score_threshold(const FastContext& c) {
    if (c.s.all_mode) return c.s.config.limits.min_score;
    if (list_full(c)) return c.s.report.builds.back().score;
    return std::nullopt;
}

// 0 = continuer ; 1 = coupe par contrainte ; 2 = coupe par objectif.
int bound_cut(const FastContext& c, const Vec& cur, std::size_t placed) {
    if (!c.use_bounds) return 0;
    Vec hi{};
    Vec lo{};
    for (std::size_t k = 0; k < 8; ++k) {
        hi[k] = cur[k] + c.vmax[placed][k];
        lo[k] = cur[k] + c.vmin[placed][k];
    }
    const StatValues sh = totals_from(c, hi);
    const StatValues sl = totals_from(c, lo);
    const DerivedMetrics dh =
        c.need_derived ? calculate_derived_metrics(sh) : DerivedMetrics{};
    const DerivedMetrics dl =
        c.need_derived ? calculate_derived_metrics(sl) : DerivedMetrics{};
    for (const auto& crit : c.constraints) {
        if (crit.minimum && metric_of(crit.id, sh, dh) < *crit.minimum) return 1;
        if (crit.maximum && metric_of(crit.id, sl, dl) > *crit.maximum) return 1;
    }
    if (const auto threshold = score_threshold(c)) {
        double ub = 0;
        for (const auto& term : c.terms) {
            double v = term.weight >= 0 ? metric_of(term.id, sh, dh)
                                        : metric_of(term.id, sl, dl);
            if (term.capped) v = std::min(v, term.cap);
            ub += v * term.weight;
        }
        if (ub < *threshold - 1e-9) return 2;
    }
    return 0;
}

void fast_leaf(FastContext& c, const Vec& v) {
    SearchState& s = c.s;
    if (budget_tick(s)) return;
    const StatValues total = totals_from(c, v);
    const DerivedMetrics derived =
        c.need_derived ? calculate_derived_metrics(total) : DerivedMetrics{};
    bool valid = true;
    for (std::size_t i = 0; i < c.constraints.size(); ++i) {
        const auto& crit = c.constraints[i];
        const double value = metric_of(crit.id, total, derived);
        auto& best = c.best_constraint[i];
        if (best == 0 || value > best) best = value;
        if (crit.minimum && value < *crit.minimum) valid = false;
        if (crit.maximum && value > *crit.maximum) valid = false;
    }
    if (!valid) {
        ++s.report.cuts_constraints;
        return;
    }
    double score = 0;
    for (const auto& term : c.terms) {
        double value = metric_of(term.id, total, derived);
        if (term.capped) value = std::min(value, term.cap);
        score += value * term.weight;
    }
    std::array<int, 6> variants{};
    for (std::size_t i = 0; i < 6; ++i)
        variants[static_cast<std::size_t>(s.slot_order[i])] =
            s.candidates[i][c.choice[i]].variant;
    if (s.all_mode) {
        CompactBuild compact;
        compact.variants = variants;
        compact.grind_cost = c.cost_used;
        compact.gem_cost = c.gems_used;
        std::array<double, 6> efficiency{};
        for (std::size_t i = 0; i < 6; ++i) {
            const auto slot = static_cast<std::size_t>(s.slot_order[i]);
            compact.rune_ids[slot] = s.candidates[i][c.choice[i]].rune.rune_id;
            efficiency[slot] = c.level[i][c.choice[i]].efficiency;
        }
        // Même ordre de sommation que calculate_build_efficiency (slots 1..6).
        double sum = 0;
        for (const double value : efficiency) sum += value;
        compact.total = total;
        compact.score = score;
        compact.effective_hp = c.need_derived
                                   ? derived.effective_hp
                                   : calculate_derived_metrics(total).effective_hp;
        compact.efficiency = std::floor(sum / 6.0 * 100.0) / 100.0;
        record_compact(s, compact);
        return;
    }
    if (list_full(c) && score < s.report.builds.back().score) {
        ++s.report.cuts_objective;
        return;
    }
    SolverBuild build;
    for (std::size_t i = 0; i < 6; ++i)
        build.runes[static_cast<std::size_t>(s.slot_order[i])] =
            s.candidates[i][c.choice[i]].rune;
    build.scenario = s.report.name;
    build.stats.total = total;
    build.metrics = derived;
    insert_build(s, std::move(build), variants);
}

void fast_descend(FastContext& c, std::size_t d, const Vec& cur) {
    SearchState& s = c.s;
    if (s.stopped || budget_tick(s)) return;
    const auto& runes = c.level[d];
    const bool last = d == 5;
    for (std::size_t i = 0; i < runes.size(); ++i) {
        const FastRune& r = runes[i];
        bool duplicate = false;
        const auto id = s.candidates[d][i].rune.rune_id;
        for (std::size_t j = 0; j < d; ++j)
            duplicate |= s.candidates[j][c.choice[j]].rune.rune_id == id;
        if (duplicate) continue;
        const int cost = s.candidates[d][i].cost;
        const int gems = s.candidates[d][i].gem_cost;
        if (!within_craft_budget(s.config, c.cost_used + cost, c.gems_used + gems)) {
            ++s.report.cuts_craft_budget;
            continue;
        }
        const auto si = static_cast<std::size_t>(r.set_index);
        if (c.count[si] >= c.required[si]) {
            ++s.report.cuts_sets;
            continue;
        }
        ++c.count[si];
        bool reachable = true;
        for (std::size_t t = 0; t < c.required.size(); ++t)
            if (c.required[t] - c.count[t] > c.available[t][d + 1]) {
                reachable = false;
                break;
            }
        if (!reachable) {
            --c.count[si];
            ++s.report.cuts_sets;
            continue;
        }
        Vec next{};
        for (std::size_t k = 0; k < 8; ++k) next[k] = cur[k] + r.v[k];
        c.choice[d] = i;
        c.cost_used += cost;
        c.gems_used += gems;
        if (last) {
            fast_leaf(c, next);
        } else if (const int cut = bound_cut(c, next, d + 1); cut == 0) {
            fast_descend(c, d + 1, next);
        } else if (cut == 1) {
            ++s.report.cuts_constraints;
        } else {
            ++s.report.cuts_objective;
        }
        c.cost_used -= cost;
        c.gems_used -= gems;
        --c.count[si];
        if (s.stopped) return;
    }
}

// Retourne true si le scénario a été traité ici (recherche rapide ou scénario
// impossible), false s'il faut utiliser la recherche générique (any_set).
bool run_fast_search(SearchState& s) {
    const SetScenario& scenario = s.scenario;
    if (scenario.any_set || scenario.sets.empty()) return false;
    int pieces = 0;
    std::set<int> ids;
    for (const auto& requirement : scenario.sets) {
        pieces += requirement.pieces;
        ids.insert(requirement.set_id);
    }
    if (ids.size() != scenario.sets.size()) return false;
    if (pieces != 6) {
        warn(s.warnings, "Solver: scenario '" + s.report.name + "' requires " +
                             std::to_string(pieces) +
                             " set pieces in total (6 needed); no build possible");
        return true;
    }
    for (std::size_t d = 0; d < 6; ++d)
        if (s.candidates[d].empty()) return true;

    FastContext c(s);
    const StatBlock baseline = [&] {
        AccountMonster empty = s.target;
        empty.runes.clear();
        return calculate_monster_stats(empty, s.set_effects);
    }();
    const StatBlock set_block = [&] {
        AccountMonster fake = s.target;
        fake.runes.clear();
        for (const auto& requirement : scenario.sets)
            for (int n = 0; n < requirement.pieces; ++n) {
                Rune rune;
                rune.set_id = requirement.set_id;
                rune.occupancy = RuneOccupancy::Equipped;
                fake.runes.push_back(rune);
            }
        return calculate_monster_stats(fake, s.set_effects);
    }();
    const auto arr = [](const StatValues& v) {
        return std::array<std::int64_t, 8>{v.hp, v.attack, v.defense, v.speed,
                                           v.crit_rate, v.crit_damage,
                                           v.resistance, v.accuracy};
    };
    const auto white = arr(baseline.white);
    const auto total0 = arr(baseline.total);
    const auto set_pct = arr(set_block.set_percent);
    for (std::size_t k = 0; k < 8; ++k) {
        const std::int64_t fixed_part = total0[k];  // blanc + artefacts
        c.base[k] = k < 4 ? 100 * fixed_part + white[k] * set_pct[k] + 99
                          : fixed_part + set_pct[k];
    }
    for (const auto& requirement : scenario.sets)
        c.required.push_back(requirement.pieces);
    c.count.assign(c.required.size(), 0);

    for (std::size_t d = 0; d < 6; ++d) {
        for (const auto& candidate : s.candidates[d]) {
            AccountMonster one = s.target;
            one.runes = {candidate.rune};
            one.runes[0].occupancy = RuneOccupancy::Equipped;
            const StatBlock block = calculate_monster_stats(one, s.set_effects);
            const auto fixed = arr(block.rune_fixed);
            const auto percent = arr(block.rune_percent);
            FastRune fast;
            for (std::size_t k = 0; k < 8; ++k)
                fast.v[k] = k < 4 ? 100 * fixed[k] + white[k] * percent[k]
                                  : fixed[k] + percent[k];
            for (std::size_t t = 0; t < scenario.sets.size(); ++t)
                if (scenario.sets[t].set_id == candidate.rune.set_id)
                    fast.set_index = static_cast<int>(t);
            fast.efficiency = calculate_rune_efficiency(candidate.rune);
            c.level[d].push_back(fast);
        }
    }
    c.vmax[6] = {};
    c.vmin[6] = {};
    c.available.assign(c.required.size(), {});
    for (std::size_t d = 6; d-- > 0;) {
        for (std::size_t k = 0; k < 8; ++k) {
            std::int64_t hi = std::numeric_limits<std::int64_t>::min();
            std::int64_t lo = std::numeric_limits<std::int64_t>::max();
            for (const auto& r : c.level[d]) {
                hi = std::max(hi, r.v[k]);
                lo = std::min(lo, r.v[k]);
            }
            c.vmax[d][k] = c.vmax[d + 1][k] + hi;
            c.vmin[d][k] = c.vmin[d + 1][k] + lo;
        }
        for (std::size_t t = 0; t < c.required.size(); ++t) {
            const bool present = std::any_of(
                c.level[d].begin(), c.level[d].end(), [&](const FastRune& r) {
                    return static_cast<std::size_t>(r.set_index) == t;
                });
            c.available[t][d] = c.available[t][d + 1] + (present ? 1 : 0);
        }
    }
    for (const auto& [name, constraint] : s.config.constraints) {
        c.constraints.push_back(
            {metric_id(name), constraint.minimum, constraint.maximum});
        c.need_derived |= is_derived(c.constraints.back().id);
    }
    c.best_constraint.assign(c.constraints.size(), 0.0);
    for (const auto& [name, weight] : s.config.objective.weights) {
        FastTerm term{metric_id(name), weight, false, 0};
        if (const auto cap = s.config.objective.caps.find(name);
            cap != s.config.objective.caps.end()) {
            term.capped = true;
            term.cap = cap->second;
        }
        c.terms.push_back(term);
        c.need_derived |= is_derived(term.id);
    }
    {
        const StatValues upper = totals_from(c, c.vmax[0]);
        const DerivedMetrics derived = calculate_derived_metrics(upper);
        for (const auto& [name, constraint] : s.config.constraints)
            if (constraint.minimum)
                s.report.constraint_upper_bounds[name] =
                    metric_of(metric_id(name), upper, derived);
    }
    c.use_bounds = true;
    c.limit = s.all_mode
                  ? std::numeric_limits<std::size_t>::max()
                  : kept_builds_limit(s.config.limits);

    Vec zero{};
    fast_descend(c, 0, zero);

    std::size_t i = 0;
    for (const auto& [name, constraint] : s.config.constraints) {
        (void)constraint;
        auto& best = s.report.best_constraint_values[name];
        if (best == 0 || c.best_constraint[i] > best) best = c.best_constraint[i];
        ++i;
    }
    return true;
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

namespace {

// ---------------------------------------------------------------------------
// Projection de la stat principale : table (étoiles, stat, niveau) -> valeur,
// construite à partir des runes observées dans l'export, complétée par la
// table "max" (+15) du mapping, appliquée aussi aux runes antiques (class
// étoiles + 10, même principale max). Une combinaison dont les sources se
// contredisent n'est pas utilisée.
// ---------------------------------------------------------------------------

using MainStatKey = std::tuple<int, int, int>;  // étoiles (class), stat, niveau

std::map<MainStatKey, int> build_main_stat_table(
    const std::vector<Rune>& pool, const std::filesystem::path& mapping_directory,
    const WarningSink& warnings) {
    std::map<MainStatKey, int> table;
    std::set<MainStatKey> conflicts;
    const auto add = [&](const MainStatKey& key, int value) {
        const auto [it, inserted] = table.emplace(key, value);
        if (!inserted && it->second != value) conflicts.insert(key);
    };
    for (const auto& rune : pool)
        if (rune.primary && rune.primary->stat != StatType::None)
            add({rune.stars, rune.primary->stat_id, rune.upgrade_level},
                rune.primary->value);

    std::ifstream input(mapping_directory / "rune_mainstat.json");
    if (input) {
        try {
            json root;
            input >> root;
            for (const auto& [stat_text, entry] : root.items()) {
                const auto max = entry.find("max");
                if (max == entry.end() || !max->is_object()) continue;
                for (const auto& [stars_text, value] : max->items()) {
                    if (!value.is_number_integer()) continue;
                    const int stars = std::stoi(stars_text);
                    const int stat = std::stoi(stat_text);
                    add({stars, stat, 15}, value.get<int>());
                    // Rune antique (class = étoiles + 10) : même principale max
                    // (+15) qu'une rune normale ; seules innée et substats changent.
                    add({stars + 10, stat, 15}, value.get<int>());
                }
            }
        } catch (const std::exception& error) {
            warn(warnings, std::string("Solver: invalid rune_mainstat mapping: ") +
                               error.what());
        }
    } else {
        warn(warnings, "Solver: unable to open rune_mainstat mapping");
    }
    for (const auto& key : conflicts) table.erase(key);
    if (!conflicts.empty())
        warn(warnings, "Solver: " + std::to_string(conflicts.size()) +
                           " main stat values are inconsistent between runes "
                           "and mapping; those combinations are not projected");
    return table;
}

// Runes de calcul : principale projetée au niveau demandé quand la valeur est
// connue. Innées, substats, meules et gemmes restent celles de la rune.
std::vector<Rune> project_pool(const std::vector<Rune>& pool, int level,
                               const std::filesystem::path& mapping_directory,
                               const WarningSink& warnings) {
    std::vector<Rune> projected = pool;
    if (level == kProjectionCurrent) return projected;
    const auto table = build_main_stat_table(pool, mapping_directory, warnings);
    std::map<std::pair<int, int>, int> missing;  // (étoiles, stat) -> runes
    std::size_t missing_total = 0;
    for (auto& rune : projected) {
        if (rune.upgrade_level >= level || !rune.primary ||
            rune.primary->stat == StatType::None)
            continue;
        const auto it =
            table.find({rune.stars, rune.primary->stat_id, level});
        if (it == table.end()) {
            ++missing[{rune.stars, rune.primary->stat_id}];
            ++missing_total;
            continue;
        }
        rune.primary->value = it->second;
        rune.upgrade_level = level;
    }
    if (missing_total > 0) {
        std::string detail;
        for (const auto& [key, count] : missing)
            detail += (detail.empty() ? "" : ", ") + std::to_string(key.first) +
                      "* stat " + std::to_string(key.second) + " : " +
                      std::to_string(count);
        warn(warnings, "Solver: projection +" + std::to_string(level) +
                           " unavailable for " + std::to_string(missing_total) +
                           " runes, used as they are (" + detail + ")");
    }
    return projected;
}

bool same_stats(const StatValues& a, const StatValues& b) {
    return a.hp == b.hp && a.attack == b.attack && a.defense == b.defense &&
           a.speed == b.speed && a.crit_rate == b.crit_rate &&
           a.crit_damage == b.crit_damage && a.resistance == b.resistance &&
           a.accuracy == b.accuracy;
}

// Après la recherche : runes réelles pour l'affichage, drapeau de projection,
// et en mode tous les builds, tri déterministe puis détail (revalidé avec la
// fonction de référence) des premiers builds.
void finalize_builds(SearchState& state) {
    // (rune, variante de meules) -> candidat
    std::map<std::pair<std::uint64_t, int>, const Candidate*> by_variant;
    for (const auto& level : state.candidates)
        for (const auto& candidate : level)
            by_variant.emplace(std::pair{candidate.rune.rune_id, candidate.variant},
                               &candidate);
    // Rune affichée : celle du build (meules simulées comprises) avec son niveau
    // et sa principale réels.
    const auto complete = [&](SolverBuild& build) {
        for (std::size_t i = 0; i < 6; ++i) {
            const auto it = by_variant.lower_bound({build.runes[i].rune_id, 0});
            if (it == by_variant.end() || it->first.first != build.runes[i].rune_id)
                continue;
            const Candidate& candidate = *it->second;
            build.actual_runes[i] = candidate.display;  // rune réelle
            if (candidate.display.upgrade_level != candidate.rune.upgrade_level)
                build.projection_level = state.config.pool.projection_level;
        }
    };
    if (!state.all_mode) {
        for (auto& build : state.report.builds) complete(build);
        return;
    }
    auto& all = state.report.all_builds;
    std::sort(all.begin(), all.end(), better_compact);
    const std::size_t detailed =
        std::min(all.size(), state.config.limits.detailed_builds);
    for (std::size_t n = 0; n < detailed; ++n) {
        const auto& compact = all[n];
        SolverBuild build;
        for (std::size_t i = 0; i < 6; ++i) {
            const auto it = by_variant.find({compact.rune_ids[i], compact.variants[i]});
            if (it == by_variant.end()) {
                state.report.validation_error = "returned build has an unknown rune";
                return;
            }
            build.runes[i] = it->second->rune;
        }
        build.scenario = state.report.name;
        build.grind_cost = compact.grind_cost;
        build.gem_cost = compact.gem_cost;
        build.stats = build_stats(state.target, build.runes, state.set_effects);
        if (!same_stats(build.stats.total, compact.total)) {
            state.report.validation_error = "internal stat recalculation mismatch";
            return;
        }
        build.metrics = calculate_derived_metrics(build.stats.total);
        build.score = score_build(build, state.config.objective);
        build.efficiency = calculate_build_efficiency(build.runes);
        complete(build);
        state.report.builds.push_back(std::move(build));
    }
}

}  // namespace

std::vector<RuneCraftVariant> list_craft_variants(
    const Rune& rune, const SolverConfig& config,
    const std::filesystem::path& mapping_directory) {
    const auto grind_ranges =
        config.grindstones.empty()
            ? CraftRanges{}
            : load_craft_ranges(mapping_directory / "grindstone.json");
    const auto gem_ranges =
        config.gems.empty()
            ? CraftRanges{}
            : load_craft_ranges(mapping_directory / "enchanted_gem.json");
    std::vector<RuneCraftVariant> result;
    for (auto& candidate : enumerate_variants(rune, rune.upgrade_level, config,
                                              grind_ranges, gem_ranges))
        result.push_back({std::move(candidate.rune), candidate.cost,
                          candidate.gem_cost});
    return result;
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
    report.projection_level = build.projection_level;
    for (std::size_t i = 0; i < report.stats.size(); ++i) {
        const int white = stat_at(build.stats.white, i);
        const int total = stat_at(build.stats.total, i);
        report.stats[i] = {white, total - white, total};
    }
    report.metrics = build.metrics;
    report.metrics.rune_efficiency = calculate_build_efficiency(build.runes);
    report.metrics.rune_efficiency_available = true;
    // Cartes : runes réelles (en jeu), avec les modifications simulées à part.
    for (std::size_t i = 0; i < report.runes.size(); ++i) {
        const Rune& used = build.runes[i];
        const Rune& shown =
            build.actual_runes[i].rune_id != 0 ? build.actual_runes[i] : used;
        auto& view = report.runes[i];
        view.rune = shown;
        view.efficiency = calculate_rune_efficiency(shown);
        view.grinds = used.applied_grindstones;
        view.gems = used.applied_gems;
        if (!view.grinds.empty() || !view.gems.empty()) {
            view.simulated_efficiency = calculate_rune_efficiency(used);
            report.simulated_craft = true;
        }
    }
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
    const auto projected_pool = project_pool(
        pool, config.pool.projection_level, mapping_directory, warnings);
    const CraftRanges grind_ranges =
        config.grindstones.empty()
            ? CraftRanges{}
            : load_craft_ranges(mapping_directory / "grindstone.json", warnings);
    const CraftRanges gem_ranges =
        config.gems.empty()
            ? CraftRanges{}
            : load_craft_ranges(mapping_directory / "enchanted_gem.json", warnings);
    const Directions directions = stat_directions(config);
    const Directions variant_directions = relevant_directions(config);
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
        state.all_mode = all_builds_mode(config);
        state.report.all_builds_mode = state.all_mode;
        for (int slot = 1; slot <= 6; ++slot) {
            std::vector<Candidate> candidates;
            auto& diagnostic = state.report.slot_filters[static_cast<std::size_t>(slot - 1)];
            for (std::size_t index = 0; index < pool.size(); ++index) {
                const Rune& source = pool[index];  // filtres sur la rune réelle
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
                const Rune real = with_source_name(source);
                for (auto& candidate : craft_variants(
                         with_source_name(projected_pool[index]),
                         source.upgrade_level, config, grind_ranges, gem_ranges,
                         target, set_effects, variant_directions)) {
                    candidate.display = real;  // rune réelle, pour la carte
                    candidates.push_back(std::move(candidate));
                }
            }
            state.report.candidates_before += diagnostic.after_set;
            if (state.report.raw_combinations == 0)
                state.report.raw_combinations = 1;
            state.report.raw_combinations *= candidates.size();
            // Les runes dominées ne peuvent pas améliorer les meilleurs builds,
            // mais elles comptent quand on veut tous les builds valides.
            if (!state.all_mode &&
                !prune_dominated(candidates, target, set_effects, directions,
                                 state))
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
        if (!run_fast_search(state)) search(state, 0);
        finalize_builds(state);
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
                     " cuts_sets=" + std::to_string(completed.cuts_sets) +
                     " cuts_objective=" + std::to_string(completed.cuts_objective) +
                     " cuts_constraints=" +
                     std::to_string(completed.cuts_constraints) +
                     " elapsed_ms=" + std::to_string(completed.elapsed_ms) +
                     (completed.incomplete ? " incomplete" : ""));
        }
    }
    return report;
}

}  // namespace swcore
