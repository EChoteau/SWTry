#include "swcore/runes.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
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
        warn(warnings, std::string("Runes: missing field '") + field + "'");
        return false;
    }
    try {
        result = it->get<T>();
        return true;
    } catch (const json::exception& error) {
        warn(warnings, std::string("Runes: invalid field '") + field +
                          "': " + error.what());
        return false;
    }
}

std::optional<StatValue> parse_stat_value(const json& value,
                                          const char* field,
                                          const RuneMapping* mapping,
                                          const WarningSink& warnings) {
    if (!value.is_array() || value.size() != 2) {
        warn(warnings, std::string("Runes: field '") + field +
                          "' must contain [stat, value]");
        return std::nullopt;
    }
    int stat_id = 0;
    int amount = 0;
    try {
        stat_id = value[0].get<int>();
        amount = value[1].get<int>();
    } catch (const json::exception& error) {
        warn(warnings, std::string("Runes: invalid field '") + field +
                          "': " + error.what());
        return std::nullopt;
    }
    StatValue result;
    result.stat_id = stat_id;
    result.value = amount;
    if (stat_id == 0) {
        result.stat = StatType::None;
        result.kind = StatValueKind::Unknown;
        return result;
    }
    if (mapping) {
        if (mapping->stat_names.find(stat_id) == mapping->stat_names.end())
            result.stat = StatType::Unknown;
        const auto kind = mapping->kind_for_stat(stat_id);
        if (kind) result.kind = *kind;
    } else {
        result.stat = StatType::Unknown;
    }
    return result;
}

std::optional<SecondaryStat> parse_secondary(const json& value,
                                             const RuneMapping* mapping,
                                             const WarningSink& warnings) {
    if (!value.is_array() || value.size() != 4) {
        warn(warnings, "Runes: secondary stat must contain four values");
        return std::nullopt;
    }
    auto stat = parse_stat_value(
        json::array({value[0], value[1]}), "sec_eff", mapping, warnings);
    if (!stat) return std::nullopt;
    SecondaryStat result;
    result.value = *stat;
    try {
        result.enchanted = value[2].get<int>() != 0;
        result.grind = value[3].get<int>();
    } catch (const json::exception& error) {
        warn(warnings, std::string("Runes: invalid secondary metadata: ") +
                          error.what());
        return std::nullopt;
    }
    return result;
}

std::optional<Rune> parse_rune(const json& object, const RuneMapping* mapping,
                               const WarningSink& warnings) {
    Rune rune;
    int occupancy = 0;
    bool valid = true;
    valid &= read_required(object, "rune_id", rune.rune_id, warnings);
    valid &= read_required(object, "slot_no", rune.slot, warnings);
    valid &= read_required(object, "set_id", rune.set_id, warnings);
    valid &= read_required(object, "rank", rune.rank, warnings);
    valid &= read_required(object, "class", rune.stars, warnings);
    valid &= read_required(object, "upgrade_curr", rune.upgrade_level, warnings);
    valid &= read_required(object, "occupied_type", occupancy, warnings);
    valid &= read_required(object, "occupied_id", rune.occupied_id, warnings);
    valid &= read_required(object, "extra", rune.extra, warnings);
    if (!valid) return std::nullopt;

    if (occupancy == 1) rune.occupancy = RuneOccupancy::Equipped;
    else if (occupancy == 2) rune.occupancy = RuneOccupancy::Inventory;
    else {
        rune.occupancy = RuneOccupancy::Unknown;
        warn(warnings, "Runes: unknown occupied_type " + std::to_string(occupancy));
    }

    const auto primary = object.find("pri_eff");
    const auto innate = object.find("prefix_eff");
    const auto secondary = object.find("sec_eff");
    if (primary == object.end() || innate == object.end() ||
        secondary == object.end()) {
        warn(warnings, "Runes: missing effect field");
        return std::nullopt;
    }
    rune.primary = parse_stat_value(*primary, "pri_eff", mapping, warnings);
    rune.innate = parse_stat_value(*innate, "prefix_eff", mapping, warnings);
    if (!secondary->is_array()) {
        warn(warnings, "Runes: field 'sec_eff' must be an array");
        return std::nullopt;
    }
    for (const auto& value : *secondary) {
        const auto parsed = parse_secondary(value, mapping, warnings);
        if (parsed) rune.secondary.push_back(*parsed);
    }
    return rune;
}

}  // namespace

std::vector<Rune> load_runes(const std::filesystem::path& path,
                             const RuneMapping* mapping,
                             WarningSink warnings);

