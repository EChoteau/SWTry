#include "swcore/account.hpp"
#include "swcore/account_bonuses.hpp"
#include "swcore/reference_db.hpp"
#include "swcore/runes.hpp"
#include "swcore/storage_stacks.hpp"
#include "swcore/set_effects.hpp"
#include "swcore/solver_config.hpp"
#include "swcore/solver.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

struct Options {
    fs::path export_path;
    fs::path solver_config_path;
    int solver_top = 0;
    std::string solver_sort;
    fs::path storage_path;
    int show_count = 0;
    std::string name_filter;
    bool runed_only = false;
    std::string format = "text";
    bool ascii = false;
};

std::string unknown(int id) {
    return "[inconnu: " + std::to_string(id) + "]";
}

std::string element_name(swcore::Element element) {
    switch (element) {
    case swcore::Element::Fire: return "Fire";
    case swcore::Element::Water: return "Water";
    case swcore::Element::Wind: return "Wind";
    case swcore::Element::Light: return "Light";
    case swcore::Element::Dark: return "Dark";
    case swcore::Element::Pure: return "Pure";
    case swcore::Element::Unknown: return "[inconnu: element]";
    }
    return "[inconnu: element]";
}

std::string stat_name(const swcore::RuneMapping& mapping, int id) {
    const auto it = mapping.stat_names.find(id);
    return it == mapping.stat_names.end() ? unknown(id) : it->second;
}

std::string set_name(const swcore::RuneMapping& mapping, int id) {
    const auto it = mapping.set_names.find(id);
    return it == mapping.set_names.end() ? unknown(id) : it->second;
}

std::string origin_name(const swcore::Rune& rune) {
    if (rune.occupancy == swcore::RuneOccupancy::Inventory)
        return "Inventaire";
    return rune.source_name.empty() ? "Monstre " +
                                           std::to_string(rune.source_unit_id)
                                    : rune.source_name;
}

using IdNames = std::map<int, std::string>;

std::string format_number(double value) {
    std::ostringstream output;
    output << std::setprecision(12) << value;
    return output.str();
}

void print_effect(const swcore::RuneMapping& mapping,
                  const swcore::StatValue& effect);

const char* stat_label(std::size_t index) {
    static constexpr const char* labels[] = {
        "PV", "ATQ", "DEF", "VIT", "Tx crit.", "Dgts crit.", "RES", "Préc."};
    return index < 8 ? labels[index] : "?";
}

json build_report_json(const swcore::BuildReport& report) {
    json result{
        {"monster_unit_id", report.monster_unit_id},
        {"monster_master_id", report.monster_master_id},
        {"level", report.monster_level},
        {"scenario", report.scenario},
        {"rank", report.rank},
        {"score", report.score},
        {"metrics",
         {{"effective_hp", report.metrics.effective_hp},
          {"effective_hp_broken_def", report.metrics.effective_hp_broken_def},
          {"expected_damage", report.metrics.expected_damage},
          {"rune_efficiency", report.metrics.rune_efficiency}}},
        {"stats", json::array()},
        {"equipped_stats", json::array()},
        {"runes", json::array()}};
    for (std::size_t i = 0; i < report.stats.size(); ++i) {
        result["stats"].push_back(
            {{"name", stat_label(i)},
             {"white", report.stats[i].white},
             {"green", report.stats[i].green},
             {"total", report.stats[i].total}});
        result["equipped_stats"].push_back(
            {{"name", stat_label(i)},
             {"white", report.equipped_stats[i].white},
             {"green", report.equipped_stats[i].green},
             {"total", report.equipped_stats[i].total}});
    }
    for (const auto& view : report.runes)
        result["runes"].push_back(
            {{"slot", view.rune.slot},
             {"id", view.rune.rune_id},
             {"set_id", view.rune.set_id},
             {"stars", view.rune.stars},
             {"upgrade_level", view.rune.upgrade_level},
             {"origin", view.rune.source_name},
             {"source_unit_id", view.rune.source_unit_id},
             {"efficiency", view.efficiency}});
    return result;
}

