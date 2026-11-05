#include "swcore/account/account.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace swcore {
namespace {

using json = nlohmann::json;

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

template <typename T>
bool read_required(const json& object, const char* field, T& result,
                   const WarningSink& warnings) {
    const auto it = object.find(field);
    if (it == object.end()) {
        warn(warnings, std::string("Account: missing field '") + field + "'");
        return false;
    }
    try {
        result = it->get<T>();
        return true;
    } catch (const json::exception& error) {
        warn(warnings, std::string("Account: invalid field '") + field +
                          "': " + error.what());
        return false;
    }
}

std::array<double, 5> parse_effect(const json& value,
                                   const WarningSink& warnings) {
    std::array<double, 5> result{};
    if (!value.is_array() || value.size() != result.size()) {
        warn(warnings, "Account: artifact effect must contain five values");
        return result;
    }
    for (std::size_t i = 0; i < result.size(); ++i) {
        try {
            result[i] = value[i].get<double>();
        } catch (const json::exception& error) {
            warn(warnings, std::string("Account: invalid artifact effect: ") +
                              error.what());
        }
    }
    return result;
}

Artifact parse_artifact(const json& object, const WarningSink& warnings) {
    Artifact artifact;
    read_required(object, "rid", artifact.rid, warnings);
    read_required(object, "slot", artifact.slot, warnings);
    read_required(object, "type", artifact.type, warnings);
    read_required(object, "attribute", artifact.attribute, warnings);
    read_required(object, "unit_style", artifact.unit_style, warnings);
    read_required(object, "rank", artifact.rank, warnings);
    read_required(object, "level", artifact.level, warnings);
    read_required(object, "occupied_id", artifact.occupied_id, warnings);
    if (const auto it = object.find("pri_effect"); it != object.end())
        artifact.primary = parse_effect(*it, warnings);
    else
        warn(warnings, "Account: missing field 'pri_effect'");
    if (const auto it = object.find("sec_effects");
        it != object.end() && it->is_array()) {
        for (const auto& effect : *it)
            artifact.secondary.push_back(parse_effect(effect, warnings));
    } else {
        warn(warnings, "Account: missing or invalid field 'sec_effects'");
    }
    return artifact;
}

void add_stat(StatValues& values, int stat_id, int amount, bool percent) {
    if (percent) {
        switch (stat_id) {
        case 2: values.hp += amount; break;
        case 4: values.attack += amount; break;
        case 6: values.defense += amount; break;
        }
        return;
    }
    switch (stat_id) {
    case 1: values.hp += amount; break;
    case 3: values.attack += amount; break;
    case 5: values.defense += amount; break;
    case 8: values.speed += amount; break;
    case 9: values.crit_rate += amount; break;
    case 10: values.crit_damage += amount; break;
    case 11: values.resistance += amount; break;
    case 12: values.accuracy += amount; break;
    default: break;
    }
}

void add_set_stat(StatValues& values, const SetActivation& activation) {
    if (activation.activations <= 0 ||
        activation.effect.status != "confirmed" ||
        activation.effect.type != "personnel" ||
        activation.effect.unit != "%")
        return;
    const int amount = activation.activations * activation.effect.value;
    if (activation.effect.stat == "hp") values.hp += amount;
    else if (activation.effect.stat == "attack") values.attack += amount;
    else if (activation.effect.stat == "def") values.defense += amount;
    else if (activation.effect.stat == "spd") values.speed += amount;
    else if (activation.effect.stat == "crit_rate") values.crit_rate += amount;
    else if (activation.effect.stat == "crit_damage") values.crit_damage += amount;
    else if (activation.effect.stat == "resistance") values.resistance += amount;
    else if (activation.effect.stat == "accuracy") values.accuracy += amount;
}

}  // namespace

std::vector<AccountMonster> load_account(const std::filesystem::path& path,
                                         WarningSink warnings) {
    return load_account(path, RuneMapping{}, std::move(warnings));
}

