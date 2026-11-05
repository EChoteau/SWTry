#pragma once

#include "swcore/runes/runes.hpp"
#include "swcore/runes/set_effects.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace swcore {

struct Artifact {
    std::uint64_t rid = 0;
    int slot = 0;
    int type = 0;
    int attribute = 0;
    int unit_style = 0;
    int rank = 0;
    int level = 0;
    std::array<double, 5> primary{};
    std::vector<std::array<double, 5>> secondary;
    std::uint64_t occupied_id = 0;
};

struct AccountMonster {
    std::uint64_t unit_id = 0;
    int unit_master_id = 0;
    int level = 0;
    int stars = 0;
    int attribute = 0;
    int con = 0;
    int attack = 0;
    int defense = 0;
    int speed = 0;
    int resistance = 0;
    int accuracy = 0;
    int crit_rate = 0;
    int crit_damage = 0;
    std::vector<Rune> runes;
    std::vector<Artifact> artifacts;
    StatBlock stats;
};

struct CraftItem {
    int craft_type = 0;
    int craft_type_id = 0;
    int amount = 0;
};

std::vector<AccountMonster> load_account(const std::filesystem::path& path,
                                         WarningSink warnings = {});
std::vector<AccountMonster> load_account(const std::filesystem::path& path,
                                         const RuneMapping& mapping,
                                         WarningSink warnings = {});
StatBlock calculate_monster_stats(const AccountMonster& monster,
                                  const SetEffects& set_effects);
std::vector<CraftItem> load_craft_items(const std::filesystem::path& path,
                                        WarningSink warnings = {});

}  // namespace swcore