void print_build_report(const swcore::BuildReport& report,
                        const swcore::RuneMapping& mapping) {
    std::cout << "\n#" << report.rank << "  " << report.scenario
              << "  score=" << report.score << "  monstre="
              << report.monster_master_id << " niveau=" << report.monster_level
              << "\nStat                 Blanc / vert / total\n";
    for (std::size_t i = 0; i < report.stats.size(); ++i)
        std::cout << std::left << std::setw(20) << stat_label(i) << " "
                  << report.stats[i].white << " / "
                  << report.stats[i].green << " / "
                  << report.stats[i].total << '\n';
    std::cout << std::left << std::setw(20) << "DMG" << " "
              << format_number(report.metrics.expected_damage) << '\n'
              << std::left << std::setw(20) << "PV eff." << " "
              << format_number(report.metrics.effective_hp) << '\n'
              << std::left << std::setw(20) << "PV eff. DEF brisée" << " "
              << format_number(report.metrics.effective_hp_broken_def) << '\n'
              << std::left << std::setw(20) << "Efficacité %" << " "
              << format_number(report.metrics.rune_efficiency) << "\n\n";
    constexpr int top_slots[] = {6, 1, 2};
    constexpr int bottom_slots[] = {5, 4, 3};
    const auto print_card = [&](int slot) {
        const auto& view = report.runes[static_cast<std::size_t>(slot - 1)];
        std::cout << "slot " << slot << "  +" << view.rune.upgrade_level
                  << " " << set_name(mapping, view.rune.set_id)
                  << " (" << view.rune.rune_id << ") "
                  << view.rune.stars << "*"
                  << (view.rune.stars == 16 ? " antique" : "") << "  "
                  << "  eff=" << format_number(view.efficiency) << "% "
                  << origin_name(view.rune)
                  << '\n';
        std::cout << "  principale: ";
        if (view.rune.primary)
            print_effect(mapping, *view.rune.primary);
        else
            std::cout << "aucun";
        std::cout << "  innate: ";
        if (view.rune.innate)
            print_effect(mapping, *view.rune.innate);
        else
            std::cout << "aucun";
        std::cout << '\n';
        for (const auto& substat : view.rune.secondary)
            std::cout << "  " << stat_name(mapping, substat.value.stat_id)
                      << " " << substat.value.value + substat.grind
                      << " [" << substat.grind << "]"
                      << (substat.enchanted ? " (gemme)" : "") << '\n';
    };
    for (int slot : top_slots) print_card(slot);
    std::cout << '\n';
    for (int slot : bottom_slots) print_card(slot);
}

IdNames load_id_names(const fs::path& path, const swcore::WarningSink& warning) {
    std::ifstream input(path);
    if (!input) {
        if (warning) warning("mapping introuvable: " + path.string());
        return {};
    }
    json root;
    try {
        input >> root;
    } catch (const json::parse_error& error) {
        if (warning) warning("mapping JSON invalide " + path.string() + ": " +
                             error.what());
        return {};
    }
    if (!root.is_object()) {
        if (warning) warning("mapping JSON non objet: " + path.string());
        return {};
    }
    IdNames names;
    for (const auto& [id, name] : root.items()) {
        try {
            names.emplace(std::stoi(id), name.get<std::string>());
        } catch (const std::exception& error) {
            if (warning) warning("entree de mapping ignoree dans " +
                                 path.string() + " (" + id + "): " +
                                 error.what());
        }
    }
    return names;
}

std::string mapped_name(const IdNames& names, int id) {
    if (id == 0) return "aucun";
    const auto it = names.find(id);
    return it == names.end() ? unknown(id) : it->second;
}

