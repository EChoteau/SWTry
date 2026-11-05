#include "swcore/dungeon/team_optimizer.hpp"

#include <algorithm>
#include <cstdio>
#include <tuple>

namespace swcore {

namespace {

std::string number_text(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.0f", value);
    return buffer;
}

// Clé de comparaison (ordre lexicographique, plus grand = mieux).
using Key = std::tuple<bool, bool, bool, double, double>;
Key key_of(const TeamAnalysis& analysis) {
    return {analysis.constraints_ok, analysis.worst.atb.script_before_boss, analysis.worst.kills,
            analysis.worst.damage, analysis.best.damage};
}

}  // namespace

std::optional<SetScenario> scenario_from_sets(const std::vector<std::string>& names,
                                              const std::unordered_map<int, std::string>& set_names,
                                              const SetEffects& effects, std::string* problem) {
    if (names.empty()) return SetScenario{"Tous sets", {}, true};
    std::map<int, int> pieces;
    std::vector<int> order;
    for (const auto& name : names) {
        const auto it = std::find_if(set_names.begin(), set_names.end(),
                                     [&](const auto& entry) { return entry.second == name; });
        if (it == set_names.end()) {
            if (problem) *problem = "set inconnu : " + name;
            return std::nullopt;
        }
        const auto effect = effects.effects.find(it->first);
        const int required = effect != effects.effects.end() && effect->second.pieces_required > 0
                                 ? effect->second.pieces_required
                                 : 2;
        if (!pieces.contains(it->first)) order.push_back(it->first);
        pieces[it->first] += required;
    }
    SetScenario scenario;
    int total = 0;
    for (const int id : order) {
        scenario.sets.push_back({id, pieces[id]});
        total += pieces[id];
    }
    if (total != 6) {
        if (problem)
            *problem = "les sets totalisent " + std::to_string(total) + " pièces (6 attendues)";
        return std::nullopt;
    }
    return scenario;
}

TeamOptimizerResult optimize_team(const TeamOptimizerInput& input,
                                  const TeamOptimizerSettings& settings, std::stop_token stop,
                                  const std::function<void(const std::string&)>& progress) {
    TeamOptimizerResult result;
    if (!input.box || !input.runes || !input.analysis.effects || !input.set_names ||
        !input.analysis.team) {
        result.log.push_back("Données manquantes (export non chargé ?).");
        return result;
    }
    const auto& effects = *input.analysis.effects;
    const auto report = [&](const std::string& message) {
        result.log.push_back(message);
        if (progress) progress(message);
    };

    // Simulation de la team avec des builds candidats (jamais les runes équipées).
    const auto analyze = [&](const std::map<std::string, std::vector<Rune>>& builds,
                             const std::map<std::string, int>& assumed) {
        TeamAnalysisInput analysis_input = input.analysis;
        analysis_input.box = input.box;
        analysis_input.builds = builds;
        analysis_input.use_current_runes = false;
        analysis_input.assumed_speeds = assumed;
        analysis_input.pinned_units = result.units;
        analysis_input.compute_windows = true;
        return analyze_team(analysis_input);
    };
    const TeamAnalysis initial = analyze({}, {});
    if (!initial.valid) {
        report("Analyse impossible (étage ou boss introuvable).");
        return result;
    }
    for (const auto& member : initial.members)
        if (member.unit) result.units[member.config->name] = member.unit->unit_id;
    if (!initial.plan.found) report("Plan de vitesse introuvable : " + initial.plan.reason + ".");

    const std::size_t count = initial.members.size();
    std::vector<const AccountMonster*> unit(count, nullptr);
    for (std::size_t i = 0; i < count; ++i) {
        TeamOptimizerMember member;
        member.name = initial.members[i].config->name;
        member.locked = settings.locked.contains(member.name);
        unit[i] = initial.members[i].unit;
        member.owned = unit[i] != nullptr;
        if (!member.owned) member.note = "absent de la box";
        // Verrouillé : ses runes actuelles sont son build, intouchable.
        if (member.locked && unit[i]) {
            std::vector<Rune> runes;
            for (const auto& rune : unit[i]->runes)
                if (rune.occupancy == RuneOccupancy::Equipped) runes.push_back(rune);
            result.builds[member.name] = std::move(runes);
        }
        result.members.push_back(std::move(member));
    }

    // Ordre de runage : ancres, supports, puis nukers par c_i décroissant.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < count; ++i) order.push_back(i);
    const auto rank = [&](std::size_t i) {
        const auto& role = initial.members[i].config->role;
        if (role == "speed_anchor") return 0;
        if (role == "support") return 1;
        return 2;
    };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        if (rank(a) == 2) return initial.members[a].coefficient > initial.members[b].coefficient;
        return initial.members[a].config->priority < initial.members[b].config->priority;
    });

    // VIT supposées des membres sans build : celles du plan joint.
    const auto assumed_for = [&](const std::map<std::string, std::vector<Rune>>& builds,
                                 std::size_t except) {
        std::map<std::string, int> assumed;
        for (std::size_t i = 0; i < count; ++i)
            if (i != except && !builds.contains(result.members[i].name))
                assumed[result.members[i].name] = initial.members[i].plan_min;
        return assumed;
    };

    struct Attempt {
        std::optional<SolverBuild> build;
        SolverConfig config;
        std::string scenario;
        std::string note;
    };
    // Recherche mono-monstre de i, contraintes calculées avec les autres.
    const auto solve_member = [&](std::size_t i,
                                  const std::map<std::string, std::vector<Rune>>& builds) {
        Attempt attempt;
        const auto& name = result.members[i].name;
        auto others = builds;
        others.erase(name);
        const auto context = analyze(others, assumed_for(others, i));
        const auto& member = context.members[i];

        const auto sets = settings.sets.find(name);
        std::string problem;
        const auto scenario = scenario_from_sets(
            sets == settings.sets.end() ? std::vector<std::string>{} : sets->second,
            *input.set_names, effects, &problem);
        if (!scenario) {
            attempt.note = "sets : " + problem;
            return attempt;
        }
        SolverConfig config;
        config.pool.target_unit_id = unit[i]->unit_id;
        config.pool.minimum_rune_level = settings.minimum_rune_level;
        config.pool.minimum_stars = settings.minimum_stars;
        config.scenarios = {*scenario};
        // Fenêtre stricte avec les VIT des autres (sinon le plan joint), ramenée
        // de la VIT de combat à la VIT des runes (moins la part tours + leader).
        int lower = member.plan_min;
        std::optional<int> upper = member.plan_max;
        if (member.window.found) {
            lower = member.window.min;
            upper = member.window.upper_open ? std::nullopt : std::optional<int>{member.window.max};
        }
        config.constraints["speed"].minimum = lower - member.speed.bonus;
        if (upper) config.constraints["speed"].maximum = *upper - member.speed.bonus;
        if (member.crit.needed)
            config.constraints["crit_rate"].minimum = member.crit.base + member.crit.required_runes;
        const bool nuker = member.config->role == "nuker" ||
                           (member.config->role.empty() && member.crit.needed);
        config.objective.weights = {{nuker ? "attack_crit_damage" : "effective_hp", 1.0}};
        config.grindstones = settings.grindstones;
        config.gems = settings.gems;
        config.craft_min_level = settings.craft_min_level;
        config.limits.top_n = 1;
        config.limits.max_builds_per_scenario = 1;
        config.limits.time_budget_ms = settings.time_budget_ms;
        if (input.locks) config.lock_table = *input.locks;

        // Pool : sans les runes des builds des autres membres.
        std::set<std::uint64_t> taken;
        for (const auto& [other, runes] : others)
            for (const auto& rune : runes) taken.insert(rune.rune_id);
        std::vector<Rune> pool;
        pool.reserve(input.runes->size());
        for (const auto& rune : *input.runes)
            if (!taken.contains(rune.rune_id)) pool.push_back(rune);

        SolveControl control;
        control.stop = stop;
        const auto run = [&](const SolverConfig& search) -> std::optional<SolverBuild> {
            const auto solved = solve_runes(*unit[i], pool, effects, search,
                                            input.mapping_directory, {}, control);
            for (const auto& scenario_report : solved.scenarios)
                if (!scenario_report.builds.empty()) {
                    attempt.scenario = scenario_report.name;
                    return scenario_report.builds.front();
                }
            return std::nullopt;
        };
        attempt.build = run(config);
        if (!attempt.build && !stop.stop_requested()) {
            // VIT requise inatteignable : le plus de VIT possible (maximum et CR
            // gardés), pour que "ce qui manque" chiffre la VIT manquante.
            auto fallback = config;
            fallback.constraints["speed"].minimum.reset();
            fallback.objective.weights = {{"speed", 1.0}};
            attempt.build = run(fallback);
            if (attempt.build) {
                attempt.note = "VIT requise non atteinte (" +
                               std::to_string(attempt.build->stats.total.speed + member.speed.bonus) +
                               " / " + std::to_string(lower) + ")";
                config = fallback;
            } else {
                attempt.note = "aucun build sous les contraintes (sets, CR)";
            }
        }
        attempt.config = std::move(config);
        return attempt;
    };

    const auto runes_of_build = [](const SolverBuild& build) {
        return std::vector<Rune>(build.runes.begin(), build.runes.end());
    };

    // --- 1. Glouton ---
    std::size_t done = 0;
    for (const auto i : order) {
        ++done;
        auto& member = result.members[i];
        if (stop.stop_requested()) break;
        if (member.locked || !member.owned) continue;
        report("Glouton : runage de " + member.name + " (" + std::to_string(done) + "/" +
               std::to_string(count) + ")...");
        auto attempt = solve_member(i, result.builds);
        member.note = attempt.note;
        if (!attempt.build) continue;
        result.builds[member.name] = runes_of_build(*attempt.build);
        member.build = attempt.build;
        member.config = attempt.config;
        member.scenario = attempt.scenario;
    }
    TeamAnalysis current = analyze(result.builds, {});
    report("Après le glouton : " + std::string(current.constraints_ok ? "contraintes tenues"
                                                                       : "contraintes non tenues") +
           ", dégâts au pire cas " + number_text(current.worst.damage) + ".");

    // --- 2. Descente coordonnée ---
    for (int pass = 1; pass <= settings.max_passes && !stop.stop_requested(); ++pass) {
        bool improved = false;
        for (const auto i : order) {
            if (stop.stop_requested()) break;
            auto& member = result.members[i];
            if (member.locked || !member.owned) continue;
            report("Passe " + std::to_string(pass) + " : " + member.name + "...");
            auto attempt = solve_member(i, result.builds);
            if (!attempt.build) continue;
            auto trial = result.builds;
            trial[member.name] = runes_of_build(*attempt.build);
            auto analysis = analyze(trial, {});
            if (!(key_of(analysis) > key_of(current))) continue;  // candidat rejeté
            result.builds = std::move(trial);
            current = std::move(analysis);
            member.build = attempt.build;
            member.config = attempt.config;
            member.scenario = attempt.scenario;
            member.note = attempt.note;
            improved = true;
            report("Passe " + std::to_string(pass) + " : " + member.name +
                   " amélioré (dégâts au pire cas " + number_text(current.worst.damage) + ").");
        }
        result.passes = pass;
        if (!improved) break;
    }

    for (std::size_t i = 0; i < count; ++i) {
        auto& member = result.members[i];
        if (!member.build || !unit[i]) continue;
        std::set<std::uint64_t> now;
        for (const auto& rune : unit[i]->runes)
            if (rune.occupancy == RuneOccupancy::Equipped) now.insert(rune.rune_id);
        std::set<std::uint64_t> next;
        bool crafted = false;
        for (const auto& rune : member.build->runes) {
            next.insert(rune.rune_id);
            crafted |= !rune.applied_grindstones.empty() || !rune.applied_gems.empty();
        }
        member.changed = now != next || crafted;
    }
    // Résultat de la simulation finale (seule source de "contraintes tenues").
    result.constraints_ok = current.constraints_ok;
    result.script_ok = current.worst.atb.script_before_boss;
    result.kills = current.worst.kills;
    result.worst_damage = current.worst.damage;
    result.best_damage = current.best.damage;
    result.cancelled = stop.stop_requested();
    report(std::string(result.cancelled ? "Optimisation annulée" : "Optimisation terminée") +
           (result.constraints_ok ? " : contraintes tenues" : " : contraintes non tenues") +
           (result.kills ? ", boss mort au pire cas." : ", boss vivant au pire cas."));
    return result;
}

}  // namespace swcore
