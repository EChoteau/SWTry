#pragma once

#include "swcore/reference_db.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace swcore {

enum class StatType {
    None,
    Unknown,
};
using Stat = StatType;

enum class StatValueKind {
    Unknown,
    Flat,
    Percent,
};

struct StatValue {
    StatType stat = StatType::Unknown;
    StatValueKind kind = StatValueKind::Unknown;
    int stat_id = 0;
    int value = 0;
};

struct SecondaryStat {
    StatValue value;
    bool enchanted = false;
    int grind = 0;
};

struct AppliedGrindstone {
    int stat_id = 0;
    int value = 0;
};

struct PlannedEnchantment {
    int stat_id = 0;
    int value = 0;
};

enum class RuneOccupancy {
    Unknown,
    Equipped,
    Inventory,
};

struct Rune {
    std::uint64_t rune_id = 0;
    int slot = 0;
    int set_id = 0;
    int rank = 0;
    int stars = 0;
    int upgrade_level = 0;
    std::optional<StatValue> primary;
    std::optional<StatValue> innate;
    std::vector<SecondaryStat> secondary;
    RuneOccupancy occupancy = RuneOccupancy::Unknown;
    std::uint64_t occupied_id = 0;
    int extra = 0;
    std::uint64_t source_unit_id = 0;
    std::string source_name;
    std::vector<AppliedGrindstone> applied_grindstones;
    std::vector<PlannedEnchantment> planned_enchantments;
};

struct RuneMapping {
    std::unordered_map<int, std::string> stat_names;
    std::unordered_map<int, StatValueKind> stat_kinds;
    std::unordered_map<int, std::string> set_names;

    static RuneMapping load(const std::filesystem::path& path,
                            WarningSink warnings = {});
    std::optional<StatValueKind> kind_for_stat(int stat_id) const;
};

std::vector<Rune> load_runes(const std::filesystem::path& path,
                             WarningSink warnings = {});
std::vector<Rune> load_runes(const std::filesystem::path& path,
                             const RuneMapping& mapping,
                             WarningSink warnings = {});

}  // namespace swcore