std::string artifact_template(const IdNames& templates, int id, double value) {
    if (id == 0) return "aucun";
    const auto it = templates.find(id);
    if (it == templates.end()) return unknown(id);
    std::string result = it->second;
    const std::string needle = "${value}";
    const std::string replacement = format_number(value);
    std::size_t position = 0;
    while ((position = result.find(needle, position)) != std::string::npos) {
        result.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
    return result;
}

void print_effect(const swcore::RuneMapping& mapping,
                  const swcore::StatValue& effect) {
    if (effect.stat == swcore::StatType::None) {
        std::cout << "aucun";
        return;
    }
    std::cout << stat_name(mapping, effect.stat_id) << " " << effect.value;
}

void print_rune(const swcore::RuneMapping& mapping, const swcore::Rune& rune) {
    std::cout << "    Rune slot=" << rune.slot
              << " set=" << set_name(mapping, rune.set_id)
              << " (" << rune.set_id << ")"
              << " stars=" << rune.stars
              << " +" << rune.upgrade_level << '\n';
    std::cout << "      primary: ";
    if (rune.primary) print_effect(mapping, *rune.primary);
    else std::cout << "[inconnu: stat]";
    std::cout << '\n';
    std::cout << "      innate: ";
    if (rune.innate) print_effect(mapping, *rune.innate);
    else std::cout << "[inconnu: stat]";
    std::cout << '\n';
    std::cout << "      substats:";
    if (rune.secondary.empty()) {
        std::cout << " none\n";
        return;
    }
    std::cout << '\n';
    for (const auto& secondary : rune.secondary) {
        std::cout << "        ";
        print_effect(mapping, secondary.value);
        std::cout << " total=" << secondary.value.value + secondary.grind
                  << " (base=" << secondary.value.value
                  << ", meule=" << secondary.grind
                  << ", enchante=" << (secondary.enchanted ? "oui" : "non")
                  << ")\n";
    }
}

void print_artifact(const swcore::Artifact& artifact, const IdNames& main_effects,
                    const IdNames& sub_effects, const IdNames& types,
                    const IdNames& attributes, const IdNames& ranks) {
    std::cout << "    Artefact rid=" << artifact.rid
              << " slot=" << artifact.slot
              << " type=" << mapped_name(types, artifact.type)
              << " attribute=" << mapped_name(attributes, artifact.attribute)
              << " unit_style=" << artifact.unit_style
              << " rank=" << mapped_name(ranks, artifact.rank)
              << " level=" << artifact.level << '\n';
    const int primary_id = static_cast<int>(artifact.primary[0]);
    std::cout << "      primary=" << mapped_name(main_effects, primary_id)
              << " value=" << format_number(artifact.primary[1]) << '\n';
    for (const auto& effect : artifact.secondary) {
        const int effect_id = static_cast<int>(effect[0]);
        std::cout << "      secondary="
                  << artifact_template(sub_effects, effect_id, effect[1])
                  << " value=" << format_number(effect[1]) << '\n';
    }
}

void print_account_bonuses(const swcore::AccountBonuses& bonuses) {
    std::cout << "Bonus de compte:\n";
    for (const auto& bonus : bonuses.values()) {
        std::cout << "  " << swcore::bonus_source_name(bonus.source)
                  << " id=" << bonus.id << " niveau=" << bonus.level << ": ";
        if (!bonus.definition) {
            std::cout << "[inconnu: " << bonus.id << ", niveau "
                      << bonus.level << "]\n";
            continue;
        }
        std::cout << bonus.definition->stat
                  << " flat=" << bonus.definition->flat
                  << " percent=" << bonus.definition->percent
                  << " raw=" << bonus.raw_value << '\n';
    }
}

void print_usage(std::ostream& output) {
    output << "Usage: swcli <export.json> --show N [--storage <storage.json>]"
              " [--runed] [--name <texte>]\n"
              "       swcli <export.json> --optimize <config.json> [--top N]"
              " [--sort <metrique>] [--show <rang>] [--format text|json]"
              " [--ascii]\n";
}

bool parse_options(int argc, char** argv, Options& options, std::string& error) {
    if (argc < 2) {
        error = "export JSON manquant";
        return false;
    }
    options.export_path = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--storage" || argument == "--show" ||
            argument == "--optimize" || argument == "--top" ||
            argument == "--sort" ||
            argument == "--name" || argument == "--format" ||
            argument == "--show") {
            if (i + 1 >= argc) {
                error = "valeur manquante pour " + argument;
                return false;
            }
            const std::string value = argv[++i];
            if (argument == "--storage") {
                options.storage_path = value;
            } else if (argument == "--optimize") {
                options.solver_config_path = value;
            } else if (argument == "--top") {
                options.solver_top = std::stoi(value);
            } else if (argument == "--sort") {
                options.solver_sort = value;
            } else if (argument == "--name") {
                options.name_filter = value;
            } else if (argument == "--format") {
                if (value != "text" && value != "json") {
                    error = "--format doit valoir text ou json";
                    return false;
                }
                options.format = value;
            } else {
                try {
                    options.show_count = std::stoi(value);
                } catch (const std::exception&) {
                    error = "--show doit etre un entier positif";
                    return false;
                }
            }
        } else if (argument == "--runed") {
            options.runed_only = true;
        } else if (argument == "--ascii") {
            options.ascii = true;
        } else {
            error = "option inconnue: " + argument;
            return false;
        }
    }
    if (options.show_count <= 0 && options.solver_config_path.empty()) {
        error = "--show doit etre un entier positif";
        return false;
    }
    return true;
}

