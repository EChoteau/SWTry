#include "swcore/dungeon/atb.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace swcore {

namespace {

constexpr double kEpsilon = 1e-6;  // égalité de barres (sommes de flottants)

std::string bar_text(double bar) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", bar);
    return buffer;
}

}  // namespace

AtbResult simulate_atb(const std::vector<AtbUnit>& units, const std::vector<AtbStep>& script,
                       const AtbOptions& options) {
    AtbResult result;
    const std::size_t count = units.size();
    std::vector<double> bars(count, 0.0);
    std::vector<int> turns_played(count, 0);
    std::vector<bool> stunned(count, false);
    std::vector<bool> dead(count, false);
    std::size_t next_step = 0;
    std::optional<std::size_t> previous_ally;
    std::set<std::string> seen_warnings;

    // Événements du tour qui vient de se jouer (étourdissements, morts).
    const auto apply_events = [&](std::size_t actor) {
        for (const auto& event : options.events) {
            if (event.actor != actor || event.actor_turn != turns_played[actor]) continue;
            for (const auto target : event.stunned)
                if (target < count) stunned[target] = true;
            for (const auto target : event.killed)
                if (target < count) {
                    dead[target] = true;
                    bars[target] = 0.0;
                }
        }
    };

    for (int tick = 1; tick <= options.max_ticks; ++tick) {
        for (std::size_t i = 0; i < count; ++i)
            if (!dead[i]) bars[i] += options.tick_percent / 100.0 * units[i].speed;
        result.bars.push_back(bars);

        // Un seul joue : la barre la plus haute >= 100 ; égalité -> ordre.
        std::optional<std::size_t> best;
        for (std::size_t i = 0; i < count; ++i)
            if (!dead[i] && bars[i] >= 100.0 - kEpsilon &&
                (!best || bars[i] > bars[*best] + kEpsilon))
                best = i;
        if (!best) continue;
        AtbTurn turn;
        turn.tick = tick;
        turn.actor = *best;
        turn.bar = bars[*best];
        if (stunned[*best]) {
            // Étourdi : le tour est perdu, l'étourdissement tombe.
            stunned[*best] = false;
            bars[*best] = 0.0;
            turn.stunned = true;
            apply_events(*best);
            ++turns_played[*best];
            result.turns.push_back(turn);
            continue;
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (i == *best || dead[i] || std::abs(bars[i] - bars[*best]) > kEpsilon) continue;
            turn.tie = true;
            turn.tied.push_back(i);
            const std::string message =
                "égalité de barre entre " + units[*best].name + " et " + units[i].name +
                " (" + bar_text(bars[i]) + ", tic " + std::to_string(tick) +
                ") : ordre non garanti, " + units[*best].name + " joue d'abord (ordre de la config)";
            if (seen_warnings.insert(message).second) result.warnings.push_back(message);
        }
        bars[*best] = 0.0;

        const AtbUnit& actor = units[*best];
        if (actor.ally) {
            if (next_step < script.size() && script[next_step].actor == *best) {
                turn.step = static_cast<int>(next_step);
                for (const auto& boost : script[next_step].boosts) {
                    std::optional<std::size_t> target;
                    switch (boost.target) {
                    case AtbBoost::Target::Self: target = *best; break;
                    case AtbBoost::Target::PreviousAlly: target = previous_ally; break;
                    case AtbBoost::Target::Ally: target = boost.ally; break;
                    }
                    if (target && *target < count)
                        bars[*target] = std::max(0.0, bars[*target] + boost.amount);
                }
                ++next_step;
            }
            previous_ally = *best;
        }
        apply_events(*best);
        ++turns_played[*best];
        result.turns.push_back(turn);

        if (actor.boss && !result.boss_turn) {
            result.boss_turn = result.turns.size() - 1;
            result.steps_done_before_boss = next_step;
            result.script_before_boss = next_step == script.size();
        }
        if (result.boss_turn &&
            static_cast<int>(result.turns.size() - 1 - *result.boss_turn) >= options.turns_after_boss)
            break;
    }
    if (!result.boss_turn) {
        // Le boss ne joue jamais dans la limite : le script passe s'il est complet.
        result.steps_done_before_boss = next_step;
        result.script_before_boss = next_step == script.size();
    }
    return result;
}

