#include "swcore/team_optimizer.hpp"

#include <algorithm>
#include <cstdio>

namespace swcore {

namespace {

std::string score_text(double score) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.0f", score);
    return buffer;
}

// Meilleure évaluation : contraintes tenues d'abord, puis score.
bool better(bool ok, double score, bool reference_ok, double reference_score) {
    if (ok != reference_ok) return ok;
    return score > reference_score + 1e-6;
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

void equip_build(AccountMonster& unit, const SolverBuild& build, const SetEffects& effects) {
    unit.runes.assign(build.runes.begin(), build.runes.end());
    for (auto& rune : unit.runes) {
        rune.occupancy = RuneOccupancy::Equipped;
        rune.source_unit_id = unit.unit_id;
    }
    unit.stats = calculate_monster_stats(unit, effects);
}

TeamOptimizerResult optimize_team(const TeamOptimizerInput& input,
                                  const TeamOptimizerSettings& settings, std::stop_token stop,
                                  const std::function<void(const std::string&)>& progress) {
    TeamOptimizerResult result;
    if (!input.box || !input.runes || !input.effects || !input.set_names ||
        !input.analysis.team) {
        result.log.push_back("Données manquantes (export non chargé ?).");
        return result;
    }
    result.box = *input.box;
    const auto report = [&](const std::string& message) {
        result.log.push_back(message);
        if (progress) progress(message);
    };

    // Analyse de la box hypothétique (fenêtres en option, plus lentes).
    std::map<std::string, std::uint64_t> pinned;
    const auto analyze = [&](const std::vector<AccountMonster>& box, bool windows) {
        TeamAnalysisInput analysis_input = input.analysis;
        analysis_input.box = &box;
        analysis_input.manual_speeds.clear();
        analysis_input.pinned_units = pinned;
        analysis_input.compute_windows = windows;
        return analyze_team(analysis_input);
    };
    TeamAnalysis current = analyze(result.box, true);
    if (!current.valid) {
        report("Analyse impossible (étage ou boss introuvable).");
        return result;
    }
    // Exemplaire de chaque membre figé pour toute l'optimisation.
    for (const auto& member : current.members)
        if (member.unit) pinned[member.config->name] = member.unit->unit_id;
    current = analyze(result.box, true);
    result.score_before = current.score;
    result.constraints_before = current.constraints_ok;

    // Index dans la box de chaque membre (exemplaire retenu par l'analyse).
    std::vector<std::optional<std::size_t>> unit_index(current.members.size());
    for (std::size_t i = 0; i < current.members.size(); ++i) {
        TeamOptimizerMember member;
        member.name = current.members[i].config->name;
        member.locked = settings.locked.contains(member.name);
        if (const auto* unit = current.members[i].unit) {
            // L'analyse pointe dans result.box (copie de la box).
            unit_index[i] = static_cast<std::size_t>(unit - result.box.data());
            member.owned = true;
        } else {
            member.note = "absent de la box";
        }
        result.members.push_back(std::move(member));
    }

    // Ordre de runage : ancres, supports, puis nukers par c_i décroissant.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < current.members.size(); ++i) order.push_back(i);
    const auto rank = [&](std::size_t i) {
        const auto& role = current.members[i].config->role;
        if (role == "speed_anchor") return 0;
        if (role == "support") return 1;
        return 2;
    };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        if (rank(a) == 2) return current.members[a].coefficient > current.members[b].coefficient;
        return current.members[a].config->priority < current.members[b].config->priority;
    });

    // Runes déjà attribuées (monstres traités ou verrouillés) : indisponibles.
    std::vector<bool> assigned(current.members.size(), false);
    for (std::size_t i = 0; i < current.members.size(); ++i)
        assigned[i] = result.members[i].locked;

    const auto runes_of = [&](std::size_t member) {
        std::set<std::uint64_t> ids;
        if (unit_index[member])
            for (const auto& rune : result.box[*unit_index[member]].runes)
                if (rune.occupancy == RuneOccupancy::Equipped) ids.insert(rune.rune_id);
        return ids;
    };

    // Recherche mono-monstre sous les contraintes calculées avec les autres.
    struct Attempt {
        std::optional<SolverBuild> build;
        SolverConfig config;
        std::string scenario;
        std::string note;
    };
    const auto solve_member = [&](std::size_t i, const TeamAnalysis& analysis,
                                  bool exclude_all_others) {
        Attempt attempt;
        const auto& member = analysis.members[i];
        const auto& name = member.config->name;
        // Sets du preset choisi.
        const auto sets = settings.sets.find(name);
        std::string problem;
        const auto scenario = scenario_from_sets(
            sets == settings.sets.end() ? std::vector<std::string>{} : sets->second,
            *input.set_names, *input.effects, &problem);
        if (!scenario) {
            attempt.note = "sets : " + problem;
            return attempt;
        }
        SolverConfig config;
        config.pool.target_unit_id = result.box[*unit_index[i]].unit_id;
        config.pool.minimum_rune_level = settings.minimum_rune_level;
        config.pool.minimum_stars = settings.minimum_stars;
        config.scenarios = {*scenario};
        // Fenêtre de VIT (VIT de combat -> VIT des runes : moins la part leader/tour).
        if (member.window.found) {
            if (!member.window.lower_open)
                config.constraints["speed"].minimum = member.window.min - member.speed.bonus;
            if (!member.window.upper_open)
                config.constraints["speed"].maximum = member.window.max - member.speed.bonus;
        }
        // Taux critique : total (base + runes) >= 100 - leader - avantage.
        if (member.crit.needed)
            config.constraints["crit_rate"].minimum =
                member.crit.base + member.crit.required_runes;
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

        // Pool : sans les runes des monstres attribués (ou de tous les autres).
        std::set<std::uint64_t> taken;
        for (std::size_t other = 0; other < analysis.members.size(); ++other)
            if (other != i && (exclude_all_others || assigned[other]))
                for (const auto id : runes_of(other)) taken.insert(id);
        std::vector<Rune> pool;
        pool.reserve(input.runes->size());
        for (const auto& rune : *input.runes)
            if (!taken.contains(rune.rune_id)) pool.push_back(rune);

        SolveControl control;
        control.stop = stop;
        const auto solved = solve_runes(result.box[*unit_index[i]], pool, *input.effects, config,
                                        input.mapping_directory, {}, control);
        for (const auto& scenario_report : solved.scenarios)
            if (!scenario_report.builds.empty()) {
                attempt.build = scenario_report.builds.front();
                attempt.scenario = scenario_report.name;
            }
        if (!attempt.build) attempt.note = "aucun build sous les contraintes (runes actuelles gardées)";
        attempt.config = std::move(config);
        return attempt;
    };

