#include "swcore/solver_config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <string>

namespace swcore {
namespace {

using json = nlohmann::json;

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

std::optional<json> read_json(const std::filesystem::path& path,
                              const WarningSink& warnings);

std::map<std::string, int> load_named_ids(const std::filesystem::path& path,
                                          const WarningSink& warnings) {
    std::map<std::string, int> result;
    const auto root = read_json(path, warnings);
    if (!root || !root->is_object()) return result;
    for (const auto& [id_text, value] : root->items()) {
        try {
            if (value.is_string())
                result.emplace(value.get<std::string>(), std::stoi(id_text));
        } catch (const std::exception& error) {
            warn(warnings, "SolverConfig: invalid named mapping entry '" +
                               id_text + "': " + error.what());
        }
    }
    return result;
}

std::optional<int> read_id_or_name(const json& value,
                                   const std::map<std::string, int>& names,
                                   const WarningSink& warnings,
                                   const char* kind) {
    try {
        if (value.is_number_integer()) return value.get<int>();
        if (value.is_string()) {
            auto name = value.get<std::string>();
            if (kind == std::string{"set"} && name == "Lame") name = "Blade";
            if (kind == std::string{"stat"} && name == "ATQ%") name = "ATK%";
            const auto it = names.find(name);
            if (it != names.end()) return it->second;
            warn(warnings, std::string("SolverConfig: unknown ") + kind +
                               " name '" + name + "'");
        }
    } catch (const json::exception& error) {
        warn(warnings, std::string("SolverConfig: invalid ") + kind + ": " +
                           error.what());
    }
    return std::nullopt;
}

bool is_allowed_grindstone_stat(int stat_id) {
    return stat_id == 1 || stat_id == 2 || stat_id == 3 || stat_id == 4 ||
           stat_id == 5 || stat_id == 6 || stat_id == 8;
}

std::optional<json> read_json(const std::filesystem::path& path,
                              const WarningSink& warnings) {
    std::ifstream input(path);
    if (!input) {
        warn(warnings, "SolverConfig: unable to open '" + path.string() + "'");
        return std::nullopt;
    }
    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        warn(warnings, std::string("SolverConfig: invalid JSON: ") +
                          error.what());
        return std::nullopt;
    }
    return root;
}

template <typename T>
bool read(const json& object, const char* field, T& result,
          const WarningSink& warnings) {
    const auto it = object.find(field);
    if (it == object.end()) return false;
    try {
        result = it->get<T>();
        return true;
    } catch (const json::exception& error) {
        warn(warnings, std::string("SolverConfig: invalid field '") + field +
                          "': " + error.what());
        return false;
    }
}

std::optional<GrindstoneMode> parse_grindstone_mode(const std::string& value) {
    if (value == "off") return GrindstoneMode::Off;
    if (value == "min") return GrindstoneMode::Min;
    if (value == "max") return GrindstoneMode::Max;
    return std::nullopt;
}

void load_mainstat_ids(const std::filesystem::path& path,
                       std::set<int>& ids, const WarningSink& warnings) {
    const auto root = read_json(path, warnings);
    if (!root || !root->is_object()) return;
    for (const auto& [id_text, value] : root->items()) {
        try {
            const int id = std::stoi(id_text);
            if (!value.is_object() || !value.contains("max")) {
                warn(warnings, "SolverConfig: invalid rune_mainstat entry " +
                                   id_text);
                continue;
            }
            ids.insert(id);
        } catch (const std::exception& error) {
            warn(warnings, "SolverConfig: invalid main stat id '" + id_text +
                               "': " + error.what());
        }
    }
}

void load_grindstone_ranges(const std::filesystem::path& path,
                            std::map<int, std::set<int>>& ranges,
                            const WarningSink& warnings) {
    const auto root = read_json(path, warnings);
    if (!root || !root->is_object()) return;
    for (const auto& [id_text, value] : root->items()) {
        try {
            const int stat_id = std::stoi(id_text);
            const auto range = value.find("range");
            if (range == value.end() || !range->is_object()) continue;
            for (const auto& [rarity, bounds] : range->items()) {
                if (bounds.is_object() && bounds.contains("min") &&
                    bounds.contains("max"))
                    ranges[stat_id].insert(std::stoi(rarity));
            }
        } catch (const std::exception& error) {
            warn(warnings, "SolverConfig: invalid grindstone mapping entry '" +
                               id_text + "': " + error.what());
        }
    }
}

}  // namespace

