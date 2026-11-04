#include "swcore/atb.hpp"

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
    std::size_t next_step = 0;
    std::optional<std::size_t> previous_ally;
    std::set<std::string> seen_warnings;

    for (int tick = 1; tick <= options.max_ticks; ++tick) {
        for (std::size_t i = 0; i < count; ++i)
            bars[i] += options.tick_percent / 100.0 * units[i].speed;
        result.bars.push_back(bars);

        // Un seul joue : la barre la plus haute >= 100 ; égalité -> ordre.
        std::optional<std::size_t> best;
        for (std::size_t i = 0; i < count; ++i)
            if (bars[i] >= 100.0 - kEpsilon && (!best || bars[i] > bars[*best] + kEpsilon))
                best = i;
        if (!best) continue;
        AtbTurn turn;
        turn.tick = tick;
        turn.actor = *best;
        turn.bar = bars[*best];
        for (std::size_t i = 0; i < count; ++i) {
            if (i == *best || std::abs(bars[i] - bars[*best]) > kEpsilon) continue;
            turn.tie = true;
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

SpeedWindow speed_window(std::vector<AtbUnit> units, const std::vector<AtbStep>& script,
                         std::size_t unit, const AtbOptions& options, int sweep_min,
                         int sweep_max) {
    SpeedWindow window;
    if (unit >= units.size() || sweep_min > sweep_max) return window;
    AtbOptions fast = options;
    fast.turns_after_boss = 0;  // seul le premier tour du boss compte
    const auto passes = [&](int speed) {
        units[unit].speed = speed;
        return simulate_atb(units, script, fast).script_before_boss;
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