PlanCheck check_speed_plan(const std::vector<AtbUnit>& units, const std::vector<AtbStep>& script,
                           const AtbOptions& options) {
    PlanCheck check;
    AtbOptions fast = options;
    fast.turns_after_boss = 0;  // seul le premier tour du boss compte
    const auto result = simulate_atb(units, script, fast);
    std::size_t next = 0;  // prochaine action du script
    const auto pending_actor = [&]() -> std::optional<std::size_t> {
        if (next < script.size()) return script[next].actor;
        return std::nullopt;
    };
    for (std::size_t t = 0; t < result.turns.size(); ++t) {
        if (result.boss_turn && t == *result.boss_turn) break;
        const auto& turn = result.turns[t];
        const auto& unit = units[turn.actor];
        if (unit.ally && turn.step >= 0) {
            // Action du script gagnée à égalité : pas garanti, 1 point d'écart requis.
            if (turn.tie) {
                check.raise = turn.actor;
                check.reason = unit.name + " joue à égalité de barre (action " +
                               std::to_string(turn.step + 1) + ")";
                return check;
            }
            next = static_cast<std::size_t>(turn.step) + 1;
            continue;
        }
        if (unit.ally) {
            // Tour hors script d'un allié qui a encore une action plus loin :
            // l'acteur de l'action en attente est trop lent.
            for (std::size_t k = next; k < script.size(); ++k)
                if (script[k].actor == turn.actor) {
                    check.raise = pending_actor();
                    check.reason = unit.name + " joue avant " +
                                   (check.raise ? units[*check.raise].name : std::string{"?"}) +
                                   " (ordre du script cassé)";
                    return check;
                }
            continue;
        }
        // Ennemi à égalité avec l'acteur de l'action en attente : écart requis.
        if (const auto pending = pending_actor(); pending && turn.tie &&
            std::find(turn.tied.begin(), turn.tied.end(), *pending) != turn.tied.end()) {
            check.raise = *pending;
            check.reason = units[*pending].name + " à égalité avec " + unit.name;
            return check;
        }
    }
    if (next < script.size()) {
        check.raise = script[next].actor;
        check.reason = "action " + std::to_string(next + 1) + " (" + units[script[next].actor].name +
                       ") après le premier tour du boss";
        return check;
    }
    check.ok = true;
    return check;
}

SpeedPlan plan_speeds(std::vector<AtbUnit> units, const std::vector<AtbStep>& script,
                      const AtbOptions& options, int speed_cap) {
    SpeedPlan plan;
    // Chaque échec monte de 1 la VIT d'une unité : borne large sur le nombre de tours.
    const int max_iterations = speed_cap * static_cast<int>(units.size() + 1);
    for (plan.iterations = 0; plan.iterations < max_iterations; ++plan.iterations) {
        const auto check = check_speed_plan(units, script, options);
        if (check.ok) {
            plan.found = true;
            break;
        }
        plan.reason = check.reason;
        if (!check.raise || units[*check.raise].speed >= speed_cap) break;
        ++units[*check.raise].speed;
    }
    for (const auto& unit : units) plan.speeds.push_back(unit.speed);
    return plan;
}

SpeedWindow speed_window(std::vector<AtbUnit> units, const std::vector<AtbStep>& script,
                         std::size_t unit, const AtbOptions& options, int sweep_min,
                         int sweep_max) {
    SpeedWindow window;
    if (unit >= units.size() || sweep_min > sweep_max) return window;
    const auto passes = [&](int speed) {
        units[unit].speed = speed;
        return check_speed_plan(units, script, options).ok;
    };
    const int current = std::clamp(units[unit].speed, sweep_min, sweep_max);
    const int original = units[unit].speed;

    // Point de départ : la VIT actuelle si elle passe, sinon la plus proche qui passe.
    std::optional<int> start;
    if (passes(current)) {
        start = current;
        window.current_inside = original == current;
    } else {
        for (int distance = 1; !start && distance <= sweep_max - sweep_min; ++distance) {
            if (current - distance >= sweep_min && passes(current - distance))
                start = current - distance;
            else if (current + distance <= sweep_max && passes(current + distance))
                start = current + distance;
        }
    }
    if (!start) {
        units[unit].speed = original;
        return window;
    }
    window.found = true;
    window.min = *start;
    window.max = *start;
    while (window.min > sweep_min && passes(window.min - 1)) --window.min;
    while (window.max < sweep_max && passes(window.max + 1)) ++window.max;
    window.lower_open = window.min == sweep_min;
    window.upper_open = window.max == sweep_max;
    units[unit].speed = original;
    return window;
}

}  // namespace swcore