std::vector<AccountMonster> load_account(const std::filesystem::path& path,
                                         const RuneMapping& rune_mapping,
                                         WarningSink warnings) {
    std::vector<AccountMonster> monsters;
    std::ifstream input(path);
    if (!input) {
        warn(warnings, "Account: unable to open '" + path.string() + "'");
        return monsters;
    }
    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        warn(warnings, std::string("Account: invalid JSON: ") + error.what());
        return monsters;
    }
    const auto units = root.find("unit_list");
    if (units == root.end() || !units->is_array()) {
        warn(warnings, "Account: 'unit_list' must be an array");
        return monsters;
    }
    const auto all_runes = load_runes(path, rune_mapping, warnings);
    std::unordered_multimap<std::uint64_t, Rune> runes_by_unit;
    for (const auto& rune : all_runes) runes_by_unit.emplace(rune.occupied_id, rune);

    for (const auto& value : *units) {
        if (!value.is_object()) {
            warn(warnings, "Account: ignored non-object monster");
            continue;
        }
        AccountMonster monster;
        read_required(value, "unit_id", monster.unit_id, warnings);
        read_required(value, "unit_master_id", monster.unit_master_id, warnings);
        read_required(value, "unit_level", monster.level, warnings);
        read_required(value, "class", monster.stars, warnings);
        read_required(value, "attribute", monster.attribute, warnings);
        read_required(value, "con", monster.con, warnings);
        read_required(value, "atk", monster.attack, warnings);
        read_required(value, "def", monster.defense, warnings);
        read_required(value, "spd", monster.speed, warnings);
        read_required(value, "resist", monster.resistance, warnings);
        read_required(value, "accuracy", monster.accuracy, warnings);
        read_required(value, "critical_rate", monster.crit_rate, warnings);
        read_required(value, "critical_damage", monster.crit_damage, warnings);
        const auto range = runes_by_unit.equal_range(monster.unit_id);
        for (auto it = range.first; it != range.second; ++it)
            monster.runes.push_back(it->second);
        if (const auto artifacts = value.find("artifacts");
            artifacts != value.end() && artifacts->is_array()) {
            for (const auto& artifact : *artifacts) {
                if (artifact.is_object())
                    monster.artifacts.push_back(parse_artifact(artifact, warnings));
                else
                    warn(warnings, "Account: ignored non-object artifact");
            }
        } else {
            warn(warnings, "Account: missing or invalid field 'artifacts'");
        }
        std::stable_sort(monster.runes.begin(), monster.runes.end(),
                         [](const Rune& left, const Rune& right) {
                             return left.slot < right.slot;
                         });
        std::stable_sort(monster.artifacts.begin(), monster.artifacts.end(),
                         [](const Artifact& left, const Artifact& right) {
                             return left.slot < right.slot;
                         });
        monster.stats = calculate_monster_stats(monster, {});
        monsters.push_back(std::move(monster));
    }

    return monsters;
}

StatBlock calculate_monster_stats(const AccountMonster& monster,
                                  const SetEffects& set_effects) {
    StatValues white{monster.con * 15, monster.attack, monster.defense,
                     monster.speed, monster.crit_rate, monster.crit_damage,
                     monster.resistance, monster.accuracy};
    StatValues rune_fixed;
    StatValues rune_percent;
    std::vector<int> set_ids;
    for (const auto& rune : monster.runes) {
        if (rune.occupancy != RuneOccupancy::Equipped) continue;
        set_ids.push_back(rune.set_id);
        if (rune.primary)
            add_stat(rune.primary->kind == StatValueKind::Percent
                         ? rune_percent : rune_fixed,
                     rune.primary->stat_id, rune.primary->value,
                     rune.primary->kind == StatValueKind::Percent);
        if (rune.innate)
            add_stat(rune.innate->kind == StatValueKind::Percent
                         ? rune_percent : rune_fixed,
                     rune.innate->stat_id, rune.innate->value,
                     rune.innate->kind == StatValueKind::Percent);
        for (const auto& secondary : rune.secondary) {
            add_stat(secondary.value.kind == StatValueKind::Percent
                         ? rune_percent : rune_fixed,
                     secondary.value.stat_id,
                     secondary.value.value + secondary.grind,
                     secondary.value.kind == StatValueKind::Percent);
        }
    }
    StatValues artifact_fixed;
    StatValues artifact_percent;
    for (const auto& artifact : monster.artifacts) {
        const int id = static_cast<int>(artifact.primary[0]);
        if (id == 100) artifact_fixed.hp += static_cast<int>(artifact.primary[1]);
        else if (id == 101) artifact_fixed.attack += static_cast<int>(artifact.primary[1]);
        else if (id == 102) artifact_fixed.defense += static_cast<int>(artifact.primary[1]);
    }
    StatValues set_percent;
    for (const auto& activation : calculate_set_activations(set_ids, set_effects))
        add_set_stat(set_percent, activation);
    return calculate_stat_block(white, rune_fixed, rune_percent,
                                artifact_fixed, artifact_percent, set_percent);
}

std::vector<CraftItem> load_craft_items(const std::filesystem::path& path,
                                        WarningSink warnings) {
    std::vector<CraftItem> items;
    std::ifstream input(path);
    if (!input) {
        warn(warnings, "Account: unable to open '" + path.string() + "'");
        return items;
    }
    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        warn(warnings, std::string("Account: invalid JSON: ") + error.what());
        return items;
    }
    const auto list = root.find("rune_craft_item_list");
    if (list == root.end() || !list->is_array()) {
        warn(warnings, "Account: missing or invalid field 'rune_craft_item_list'");
        return items;
    }
    for (const auto& value : *list) {
        if (!value.is_object()) {
            warn(warnings, "Account: ignored non-object craft item");
            continue;
        }
        CraftItem item;
        const bool valid_type = read_required(value, "craft_type",
                                              item.craft_type, warnings);
        const bool valid_id = read_required(value, "craft_type_id",
                                            item.craft_type_id, warnings);
        const bool valid_amount = read_required(value, "amount",
                                                item.amount, warnings);
        if (valid_type && valid_id && valid_amount) items.push_back(item);
    }
    return items;
}

}  // namespace swcore