int run(const Options& options) {
    std::vector<std::string> warnings;
    const auto warning = [&](const std::string& message) {
        if (message.rfind("[solver-progress]", 0) == 0) {
            std::cout << message << '\n' << std::flush;
            return;
        }
        if (message.rfind("[solver-scenario]", 0) == 0) {
            std::cout << message << '\n' << std::flush;
            return;
        }
        warnings.push_back(message);
    };
    if (!options.solver_config_path.empty()) {
        auto solver_config = swcore::SolverConfig::load(
            options.solver_config_path, "data/mapping", warning);
        const auto mapping = swcore::RuneMapping::load("data/mapping", warning);
        const auto reference =
            swcore::ReferenceDB::load("data/swarfarm/monsters.json", warning);
        auto monsters = swcore::load_account(options.export_path, mapping, warning);
        const auto effects = swcore::SetEffects::load(
            "data/mapping/set_effects.json", warning);
        auto runes = swcore::load_runes(options.export_path, mapping, warning);
        for (auto& rune : runes) {
            if (rune.occupancy == swcore::RuneOccupancy::Inventory) {
                rune.source_name = "Inventaire";
                continue;
            }
            const auto owner = std::find_if(
                monsters.begin(), monsters.end(),
                [&](const swcore::AccountMonster& monster) {
                    return monster.unit_id == rune.source_unit_id;
                });
            if (owner != monsters.end()) {
                const auto* name = reference.find(owner->unit_master_id);
                rune.source_name =
                    (name ? name->name : std::string{"Monstre"}) + " (" +
                    std::to_string(owner->unit_id) + ")";
            }
        }
        for (auto& monster : monsters)
            monster.stats = swcore::calculate_monster_stats(monster, effects);
        if (monsters.empty()) {
            std::cerr << "Aucun monstre cible dans l'export.\n";
            return 1;
        }
        const swcore::AccountMonster* target = &monsters.front();
        if (solver_config.pool.target_unit_id) {
            const auto it = std::find_if(
                monsters.begin(), monsters.end(),
                [&](const swcore::AccountMonster& monster) {
                    return monster.unit_id == *solver_config.pool.target_unit_id;
                });
            if (it == monsters.end()) {
                std::cerr << "Monstre cible introuvable: "
                          << *solver_config.pool.target_unit_id << '\n';
                return 1;
            }
            target = &*it;
        }
        if (options.solver_top > 0) solver_config.limits.top_n = options.solver_top;
        if (!options.solver_sort.empty()) {
            solver_config.objective.sort_metrics = {options.solver_sort};
            solver_config.objective.mode = "explore";
        }
        const auto solver = swcore::solve_runes(
            *target, runes, effects, solver_config, "data/mapping",
            warning);
        json json_reports = json::array();
        for (const auto& scenario : solver.scenarios) {
            if (options.format == "text") {
                std::cout << "Scenario " << scenario.name
                          << " | candidats " << scenario.candidates_before
                          << " -> " << scenario.candidates_after
                          << " | espace brut " << scenario.raw_combinations
                          << " | noeuds " << scenario.nodes
                          << " | temps " << scenario.elapsed_ms << " ms";
                if (scenario.incomplete) std::cout << " | recherche incomplète";
                std::cout << '\n';
                for (std::size_t i = 0; i < scenario.slot_filters.size(); ++i) {
                    const auto& filter = scenario.slot_filters[i];
                    std::cout << "  slot " << (i + 1)
                              << ": pool=" << filter.pool
                              << ", niveau=" << filter.after_level
                              << ", etoiles=" << filter.after_stars
                              << ", principales=" << filter.after_main_stat
                              << ", sets=" << filter.after_set
                              << ", elagage=" << filter.after_pruning;
                    std::cout << " | origine: inventaire=" << filter.inventory
                              << ", cible=" << filter.target_equipped
                              << ", autres=" << filter.other_equipped;
                    if (filter.after_pruning == 0) {
                        std::cout << " | slot " << (i + 1)
                                  << " : 0 candidat";
                        if (filter.after_main_stat == 0)
                            std::cout << " (filtre principales)";
                        else if (filter.after_stars == 0)
                            std::cout << " (filtre etoiles)";
                        else if (filter.after_level == 0)
                            std::cout << " (filtre niveau)";
                        else
                            std::cout << " (elagage)";
                    }
                    std::cout << '\n';
                }
                if (scenario.builds.empty()) {
                    std::cout << "  Aucun build valide pour ce scenario.\n";
                    if (!scenario.validation_error.empty())
                        std::cout << "  Erreur interne: "
                                  << scenario.validation_error << '\n';
                    for (const auto& [name, constraint] :
                         solver_config.constraints) {
                        const auto best = scenario.best_constraint_values.find(name);
                        const double reachable =
                            best == scenario.best_constraint_values.end()
                                ? 0
                                : best->second;
                        if (constraint.minimum) {
                            std::cout << "  " << name << " : max atteignable "
                                      << reachable << " pour "
                                      << *constraint.minimum << " demandes\n";
                        }
                    }
                }
            }
            for (std::size_t i = 0; i < scenario.builds.size(); ++i) {
                const auto report = swcore::make_build_report(
                    *target, scenario.builds[i], static_cast<int>(i + 1));
                if (options.format == "json")
                    json_reports.push_back(build_report_json(report));
                else if (static_cast<int>(i + 1) ==
                         (options.show_count > 0 ? options.show_count : 1))
                    print_build_report(report, mapping);
            }
        }
        if (options.format == "text" && solver.scenarios.empty())
            std::cout << "Aucun scenario de solveur charge.\n";
        if (options.format == "json") {
            std::cout << json_reports.dump(2) << '\n';
        }
        for (const auto& message : warnings)
            std::cerr << "Avertissement: " << message << '\n';
        return 0;
    }
    const auto reference = swcore::ReferenceDB::load(
        "data/swarfarm/monsters.json", warning);
    const auto mapping = swcore::RuneMapping::load(
        "data/mapping", warning);
    auto monsters = swcore::load_account(options.export_path, mapping, warning);
    const auto set_effects =
        swcore::SetEffects::load("data/mapping/set_effects.json", warning);
    for (auto& monster : monsters)
        monster.stats = swcore::calculate_monster_stats(monster, set_effects);
    const auto all_runes = swcore::load_runes(options.export_path, mapping, warning);
    const auto craft_items = swcore::load_craft_items(options.export_path, warning);
    const auto account_bonuses = swcore::AccountBonuses::load(
        options.export_path, "data/mapping/account_bonuses.json", warning);
    const auto artifact_effect_labels =
        load_id_names("data/i18n/en.json", warning);
    const auto artifact_main_effects = artifact_effect_labels;
    const auto artifact_sub_effects = artifact_effect_labels;
    const auto artifact_types =
        load_id_names("data/mapping/artifact_types.json", warning);
    const auto artifact_attributes =
        load_id_names("data/mapping/monster_attributes.json", warning);
    const auto artifact_ranks =
        load_id_names("data/mapping/artifact_rank.json", warning);

    swcore::StorageStacks storage;
    if (!options.storage_path.empty())
        storage = swcore::StorageStacks::load(options.storage_path, warning);

    std::set<std::uint64_t> monster_ids;
    std::set<int> missing_reference_ids;
    const std::size_t rune_count = all_runes.size();
    std::size_t artifact_count = 0;
    std::size_t duplicate_slots = 0;
    std::size_t orphan_runes = 0;
    for (const auto& monster : monsters) {
        monster_ids.insert(monster.unit_id);
    }
    for (const auto& monster : monsters) {
        if (reference.find(monster.unit_master_id) == nullptr)
            missing_reference_ids.insert(monster.unit_master_id);
        artifact_count += monster.artifacts.size();
        std::map<int, int> slots;
        for (const auto& rune : monster.runes) {
            ++slots[rune.slot];
        }
        for (const auto& entry : slots)
            if (entry.second > 1) ++duplicate_slots;
    }
    for (const auto& rune : all_runes) {
        if (rune.occupancy == swcore::RuneOccupancy::Equipped &&
            !monster_ids.contains(rune.occupied_id))
            ++orphan_runes;
    }

    std::size_t shown = 0;
    for (const auto& monster : monsters) {
        const auto* reference_monster = reference.find(monster.unit_master_id);
        const std::string display_name =
            reference_monster ? reference_monster->name : unknown(monster.unit_master_id);
        if (!options.name_filter.empty() &&
            (reference_monster == nullptr ||
             display_name.find(options.name_filter) == std::string::npos))
            continue;
        const bool has_equipped_rune = std::any_of(
            monster.runes.begin(), monster.runes.end(),
            [](const swcore::Rune& rune) {
                return rune.occupancy == swcore::RuneOccupancy::Equipped;
            });
        if (options.runed_only && !has_equipped_rune) continue;
        if (shown == static_cast<std::size_t>(options.show_count)) break;
        ++shown;
        std::cout << monster.unit_id << ": " << display_name
                  << " element=" << (reference_monster
                                          ? element_name(reference_monster->element)
                                          : unknown(monster.unit_master_id))
                  << " stars=" << monster.stars
                  << " level=" << monster.level
                  << " unit_id=" << monster.unit_id
                  << " unit_master_id=" << monster.unit_master_id << '\n';
        if (reference_monster) {
            std::cout << "  SWARFARM 6* niv.40 (max_lvl_*): hp="
                      << reference_monster->max_lvl_hp
                      << " atk=" << reference_monster->max_lvl_attack
                      << " def=" << reference_monster->max_lvl_defense
                      << " spd=" << reference_monster->speed
                      << " crit_rate=" << reference_monster->crit_rate
                      << " crit_damage=" << reference_monster->crit_damage
                      << " resistance=" << reference_monster->resistance
                      << " accuracy=" << reference_monster->accuracy << '\n';
        } else {
            std::cout << "  SWARFARM 6* niv.40 (max_lvl_*): "
                      << unknown(monster.unit_master_id) << '\n';
        }
        const auto& stats = monster.stats;
        std::cout << "  stats:\n"
                  << "    hp=" << stats.white.hp << " +"
                  << stats.total.hp - stats.white.hp << " = "
                  << stats.total.hp << '\n'
                  << "    atk=" << stats.white.attack << " +"
                  << stats.total.attack - stats.white.attack << " = "
                  << stats.total.attack << '\n'
                  << "    def=" << stats.white.defense << " +"
                  << stats.total.defense - stats.white.defense << " = "
                  << stats.total.defense << '\n'
                  << "    spd=" << stats.white.speed << " +"
                  << stats.total.speed - stats.white.speed << " = "
                  << stats.total.speed << '\n'
                  << "    crit_rate=" << stats.white.crit_rate << " +"
                  << stats.total.crit_rate - stats.white.crit_rate << " = "
                  << stats.total.crit_rate << '\n'
                  << "    crit_damage=" << stats.white.crit_damage << " +"
                  << stats.total.crit_damage - stats.white.crit_damage << " = "
                  << stats.total.crit_damage << '\n'
                  << "    resistance=" << stats.white.resistance << " +"
                  << stats.total.resistance - stats.white.resistance << " = "
                  << stats.total.resistance << '\n'
                  << "    accuracy=" << stats.white.accuracy << " +"
                  << stats.total.accuracy - stats.white.accuracy << " = "
                  << stats.total.accuracy << '\n';
        for (const auto& rune : monster.runes) print_rune(mapping, rune);
        for (const auto& artifact : monster.artifacts)
            print_artifact(artifact, artifact_main_effects, artifact_sub_effects,
                           artifact_types, artifact_attributes, artifact_ranks);
    }

    print_account_bonuses(account_bonuses);
    std::cout << "\nResume de controle:\n"
              << "  monstres: " << monsters.size() << '\n'
              << "  runes: " << rune_count << '\n'
              << "  artefacts: " << artifact_count << '\n'
              << "  gemmes/meules: " << craft_items.size() << '\n'
              << "  monstres sans ReferenceDB: " << missing_reference_ids.size();
    for (const int id : missing_reference_ids) std::cout << " " << id;
    std::cout << "\n"
              << "  runes equipees orphelines: " << orphan_runes << '\n'
              << "  monstres avec slots dupliques: " << duplicate_slots << '\n';
    if (!options.storage_path.empty()) {
        std::cout << "  storage entries: " << storage.stacks().size()
                  << ", quantity total: " << storage.totalQuantity() << '\n';
    }
    if (!warnings.empty()) {
        std::cerr << "Avertissements (" << warnings.size() << "):\n";
        for (const auto& message : warnings) std::cerr << "  " << message << '\n';
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::cerr << "Erreur: " << error << '\n';
        print_usage(std::cerr);
        return 2;
    }
    try {
        return run(options);
    } catch (const std::exception& error) {
        std::cerr << "Erreur: " << error.what() << '\n';
        return 1;
    }
}
