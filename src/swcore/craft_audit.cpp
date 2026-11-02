#include "swcore/craft_audit.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <string>

namespace swcore {
namespace {

using json = nlohmann::json;

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

bool in_any_range(const CraftRanges& ranges, int stat_id, int value) {
    const auto stat = ranges.find(stat_id);
    if (stat == ranges.end()) return false;
    return std::any_of(stat->second.begin(), stat->second.end(),
                       [&](const auto& rarity) {
                           return value >= rarity.second.first &&
                                  value <= rarity.second.second;
                       });
}

void keep_min(std::optional<int>& current, int value) {
    if (!current || value < *current) current = value;
}

}  // namespace

CraftRanges load_craft_ranges(const std::filesystem::path& path,
                              WarningSink warnings) {
    CraftRanges ranges;
    std::ifstream input(path);
    if (!input) {
        warn(warnings, "Craft: unable to open '" + path.string() + "'");
        return ranges;
    }
    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        warn(warnings, std::string("Craft: invalid JSON: ") + error.what());
        return ranges;
    }
    if (!root.is_object()) return ranges;
    for (const auto& [stat_text, entry] : root.items()) {
        try {
            const auto range = entry.find("range");
            if (range == entry.end() || !range->is_object()) continue;
            for (const auto& [rarity_text, bounds] : range->items())
                if (bounds.is_object() && bounds.contains("min") &&
                    bounds.contains("max"))
                    ranges[std::stoi(stat_text)][std::stoi(rarity_text)] = {
                        bounds.at("min").get<int>(), bounds.at("max").get<int>()};
        } catch (const std::exception& error) {
            warn(warnings, "Craft: invalid entry '" + stat_text + "' in '" +
                               path.string() + "': " + error.what());
        }
    }
    return ranges;
}

bool is_grindable_stat(int stat_id) {
    return (stat_id >= 1 && stat_id <= 6) || stat_id == 8;
}

CraftAudit audit_craft(const std::vector<Rune>& runes,
                       const CraftRanges& grindstones, const CraftRanges& gems) {
    CraftAudit audit;
    for (const auto& rune : runes) {
        ++audit.runes;
        std::size_t gems_on_rune = 0;
        for (const auto& line : rune.secondary) {
            const int stat = line.value.stat_id;
            if (line.grind > 0) {
                ++audit.ground_lines;
                keep_min(audit.min_level_ground, rune.upgrade_level);
                const CraftLineIssue issue{rune.rune_id, stat, line.grind};
                if (!is_grindable_stat(stat))
                    audit.ground_bad_stat.push_back(issue);
                else if (!in_any_range(grindstones, stat, line.grind))
                    audit.ground_out_of_range.push_back(issue);
            }
            if (!line.enchanted) continue;
            ++gems_on_rune;
            ++audit.gem_lines;
            keep_min(audit.min_level_gem, rune.upgrade_level);
            if (!in_any_range(gems, stat, line.value.value))
                audit.gem_out_of_range.push_back({rune.rune_id, stat, line.value.value});
            if (rune.primary && rune.primary->stat_id == stat)
                audit.gem_conflicts.push_back(
                    {rune.rune_id, stat, GemConflict::MainStat});
            const auto same_stat = std::count_if(
                rune.secondary.begin(), rune.secondary.end(),
                [&](const SecondaryStat& other) {
                    return other.value.stat_id == stat;
                });
            if (same_stat > 1)
                audit.gem_conflicts.push_back(
                    {rune.rune_id, stat, GemConflict::OtherSubstat});
            const bool has_innate =
                rune.innate && rune.innate->stat != StatType::None;
            if (has_innate) {
                ++audit.gem_runes_with_innate;
                if (rune.innate->stat_id == stat) ++audit.gem_same_as_innate;
            }
            if (line.grind > 0) ++audit.gem_with_grind;
        }
        if (gems_on_rune > 1) audit.runes_with_several_gems.push_back(rune.rune_id);
    }
    return audit;
}

}  // namespace swcore
