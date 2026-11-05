#include "logic/gui_logic.hpp"

#include <algorithm>
#include <numeric>

namespace swgui {

namespace {

int stat_value(const swcore::StatValues& s, int index) {
    switch (index) {
    case 0: return s.hp;
    case 1: return s.attack;
    case 2: return s.defense;
    case 3: return s.speed;
    case 4: return s.crit_rate;
    case 5: return s.crit_damage;
    case 6: return s.resistance;
    default: return s.accuracy;
    }
}

int compare(const ResultRow& a, const ResultRow& b, int column,
            const std::vector<std::string>& labels) {
    const auto cmp = [](const auto& x, const auto& y) { return x < y ? -1 : y < x ? 1 : 0; };
    switch (column) {
    case kColScenario: return cmp(a.scenario, b.scenario);
    case kColSets: return cmp(labels[a.sets_label], labels[b.sets_label]);
    case kColScore: return cmp(a.score, b.score);
    case kColEffectiveHp: return cmp(a.effective_hp, b.effective_hp);
    case kColEffectiveHpBroken: return cmp(a.effective_hp_broken_def, b.effective_hp_broken_def);
    case kColDamage: return cmp(a.expected_damage, b.expected_damage);
    case kColEfficiency: return cmp(a.efficiency, b.efficiency);
    case kColCraft: return cmp(a.grinds + a.gems, b.grinds + b.gems);
    case kColIds: return cmp(a.rune_ids, b.rune_ids);
    default:
        if (column >= kColHp && column <= kColAccuracy)
            return cmp(stat_value(a.total, column - kColHp), stat_value(b.total, column - kColHp));
        return 0;
    }
}

}  // namespace

std::vector<std::uint32_t> sorted_result_order(const std::vector<ResultRow>& rows,
                                               const SortKeys& keys,
                                               const std::vector<std::string>& set_labels) {
    std::vector<std::uint32_t> order(rows.size());
    std::iota(order.begin(), order.end(), std::uint32_t{0});
    if (keys.empty()) return order;
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t l, std::uint32_t r) {
        for (const auto& [column, ascending] : keys) {
            const int result = compare(rows[l], rows[r], column, set_labels);
            if (result != 0) return ascending ? result < 0 : result > 0;
        }
        return false;
    });
    return order;
}

std::vector<std::uint32_t> filter_scenario(const std::vector<std::uint32_t>& order,
                                           const std::vector<ResultRow>& rows, int scenario) {
    if (scenario < 0) return order;
    std::vector<std::uint32_t> filtered;
    for (const auto index : order)
        if (rows[index].scenario == scenario) filtered.push_back(index);
    return filtered;
}

std::size_t Pager::page_count(std::size_t rows, std::size_t page_size) {
    if (page_size == 0) page_size = 1;
    return std::max<std::size_t>(1, (rows + page_size - 1) / page_size);
}

void Pager::clamp(std::size_t rows) {
    page = std::clamp<std::size_t>(page, 1, page_count(rows, page_size));
}

void Pager::set_page_size(std::size_t size) {
    page_size = std::max<std::size_t>(1, size);
    page = 1;
}

std::pair<std::size_t, std::size_t> Pager::range(std::size_t rows) const {
    const std::size_t size = std::max<std::size_t>(1, page_size);
    const std::size_t current = std::clamp<std::size_t>(page, 1, page_count(rows, size));
    const std::size_t first = std::min(rows, (current - 1) * size);
    return {first, std::min(rows, first + size)};
}

std::vector<std::size_t> page_buttons(std::size_t current, std::size_t count) {
    std::vector<std::size_t> pages;
    if (count <= 7) {
        for (std::size_t p = 1; p <= count; ++p) pages.push_back(p);
        return pages;
    }
    // Première, dernière, et une fenêtre de 5 autour de la page courante.
    std::size_t from = current > 2 ? current - 2 : 1;
    std::size_t to = std::min(count, from + 4);
    from = to >= 5 ? std::min(from, to - 4) : 1;
    if (from > 1) {
        pages.push_back(1);
        if (from > 2) pages.push_back(0);
    }
    for (std::size_t p = from; p <= to; ++p) pages.push_back(p);
    if (to < count) {
        if (to + 1 < count) pages.push_back(0);
        pages.push_back(count);
    }
    return pages;
}

LockScopeState lock_scope_state(const swcore::LockTable& locks,
                                const swcore::AccountMonster& monster, swcore::LockKind kind) {
    LockScopeState state;
    const auto mine = [&](std::uint64_t id) {
        const auto* lock = locks.find(kind, id);
        return lock && lock->reserved_for == monster.unit_id;
    };
    if (kind == swcore::LockKind::Rune) {
        for (const auto& rune : monster.runes) {
            if (rune.occupancy != swcore::RuneOccupancy::Equipped) continue;
            ++state.total;
            state.locked += mine(rune.rune_id) ? 1 : 0;
        }
    } else {
        for (const auto& artifact : monster.artifacts) {
            ++state.total;
            state.locked += mine(artifact.rid) ? 1 : 0;
        }
    }
    return state;
}

swcore::LockState combined_lock_state(const LockScopeState& runes,
                                      const LockScopeState& artifacts) {
    const int locked = runes.locked + artifacts.locked;
    const int total = runes.total + artifacts.total;
    if (locked == 0) return swcore::LockState::None;
    return locked == total ? swcore::LockState::Full : swcore::LockState::Partial;
}

}  // namespace swgui
