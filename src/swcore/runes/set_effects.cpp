#include "swcore/runes/set_effects.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <string>
#include <utility>

namespace swcore {
namespace {

using json = nlohmann::json;

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

template <typename T>
bool read(const json& object, const char* field, T& result,
          const WarningSink& warnings) {
    const auto it = object.find(field);
    if (it == object.end()) {
        warn(warnings, std::string("SetEffects: missing field '") + field + "'");
        return false;
    }
    try {
        result = it->get<T>();
        return true;
    } catch (const json::exception& error) {
        warn(warnings, std::string("SetEffects: invalid field '") + field +
                          "': " + error.what());
        return false;
    }
}

// Summoners War applies percentage rune/set bonuses to the white stat.
// HP, ATK and DEF use an upward integer rounding at this boundary.
int ceil_percent(int white, int percent) {
    return static_cast<int>(std::ceil(static_cast<double>(white) * percent / 100.0));
}

StatValues add(const StatValues& left, const StatValues& right) {
    return {left.hp + right.hp, left.attack + right.attack,
            left.defense + right.defense, left.speed + right.speed,
            left.crit_rate + right.crit_rate,
            left.crit_damage + right.crit_damage,
            left.resistance + right.resistance, left.accuracy + right.accuracy};
}

}  // namespace

SetEffects SetEffects::load(const std::filesystem::path& path,
                            WarningSink warnings) {
    SetEffects result;
    std::ifstream input(path);
    if (!input) {
        warn(warnings, "SetEffects: unable to open '" + path.string() + "'");
        return result;
    }
    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        warn(warnings, std::string("SetEffects: invalid JSON: ") + error.what());
        return result;
    }
    if (!root.is_object()) {
        warn(warnings, "SetEffects: root must be an object");
        return result;
    }
    for (const auto& [id_text, value] : root.items()) {
        try {
            const int id = std::stoi(id_text);
            if (!value.is_object()) {
                warn(warnings, "SetEffects: entry is not an object: " + id_text);
                continue;
            }
            SetEffect effect;
            const bool valid =
                read(value, "pieces_required", effect.pieces_required, warnings) &&
                read(value, "stat", effect.stat, warnings) &&
                read(value, "value", effect.value, warnings) &&
                read(value, "unit", effect.unit, warnings) &&
                read(value, "type", effect.type, warnings) &&
                read(value, "status", effect.status, warnings);
            if (valid) result.effects.emplace(id, std::move(effect));
        } catch (const std::exception& error) {
            warn(warnings, "SetEffects: invalid id '" + id_text + "': " +
                               error.what());
        }
    }
    return result;
}

std::vector<SetActivation> calculate_set_activations(
    const std::vector<int>& set_ids, const SetEffects& effects) {
    std::map<int, int> counts;
    for (const int id : set_ids) ++counts[id];
    int empty_set_count = 0;
    if (const auto it = counts.find(25); it != counts.end()) {
        empty_set_count = it->second;
        counts.erase(it);
    }
    std::vector<SetActivation> result;
    for (const auto& [id, count] : counts) {
        const auto it = effects.effects.find(id);
        if (it == effects.effects.end() || it->second.pieces_required <= 0)
            continue;
        const int required = it->second.pieces_required;
        const int usable_empty_sets =
            std::min(empty_set_count, required - (count % required));
        const int complete_count = count + usable_empty_sets;
        empty_set_count -= usable_empty_sets;
        result.push_back({id, complete_count / required, it->second});
    }
    return result;
}

StatBlock calculate_stat_block(const StatValues& white,
                               const StatValues& rune_fixed,
                               const StatValues& rune_percent,
                               const StatValues& artifact_fixed,
                               const StatValues& artifact_percent,
                               const StatValues& set_percent) {
    StatBlock result{white, rune_fixed, rune_percent, artifact_fixed,
                     artifact_percent, set_percent, {}};
    const StatValues percent = add(rune_percent, set_percent);
    result.total.hp = white.hp + artifact_fixed.hp + rune_fixed.hp +
        ceil_percent(white.hp, percent.hp);
    result.total.attack = white.attack + artifact_fixed.attack + rune_fixed.attack +
        ceil_percent(white.attack, percent.attack);
    result.total.defense = white.defense + artifact_fixed.defense + rune_fixed.defense +
        ceil_percent(white.defense, percent.defense);
    result.total.speed = white.speed + rune_fixed.speed + artifact_fixed.speed +
        ceil_percent(white.speed, rune_percent.speed + set_percent.speed +
                     artifact_percent.speed);
    result.total.crit_rate = white.crit_rate + rune_fixed.crit_rate +
        artifact_fixed.crit_rate + rune_percent.crit_rate + set_percent.crit_rate +
        artifact_percent.crit_rate;
    result.total.crit_damage = white.crit_damage + rune_fixed.crit_damage +
        artifact_fixed.crit_damage + rune_percent.crit_damage + set_percent.crit_damage +
        artifact_percent.crit_damage;
    result.total.resistance = white.resistance + rune_fixed.resistance +
        artifact_fixed.resistance + rune_percent.resistance + set_percent.resistance +
        artifact_percent.resistance;
    result.total.accuracy = white.accuracy + rune_fixed.accuracy +
        artifact_fixed.accuracy + rune_percent.accuracy + set_percent.accuracy +
        artifact_percent.accuracy;
    return result;
}

}  // namespace swcore