RuneMapping RuneMapping::load(const std::filesystem::path& path,
                              WarningSink warnings) {
    RuneMapping mapping;
    const auto load_table = [&](const std::filesystem::path& table,
                                std::unordered_map<int, std::string>& target) {
        std::ifstream input(table);
        if (!input) {
            warn(warnings, "RuneMapping: unable to open '" + table.string() + "'");
            return;
        }
        json root;
        try {
            input >> root;
        } catch (const json::exception& error) {
            warn(warnings, std::string("RuneMapping: invalid JSON: ") +
                              error.what());
            return;
        }
        if (!root.is_object()) {
            warn(warnings, "RuneMapping: table must be an object: " + table.string());
            return;
        }
        for (const auto& [id, name] : root.items()) {
            try {
                target[std::stoi(id)] = name.get<std::string>();
            } catch (const std::exception& error) {
                warn(warnings, std::string("RuneMapping: invalid entry in ") +
                                  table.string() + ": " + error.what());
            }
        }
    };

    if (std::filesystem::is_directory(path)) {
        load_table(path / "rune_effectTypes.json", mapping.stat_names);
        for (const auto& [id, name] : mapping.stat_names) {
            if (!name.empty())
                mapping.stat_kinds[id] =
                    name.find('%') != std::string::npos
                        ? StatValueKind::Percent : StatValueKind::Flat;
        }
        load_table(path / "rune_sets.json", mapping.set_names);
        return mapping;
    }

    std::ifstream input(path);
    if (!input) {
        warn(warnings, "RuneMapping: unable to open '" + path.string() + "'");
        return mapping;
    }
    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        warn(warnings, std::string("RuneMapping: invalid JSON: ") + error.what());
        return mapping;
    }
    const auto rune = root.find("rune");
    if (rune == root.end() || !rune->is_object()) {
        warn(warnings, "RuneMapping: missing object 'rune'");
        return mapping;
    }
    if (const auto effects = rune->find("effectTypes");
        effects != rune->end() && effects->is_object()) {
        for (const auto& [id, name] : effects->items())
            mapping.stat_names[std::stoi(id)] = name.get<std::string>();
        for (const auto& [id, name] : mapping.stat_names) {
            if (!name.empty())
                mapping.stat_kinds[id] =
                    name.find('%') != std::string::npos
                        ? StatValueKind::Percent : StatValueKind::Flat;
        }
    }
    if (const auto sets = rune->find("sets");
        sets != rune->end() && sets->is_object()) {
        for (const auto& [id, name] : sets->items())
            mapping.set_names[std::stoi(id)] = name.get<std::string>();
    }
    return mapping;
}

std::optional<StatValueKind> RuneMapping::kind_for_stat(int stat_id) const {
    const auto it = stat_kinds.find(stat_id);
    if (it == stat_kinds.end()) return std::nullopt;
    return it->second;
}

std::vector<Rune> load_runes(const std::filesystem::path& path,
                             WarningSink warnings) {
    return load_runes(path, nullptr, std::move(warnings));
}

std::vector<Rune> load_runes(const std::filesystem::path& path,
                             const RuneMapping& mapping,
                             WarningSink warnings) {
    return load_runes(path, &mapping, std::move(warnings));
}

std::vector<Rune> load_runes(const std::filesystem::path& path,
                             const RuneMapping* mapping,
                             WarningSink warnings) {
    std::vector<Rune> runes;
    std::ifstream input(path);
    if (!input) {
        warn(warnings, "Runes: unable to open '" + path.string() + "'");
        return runes;
    }

    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        warn(warnings, std::string("Runes: invalid JSON: ") + error.what());
        return runes;
    }
    if (!root.is_object()) {
        warn(warnings, "Runes: root must be an object");
        return runes;
    }
    const auto parse_rune_list = [&](const json& list, std::uint64_t unit_id,
                                     const std::string& source_name) {
        if (!list.is_array()) return;
        for (const auto& value : list) {
            if (!value.is_object()) {
                warn(warnings, "Runes: ignored non-object rune");
                continue;
            }
            const auto rune = parse_rune(value, mapping, warnings);
            if (!rune) continue;
            Rune parsed = *rune;
            parsed.source_unit_id = unit_id;
            parsed.source_name = source_name;
            if (parsed.occupancy == RuneOccupancy::Inventory) {
                parsed.source_name = "inventaire";
                parsed.source_unit_id = 0;
            }
            runes.push_back(std::move(parsed));
        }
    };

    const auto inventory = root.find("runes");
    if (inventory != root.end()) parse_rune_list(*inventory, 0, "Inventaire");

    const auto units = root.find("unit_list");
    if (units == root.end() || !units->is_array()) {
        warn(warnings, "Runes: 'unit_list' must be an array");
        return runes;
    }
    for (const auto& unit : *units) {
        if (!unit.is_object()) {
            warn(warnings, "Runes: ignored non-object unit");
            continue;
        }
        const auto unit_runes = unit.find("runes");
        if (unit_runes == unit.end() || !unit_runes->is_array()) {
            warn(warnings, "Runes: unit 'runes' must be an array");
            continue;
        }
        parse_rune_list(*unit_runes, unit.value("unit_id", std::uint64_t{0}),
                        "Monstre " +
                            std::to_string(unit.value("unit_id", std::uint64_t{0})));
    }
    return runes;
}

}  // namespace swcore
