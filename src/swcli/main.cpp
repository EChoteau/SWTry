#include "swcore/account.hpp"
#include "swcore/account_bonuses.hpp"
#include "swcore/reference_db.hpp"
#include "swcore/runes.hpp"
#include "swcore/storage_stacks.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Options {
    fs::path export_path;
    fs::path storage_path;
    int show_count = 0;
    std::string name_filter;
    bool runed_only = false;
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

std::string format_number(double value) {
    std::ostringstream output;
    output << std::setprecision(12) << value;
    return output.str();
}

void print_effect(const swcore::RuneMapping& mapping,
                  const swcore::StatValue& effect) {
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
        std::cout << " (base=" << secondary.value.value
                  << ", meule=" << secondary.grind
                  << ", enchante=" << (secondary.enchanted ? "oui" : "non")
                  << ")\n";
    }
}

void print_artifact(const swcore::Artifact& artifact) {
    std::cout << "    Artefact rid=" << artifact.rid
              << " slot=" << artifact.slot
              << " type=" << artifact.type
              << " attribute=" << artifact.attribute
              << " unit_style=" << artifact.unit_style
              << " rank=" << artifact.rank
              << " level=" << artifact.level << '\n';
    std::cout << "      primary=[";
    for (std::size_t i = 0; i < artifact.primary.size(); ++i)
        std::cout << (i == 0 ? "" : ", ") << format_number(artifact.primary[i]);
    std::cout << "]\n";
    for (const auto& effect : artifact.secondary) {
        std::cout << "      secondary=[";
        for (std::size_t i = 0; i < effect.size(); ++i)
            std::cout << (i == 0 ? "" : ", ") << format_number(effect[i]);
        std::cout << "]\n";
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
    output << "Usage: swcli <export.json> [--storage <storage.json>]"
              " --show N [--runed] [--name <texte>]\n";
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
            argument == "--name") {
            if (i + 1 >= argc) {
                error = "valeur manquante pour " + argument;
                return false;
            }
            const std::string value = argv[++i];
            if (argument == "--storage") {
                options.storage_path = value;
            } else if (argument == "--name") {
                options.name_filter = value;
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
        } else {
            error = "option inconnue: " + argument;
            return false;
        }
    }
    if (options.show_count <= 0) {
        error = "--show doit etre un entier positif";
        return false;
    }
    return true;
}

int run(const Options& options) {
    std::vector<std::string> warnings;
    const auto warning = [&](const std::string& message) {
        warnings.push_back(message);
    };
    const auto reference = swcore::ReferenceDB::load(
        "data/swarfarm/monsters.json", warning);
    const auto mapping = swcore::RuneMapping::load(
        "data/mapping", warning);
    const auto monsters = swcore::load_account(options.export_path, warning);
    const auto all_runes = swcore::load_runes(options.export_path, mapping, warning);
    const auto craft_items = swcore::load_craft_items(options.export_path, warning);
    const auto account_bonuses = swcore::AccountBonuses::load(
        options.export_path, "data/mapping/account_bonuses.json", warning);

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
            std::cout << "  base: hp=" << reference_monster->base_hp
                      << " atk=" << reference_monster->base_attack
                      << " def=" << reference_monster->base_defense
                      << " spd=" << reference_monster->speed
                      << " crit_rate=" << reference_monster->crit_rate
                      << " crit_damage=" << reference_monster->crit_damage
                      << " resistance=" << reference_monster->resistance
                      << " accuracy=" << reference_monster->accuracy << '\n';
        } else {
            std::cout << "  base: " << unknown(monster.unit_master_id) << '\n';
        }
        for (const auto& rune : monster.runes) print_rune(mapping, rune);
        for (const auto& artifact : monster.artifacts) print_artifact(artifact);
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
