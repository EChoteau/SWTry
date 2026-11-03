#include "monster_view.hpp"

#include "swcore/solver.hpp"

namespace swgui {

namespace {

std::array<int, 8> values(const swcore::StatValues& s) {
    return {s.hp, s.attack, s.defense, s.speed, s.crit_rate, s.crit_damage,
            s.resistance, s.accuracy};
}

std::array<double, 3> metrics(const swcore::StatValues& s) {
    const auto derived = swcore::calculate_derived_metrics(s);
    return {derived.expected_damage, derived.effective_hp,
            derived.effective_hp_broken_def};
}

}  // namespace

MonsterStatsView monster_stats_view(const swcore::AccountMonster& monster) {
    MonsterStatsView view;
    view.base = values(monster.stats.white);
    view.current = values(monster.stats.total);
    view.base_metrics = metrics(monster.stats.white);
    view.current_metrics = metrics(monster.stats.total);
    double sum = 0;
    for (const auto& rune : monster.runes) {
        if (rune.occupancy != swcore::RuneOccupancy::Equipped) continue;
        sum += swcore::calculate_rune_efficiency(rune);
        ++view.runes;
        if (rune.slot >= 1 && rune.slot <= 6)
            view.rune_slots[static_cast<std::size_t>(rune.slot - 1)] = true;
    }
    if (view.runes > 0) view.efficiency = sum / view.runes;
    view.artifacts = static_cast<int>(monster.artifacts.size());
    return view;
}

}  // namespace swgui