    // Applique un build : runes retirées des autres membres, stats recalculées.
    const auto apply = [&](std::vector<AccountMonster>& box, std::size_t i,
                           const SolverBuild& build) {
        std::set<std::uint64_t> ids;
        for (const auto& rune : build.runes) ids.insert(rune.rune_id);
        for (std::size_t other = 0; other < current.members.size(); ++other) {
            if (other == i || !unit_index[other]) continue;
            auto& unit = box[*unit_index[other]];
            const auto before = unit.runes.size();
            std::erase_if(unit.runes, [&](const Rune& rune) { return ids.contains(rune.rune_id); });
            if (unit.runes.size() != before) unit.stats = calculate_monster_stats(unit, *input.effects);
        }
        equip_build(box[*unit_index[i]], build, *input.effects);
    };
    const auto runes_differ = [&](std::size_t i, const SolverBuild& build) {
        std::set<std::uint64_t> now;
        for (const auto& rune : input.box->at(*unit_index[i]).runes)
            if (rune.occupancy == RuneOccupancy::Equipped) now.insert(rune.rune_id);
        std::set<std::uint64_t> next;
        for (const auto& rune : build.runes) next.insert(rune.rune_id);
        bool crafted = false;
        for (const auto& rune : build.runes)
            crafted |= !rune.applied_grindstones.empty() || !rune.applied_gems.empty();
        return now != next || crafted;
    };

    // --- 1. Passage glouton ---
    std::size_t done = 0;
    const std::size_t total = order.size();
    for (const auto i : order) {
        ++done;
        auto& member = result.members[i];
        if (stop.stop_requested()) break;
        if (member.locked) {
            report("Glouton : " + member.name + " verrouillé, inchangé.");
            continue;
        }
        if (!unit_index[i]) {
            report("Glouton : " + member.name + " absent de la box, ignoré.");
            assigned[i] = true;
            continue;
        }
        report("Glouton : runage de " + member.name + " (" + std::to_string(done) + "/" +
               std::to_string(total) + ")...");
        auto attempt = solve_member(i, current, false);
        if (attempt.build) {
            apply(result.box, i, *attempt.build);
            member.build = attempt.build;
            member.config = attempt.config;
            member.scenario = attempt.scenario;
            member.note.clear();
        } else {
            member.note = attempt.note;
        }
        assigned[i] = true;
        current = analyze(result.box, true);
    }
    report("Après le glouton : score " + score_text(current.score) +
           (current.constraints_ok ? ", contraintes tenues." : ", contraintes non tenues."));

    // --- 2. Descente coordonnée ---
    for (int pass = 1; pass <= settings.max_passes && !stop.stop_requested(); ++pass) {
        bool improved = false;
        for (const auto i : order) {
            if (stop.stop_requested()) break;
            auto& member = result.members[i];
            if (member.locked || !unit_index[i]) continue;
            report("Passe " + std::to_string(pass) + " : " + member.name + "...");
            auto attempt = solve_member(i, current, true);
            if (!attempt.build) continue;
            auto trial = result.box;
            apply(trial, i, *attempt.build);
            const auto analysis = analyze(trial, true);
            if (!better(analysis.constraints_ok, analysis.score, current.constraints_ok,
                        current.score))
                continue;
            result.box = std::move(trial);
            current = analysis;
            member.build = attempt.build;
            member.config = attempt.config;
            member.scenario = attempt.scenario;
            member.note.clear();
            improved = true;
            report("Passe " + std::to_string(pass) + " : " + member.name + " amélioré (score " +
                   score_text(current.score) + ").");
        }
        result.passes = pass;
        if (!improved) break;
    }

    for (std::size_t i = 0; i < result.members.size(); ++i)
        if (result.members[i].build && unit_index[i])
            result.members[i].changed = runes_differ(i, *result.members[i].build);
    result.units = pinned;
    result.cancelled = stop.stop_requested();
    result.score_after = current.score;
    result.constraints_after = current.constraints_ok;
    report(std::string(result.cancelled ? "Optimisation annulée" : "Optimisation terminée") +
           " : score " + score_text(result.score_before) + " -> " +
           score_text(result.score_after) + ".");
    return result;
}

}  // namespace swcore