SolverConfig SolverConfig::load(const std::filesystem::path& path,
                                const std::filesystem::path& mapping_directory,
                                WarningSink warnings) {
    SolverConfig config;
    const auto root = read_json(path, warnings);
    if (!root || !root->is_object()) return config;

    if (const auto pool = root->find("pool");
        pool != root->end() && pool->is_object()) {
        std::uint64_t target_unit_id = 0;
        if (read(*pool, "target_unit_id", target_unit_id, warnings))
            config.pool.target_unit_id = target_unit_id;
        read(*pool, "inventory_only", config.pool.inventory_only, warnings);
        read(*pool, "minimum_rune_level", config.pool.minimum_rune_level,
             warnings);
        read(*pool, "minimum_stars", config.pool.minimum_stars, warnings);
        if (const auto locked = pool->find("locked_unit_ids");
            locked != pool->end() && locked->is_array()) {
            for (const auto& value : *locked) {
                try {
                    config.pool.locked_unit_ids.push_back(value.get<std::uint64_t>());
                } catch (const json::exception& error) {
                    warn(warnings, std::string("SolverConfig: invalid locked unit: ") +
                                      error.what());
                }
            }
        }
    }

    const auto stat_names =
        load_named_ids(mapping_directory / "rune_effectTypes.json", warnings);
    const auto set_names =
        load_named_ids(mapping_directory / "rune_sets.json", warnings);

    if (const auto scenarios = root->find("scenarios");
        scenarios != root->end() && scenarios->is_array()) {
        for (const auto& value : *scenarios) {
            if (!value.is_object()) {
                warn(warnings, "SolverConfig: ignored non-object scenario");
                continue;
            }
            SetScenario scenario;
            read(value, "name", scenario.name, warnings);
            read(value, "any_set", scenario.any_set, warnings);
            if (const auto sets = value.find("sets");
                sets != value.end() && sets->is_array()) {
                for (const auto& set : *sets) {
                    if (!set.is_object()) continue;
                    SetRequirement requirement;
                    const auto set_id = set.find("set_id");
                    if (set_id != set.end()) {
                        const auto resolved =
                            read_id_or_name(*set_id, set_names, warnings, "set");
                        if (!resolved) continue;
                        requirement.set_id = *resolved;
                    }
                    if (read(set, "pieces", requirement.pieces, warnings))
                        scenario.sets.push_back(requirement);
                }
            } else if (sets != value.end() && sets->is_object()) {
                for (const auto& [name, pieces] : sets->items()) {
                    const auto set_id = read_id_or_name(
                        json(name), set_names, warnings, "set");
                    if (!set_id || !pieces.is_number_integer()) continue;
                    scenario.sets.push_back(
                        {*set_id, pieces.get<int>()});
                }
            }
            if (const auto allowed = root->find("allowed_sets");
                allowed != root->end() && allowed->is_array()) {
                for (const auto& value : *allowed) {
                    const auto id = read_id_or_name(value, set_names, warnings, "set");
                    if (id) config.allowed_sets.push_back(*id);
                }
            }
            config.scenarios.push_back(std::move(scenario));
        }
    }

    std::set<int> known_main_stats;
    load_mainstat_ids(mapping_directory / "rune_mainstat.json",
                      known_main_stats, warnings);
    if (const auto main_stats = root->find("main_stats");
        main_stats != root->end() && main_stats->is_object()) {
        for (int slot = 2; slot <= 6; ++slot) {
            const auto entry = main_stats->find(std::to_string(slot));
            if (entry == main_stats->end()) continue;
            MainStatSelection selection;
            if (entry->is_string() && entry->get<std::string>() == "all") {
                selection.all = true;
            } else if (entry->is_array()) {
                for (const auto& stat : *entry) {
                    try {
                        const auto resolved =
                            read_id_or_name(stat, stat_names, warnings, "stat");
                        if (!resolved) continue;
                        const int id = *resolved;
                        if (!known_main_stats.contains(id))
                            warn(warnings, "SolverConfig: unknown main stat " +
                                               std::to_string(id));
                        else
                            selection.stat_ids.push_back(id);
                    } catch (const json::exception& error) {
                        warn(warnings, std::string("SolverConfig: invalid main stat: ") +
                                          error.what());
                    }
                }
            } else {
                warn(warnings, "SolverConfig: main_stats slot must be 'all' or array");
            }
            config.main_stats[slot] = selection;
        }
    }

    if (const auto constraints = root->find("constraints");
        constraints != root->end() && constraints->is_object()) {
        for (const auto& [name, value] : constraints->items()) {
            if (!value.is_object()) continue;
            StatConstraint constraint;
            double minimum = 0;
            double maximum = 0;
            if (read(value, "min", minimum, warnings)) constraint.minimum = minimum;
            if (read(value, "max", maximum, warnings)) constraint.maximum = maximum;
            config.constraints.emplace(name, constraint);
        }
    }

    if (const auto grindstones = root->find("grindstones");
        grindstones != root->end() && grindstones->is_object()) {
        std::map<int, std::set<int>> known_grindstones;
        load_grindstone_ranges(mapping_directory / "grindstone.json",
                               known_grindstones, warnings);
        read(*grindstones, "allow_extended_stats",
             config.allow_extended_grindstone_stats, warnings);
        if (const auto rules = grindstones->find("stats");
            rules != grindstones->end() && rules->is_object()) {
            for (const auto& [id_text, value] : rules->items()) {
                try {
                    const int stat_id = std::stoi(id_text);
                    if (!config.allow_extended_grindstone_stats &&
                        !is_allowed_grindstone_stat(stat_id)) {
                        warn(warnings, "SolverConfig: grindstone stat ignored: " +
                                           id_text);
                        continue;
                    }
                    if (!known_grindstones.contains(stat_id)) {
                        warn(warnings, "SolverConfig: unknown grindstone stat: " +
                                           id_text);
                        continue;
                    }
                    GrindstoneRule rule;
                    std::string mode;
                    if (!value.is_object() ||
                        !read(value, "mode", mode, warnings) ||
                        !read(value, "rarity", rule.rarity, warnings)) continue;
                    const auto parsed = parse_grindstone_mode(mode);
                    if (!parsed) {
                        warn(warnings, "SolverConfig: unknown grindstone mode: " + mode);
                        continue;
                    }
                    if (!known_grindstones[stat_id].contains(rule.rarity)) {
                        warn(warnings, "SolverConfig: unsupported grindstone rarity " +
                                           std::to_string(rule.rarity) + " for stat " +
                                           id_text);
                        continue;
                    }
                    rule.mode = *parsed;
                    config.grindstones.emplace(stat_id, rule);
                } catch (const std::exception& error) {
                    warn(warnings, "SolverConfig: invalid grindstone stat '" +
                                       id_text + "': " + error.what());
                }
            }
        }
    }

    if (const auto objective = root->find("objective");
        objective != root->end() && objective->is_object()) {
        read(*objective, "mode", config.objective.mode, warnings);
        if (const auto weights = objective->find("weights");
            weights != objective->end() && weights->is_object())
            for (const auto& [name, value] : weights->items())
                if (value.is_number()) config.objective.weights[name] = value.get<double>();
        if (const auto caps = objective->find("caps");
            caps != objective->end() && caps->is_object())
            for (const auto& [name, value] : caps->items())
                if (value.is_number()) config.objective.caps[name] = value.get<double>();
        read(*objective, "sort_metrics", config.objective.sort_metrics, warnings);
    }

    if (const auto limits = root->find("limits");
        limits != root->end() && limits->is_object()) {
        read(*limits, "top_n", config.limits.top_n, warnings);
        read(*limits, "time_budget_ms", config.limits.time_budget_ms, warnings);
        if (const auto seconds = limits->find("time_budget_s");
            seconds != limits->end() && seconds->is_number()) {
            config.limits.time_budget_ms =
                static_cast<std::int64_t>(seconds->get<double>() * 1000.0);
        }
        read(*limits, "node_limit", config.limits.node_limit, warnings);
        read(*limits, "max_builds_per_scenario",
             config.limits.max_builds_per_scenario, warnings);
        read(*limits, "one_build_per_set_composition",
             config.limits.one_build_per_set_composition, warnings);
    }

    return config;
}

}  // namespace swcore
