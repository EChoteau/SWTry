#pragma once

#include "swcore/reference_db.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace swcore {

enum class StatBlockType {
    Hp,
    Attack,
    Defense,
    Speed,
    CritRate,
    CritDamage,
    Resistance,
    Accuracy,
};

struct SetEffect {
    int pieces_required = 0;
    std::string stat;
    int value = 0;
    std::string unit;
    std::string type;
    std::string status;
};

struct SetEffects {
    std::unordered_map<int, SetEffect> effects;

    static SetEffects load(const std::filesystem::path& path,
                           WarningSink warnings = {});
};

struct StatValues {
    int hp = 0;
    int attack = 0;
    int defense = 0;
    int speed = 0;
    int crit_rate = 0;
    int crit_damage = 0;
    int resistance = 0;
    int accuracy = 0;
};

struct StatBlock {
    StatValues white;
    StatValues rune_fixed;
    StatValues rune_percent;
    StatValues artifact_fixed;
    StatValues artifact_percent;
    StatValues set_percent;
    StatValues total;
};

struct SetActivation {
    int set_id = 0;
    int activations = 0;
    SetEffect effect;
};

std::vector<SetActivation> calculate_set_activations(
    const std::vector<int>& set_ids, const SetEffects& effects);

StatBlock calculate_stat_block(const StatValues& white,
                               const StatValues& rune_fixed,
                               const StatValues& rune_percent,
                               const StatValues& artifact_fixed,
                               const StatValues& artifact_percent,
                               const StatValues& set_percent);

}  // namespace swcore
