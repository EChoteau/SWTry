#include "swcore/account/account.hpp"
#include "swcore/account/account_bonuses.hpp"
#include "swcore/data_files.hpp"
#include "swcore/solver/build_text.hpp"
#include "swcore/runes/craft_audit.hpp"
#include "swcore/account/equip.hpp"
#include "swcore/reference_db.hpp"
#include "swcore/runes/runes.hpp"
#include "swcore/account/storage_stacks.hpp"
#include "swcore/runes/set_effects.hpp"
#include "swcore/solver/solver_config.hpp"
#include "swcore/solver/solver.hpp"
#include "swcore/solver/solve_async.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
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
namespace files = swcore::data_files;

// Chemins des données, relatifs au dossier courant (racine du projet).
const fs::path kGameDir = files::game_dir;
const fs::path kSwarfarmMonsters = fs::path(files::swarfarm_dir) / "monsters.json";
using namespace swcore::text;  // rendu texte partagé avec swgui

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
    bool verbose = false;
    bool check_white = false;
    bool audit_craft = false;
    fs::path csv_export_path;
    fs::path locks_path;  // --locks : table des verrous (format data/user/locks.json)
    fs::path equip_actions_path;  // --equip-actions : équipement virtuel (GUI)
    std::string marginal_kind;  // "gems" ou "grinds" (--marginal)
    int marginal_max = 5;
    int width = 0;  // 0 = largeur de la console
};


using IdNames = std::map<int, std::string>;


void print_effect(const swcore::RuneMapping& mapping,
                  const swcore::StatValue& effect);


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

// ---------------------------------------------------------------------------
// Texte : largeur d'affichage UTF-8, alignement, nombres, mode ASCII.
// ---------------------------------------------------------------------------


// Translittération pour --ascii : accents retirés, symboles remplacés.
std::string to_ascii(const std::string& text) {
    static const std::map<std::string, std::string> table = {
        {"é", "e"}, {"è", "e"}, {"ê", "e"}, {"ë", "e"}, {"à", "a"},
        {"â", "a"}, {"ä", "a"}, {"î", "i"}, {"ï", "i"}, {"ô", "o"},
        {"ö", "o"}, {"ù", "u"}, {"û", "u"}, {"ü", "u"}, {"ç", "c"},
        {"É", "E"}, {"È", "E"}, {"Ê", "E"}, {"À", "A"}, {"Â", "A"},
        {"Ç", "C"}, {"Ô", "O"}, {"Î", "I"}, {"œ", "oe"}, {"Œ", "OE"},
        {"’", "'"}, {"‘", "'"}, {"“", "\""}, {"”", "\""}, {"«", "\""},
        {"»", "\""}, {"—", "-"}, {"–", "-"}, {"…", "..."}, {"×", "x"},
        {"≥", ">="}, {"≤", "<="}, {"→", "->"}, {"•", "*"},
        {"\xC2\xA0", " "}, {"\xE2\x80\xAF", " "}};
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto length = utf8_length(static_cast<unsigned char>(text[i]));
        if (length == 1) {
            result += text[i];
        } else {
            const auto it = table.find(text.substr(i, length));
            result += it == table.end() ? "?" : it->second;
        }
        i += length;
    }
    return result;
}

int console_width(int forced) {
    if (forced > 0) return forced;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
        return info.srWindow.Right - info.srWindow.Left + 1;
#endif
    if (const char* columns = std::getenv("COLUMNS")) {
        try {
            const int value = std::stoi(columns);
            if (value > 0) return value;
        } catch (const std::exception&) {
        }
    }
    return 120;  // largeur inconnue (sortie redirigée) : affichage en cartes
}

// ---------------------------------------------------------------------------
// Rendu d'un build.
// ---------------------------------------------------------------------------


void print_no_build(std::ostream& out, const swcore::ScenarioReport& scenario,
                    const swcore::SolverConfig& config) {
    out << "Aucun build valide pour le scénario " << scenario.name << ".\n";
    if (!scenario.validation_error.empty())
        out << "Erreur interne : " << scenario.validation_error << '\n';
    std::string chosen;
    double chosen_reach = 0;
    double chosen_need = 0;
    double chosen_ratio = std::numeric_limits<double>::infinity();
    for (const auto& [name, constraint] : config.constraints) {
        if (!constraint.minimum) continue;
        double reach = 0;
        if (const auto bound = scenario.constraint_upper_bounds.find(name);
            bound != scenario.constraint_upper_bounds.end())
            reach = bound->second;
        else if (const auto best = scenario.best_constraint_values.find(name);
                 best != scenario.best_constraint_values.end())
            reach = best->second;
        const double need = *constraint.minimum;
        const double ratio = need > 0 ? reach / need
                                      : std::numeric_limits<double>::infinity();
        if (chosen.empty() || ratio < chosen_ratio) {
            chosen = name;
            chosen_reach = reach;
            chosen_need = need;
            chosen_ratio = ratio;
        }
    }
    if (chosen.empty()) return;
    if (chosen_ratio < 1)
        out << "Contrainte la plus limitante : " << chosen << " (au mieux "
            << format_number(chosen_reach) << " pour " << format_number(chosen_need)
            << " demandés).\n";
    else
        out << "Aucune contrainte n'est impossible seule ; c'est leur combinaison "
               "qui bloque.\nLa plus serrée : "
            << chosen << " (au mieux " << format_number(chosen_reach) << " pour "
            << format_number(chosen_need) << " demandés).\n";
}

void print_diagnostics(std::ostream& out, const swcore::ScenarioReport& scenario) {
    out << "Diagnostic [" << scenario.name << "] : noeuds "
        << group_digits(static_cast<long long>(scenario.nodes)) << ", espace brut "
        << group_digits(static_cast<long long>(scenario.raw_combinations))
        << ", temps " << scenario.elapsed_ms << " ms\n"
        << "  Coupes : sets "
        << group_digits(static_cast<long long>(scenario.cuts_sets))
        << ", objectif "
        << group_digits(static_cast<long long>(scenario.cuts_objective))
        << ", contraintes "
        << group_digits(static_cast<long long>(scenario.cuts_constraints))
        << ", budget meules "
        << group_digits(static_cast<long long>(scenario.cuts_craft_budget)) << '\n'
        << "  Runes exclues par verrou : "
        << group_digits(static_cast<long long>(scenario.lock_excluded));
    if (!scenario.skipped_reason.empty())
        out << " ; scénario écarté : " << scenario.skipped_reason;
    out << '\n';
    const std::array<std::pair<const char*, std::size_t>, 10> columns{{
        {"Slot", 6}, {"Pool", 7}, {"Niv.", 7}, {"Étoiles", 9}, {"Stats princ.", 14},
        {"Sets", 7}, {"Sans dominées", 15}, {"Inventaire", 12},
        {"Sur la cible", 14}, {"Sur d'autres", 14}}};
    std::size_t total = 0;
    for (const auto& [label, width] : columns) {
        out << pad_left(label, width);
        total += width;
    }
    out << '\n' << std::string(total, '-') << '\n';
    for (std::size_t i = 0; i < scenario.slot_filters.size(); ++i) {
        const auto& f = scenario.slot_filters[i];
        const std::array<std::size_t, 10> values{
            i + 1, f.pool, f.after_level, f.after_stars, f.after_main_stat,
            f.after_set, f.after_pruning, f.inventory, f.target_equipped,
            f.other_equipped};
        for (std::size_t c = 0; c < columns.size(); ++c)
            out << pad_left(std::to_string(values[c]), columns[c].second);
        out << '\n';
    }
    out << "  Pool : runes de ce slot (inventaire + équipées).\n"
           "  Niv. : après monstres verrouillés et niveau minimum. "
           "Étoiles : étoiles minimum.\n"
           "  Stats princ. : stat principale autorisée. Sets : set du scénario.\n"
           "  Sans dominées : après retrait des runes dominées (même set, même "
           "principale, stats toutes inférieures ou égales).\n"
           "  Inventaire / Sur la cible / Sur d'autres : origine des runes du "
           "pool.\n";
}

// --marginal gems|grinds [K] : meilleur score pour un plafond de 0 à K, et gain
// par rapport au plafond précédent. Seul ce tableau est affiché.
int print_marginal(const Options& options, swcore::SolverConfig config,
                   const swcore::AccountMonster& target,
                   const std::vector<swcore::Rune>& runes,
                   const swcore::SetEffects& effects) {
    const bool gems = options.marginal_kind == "gems";
    if (gems ? config.gems.empty() : config.grindstones.empty()) {
        std::cerr << "Erreur: la config n'a pas de section \""
                  << (gems ? "gems" : "grindstones") << "\" active.\n";
        return 2;
    }
    config.limits.top_n = 1;
    config.limits.max_builds_per_scenario = 1;

    // Autre ressource : désactivée, illimitée ou plafonnée (inchangée ici).
    const bool other_active = gems ? !config.grindstones.empty() : !config.gems.empty();
    const auto& other_cap = gems ? config.max_new_grinds : config.max_new_gems;
    std::cout << "Autre ressource (" << (gems ? "meules" : "gemmes") << ") : "
              << (!other_active ? std::string{"désactivée"}
                  : other_cap   ? "plafond " + std::to_string(*other_cap)
                                : std::string{"illimitée"})
              << "\n\n";
    std::cout << pad_right(gems ? "Gemmes max" : "Meules max", 22)
              << pad_left("Meilleur score", 15) << pad_left("Gain", 10)
              << pad_left("Temps", 11) << '\n'
              << std::string(58, '-') << '\n';

    std::optional<double> previous;
    const auto row = [&](const std::string& label, const swcore::SolverConfig& run) {
        const auto started = std::chrono::steady_clock::now();
        swcore::AsyncSolver async;
        async.start(target, runes, effects, run, kGameDir);
        const auto solver = async.wait();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        std::optional<double> best;
        bool incomplete = false;
        for (const auto& scenario : solver.scenarios) {
            incomplete |= scenario.incomplete;
            if (!scenario.builds.empty() &&
                (!best || scenario.builds.front().score > *best))
                best = scenario.builds.front().score;
        }
        std::string gain = "-";
        if (best && previous) {
            const double delta = *best - *previous;
            gain = delta == std::floor(delta)
                       ? signed_group(static_cast<long long>(delta))
                       : signed_fixed2(delta);
        }
        std::cout << pad_right(label, 22)
                  << pad_left(best ? format_score(*best) : "aucun", 15)
                  << pad_left(gain, 10)
                  << pad_left(std::to_string(elapsed) + " ms", 11)
                  << (incomplete ? "  (recherche incomplète)" : "") << '\n';
        if (best) previous = best;
    };

    // Ligne de base : ni meules ni gemmes.
    auto base = config;
    base.grindstones.clear();
    base.gems.clear();
    row("sans meules ni gemmes", base);
    for (int cap = 0; cap <= options.marginal_max; ++cap) {
        (gems ? config.max_new_gems : config.max_new_grinds) = cap;
        row(std::to_string(cap), config);
    }
    return 0;
}

// Audit des meules et gemmes réelles de l'export (--audit-craft).
void print_craft_audit(std::ostream& out, const swcore::CraftAudit& audit,
                       const swcore::RuneMapping& mapping) {
    const auto lines = [&](const std::vector<swcore::CraftLineIssue>& issues,
                           const char* what) {
        for (const auto& issue : issues)
            out << "    rune " << issue.rune_id << " : "
                << stat_name(mapping, issue.stat_id) << ' ' << what << ' '
                << issue.value << '\n';
    };
    const auto level = [](const std::optional<int>& value) {
        return value ? "+" + std::to_string(*value) : std::string{"-"};
    };
    out << "Audit meules et gemmes (" << group_digits(static_cast<long long>(audit.runes))
        << " runes)\n";

    out << "  Meules réelles : " << audit.ground_lines << " lignes ; stat non meulable : "
        << audit.ground_bad_stat.size() << " ; hors plage de grindstone.json : "
        << audit.ground_out_of_range.size() << '\n';
    lines(audit.ground_bad_stat, "meule");
    lines(audit.ground_out_of_range, "meule");

    out << "  Lignes gemmées : " << audit.gem_lines
        << " ; hors plage de enchanted_gem.json : " << audit.gem_out_of_range.size()
        << '\n';
    lines(audit.gem_out_of_range, "valeur");

    std::size_t on_main = 0;
    for (const auto& issue : audit.gem_conflicts)
        if (issue.conflict == swcore::GemConflict::MainStat) ++on_main;
    out << "  Stat gemmée déjà présente sur la rune : " << audit.gem_conflicts.size()
        << " (principale : " << on_main << ", autre substat : "
        << audit.gem_conflicts.size() - on_main << ")\n";
    for (const auto& issue : audit.gem_conflicts)
        out << "    rune " << issue.rune_id << " : " << stat_name(mapping, issue.stat_id)
            << (issue.conflict == swcore::GemConflict::MainStat
                    ? " = stat principale\n"
                    : " = autre substat\n");

    out << "  Runes avec plusieurs gemmes : " << audit.runes_with_several_gems.size()
        << '\n';
    for (const auto id : audit.runes_with_several_gems)
        out << "    rune " << id << '\n';

    out << "  Gemme sur la stat de l'innée : " << audit.gem_same_as_innate << " sur "
        << audit.gem_runes_with_innate << " gemmes posées sur une rune à innée -> "
        << (audit.gem_same_as_innate > 0
                ? "permis"
                : "jamais observé, traité comme interdit par prudence")
        << '\n';
    out << "  Gemmes portant une meule : " << audit.gem_with_grind << " sur "
        << audit.gem_lines << " -> "
        << (audit.gem_with_grind > 0 ? "meule sur gemme possible"
                                     : "jamais observé")
        << '\n';
    out << "  Niveau minimum observé : meule " << level(audit.min_level_ground)
        << ", gemme " << level(audit.min_level_gem) << '\n';
}


// ---------------------------------------------------------------------------
// Avertissements : une seule fois chacun, regroupés par type, à la fin.
// ---------------------------------------------------------------------------

std::string warning_kind(const std::string& message) {
    std::string kind;
    bool in_digits = false;
    for (const char c : message) {
        if (c >= '0' && c <= '9') {
            if (!in_digits) kind += '#';
            in_digits = true;
        } else {
            kind += c;
            in_digits = false;
        }
    }
    return kind;
}

void print_warnings(std::ostream& err, const std::vector<std::string>& warnings,
                    const swcore::ReferenceDB* reference) {
    std::vector<std::string> lines;
    if (reference && !reference->monsters_without_stats().empty()) {
        const auto& ids = reference->monsters_without_stats();
        std::string line = std::to_string(ids.size()) +
                           " monstres SWARFARM sans stats (";
        for (std::size_t i = 0; i < ids.size(); ++i)
            line += (i ? ", " : "") + std::to_string(ids[i]);
        lines.push_back(line + ")");
    }
    std::vector<std::string> order;
    std::map<std::string, std::pair<std::size_t, std::string>> groups;
    for (const auto& message : warnings) {
        const auto kind = warning_kind(message);
        auto [it, inserted] = groups.try_emplace(kind, 0, message);
        if (inserted) order.push_back(kind);
        ++it->second.first;
    }
    for (const auto& kind : order) {
        const auto& [count, example] = groups[kind];
        lines.push_back(count == 1 ? example
                                   : std::to_string(count) + " x " + kind);
    }
    if (lines.empty()) return;
    err << "\nAvertissements (" << lines.size() << ") :\n";
    for (const auto& line : lines) err << "  - " << line << '\n';
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
              " [--runed] [--name <texte>] [--ascii]\n"
              "       swcli <export.json> --check-white [--ascii]\n"
              "       swcli <export.json> --audit-craft [--ascii]\n"
              "       swcli <export.json> --optimize <config.json> [--top N]"
              " [--sort <metrique>] [--show N] [--format text|json]"
              " [--export <builds.csv>] [--locks <locks.json>]"
              " [--equip-actions <equip_actions.json>] [--verbose]"
              " [--width N] [--ascii]\n"
              "       swcli <export.json> --optimize <config.json>"
              " --marginal gems|grinds [K]\n"
              "       (--show N : nombre de builds affichés, du meilleur au moins"
              " bon ; 10 par défaut)\n";
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
            argument == "--width" || argument == "--export" ||
            argument == "--locks" || argument == "--equip-actions") {
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
            } else if (argument == "--equip-actions") {
                options.equip_actions_path = value;
            } else if (argument == "--locks") {
                options.locks_path = value;
            } else if (argument == "--export") {
                options.csv_export_path = value;
            } else if (argument == "--width") {
                try {
                    options.width = std::stoi(value);
                } catch (const std::exception&) {
                    error = "--width doit etre un entier positif";
                    return false;
                }
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
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--check-white") {
            options.check_white = true;
        } else if (argument == "--audit-craft") {
            options.audit_craft = true;
        } else if (argument == "--marginal") {
            if (i + 1 >= argc || (std::string{argv[i + 1]} != "gems" &&
                                  std::string{argv[i + 1]} != "grinds")) {
                error = "--marginal attend gems ou grinds";
                return false;
            }
            options.marginal_kind = argv[++i];
            if (i + 1 < argc) {
                const std::string next = argv[i + 1];
                if (!next.empty() &&
                    std::all_of(next.begin(), next.end(),
                                [](unsigned char c) { return std::isdigit(c); })) {
                    options.marginal_max = std::stoi(next);
                    ++i;
                }
            }
        } else {
            error = "option inconnue: " + argument;
            return false;
        }
    }
    if (options.show_count <= 0 && options.solver_config_path.empty() &&
        !options.check_white && !options.audit_craft) {
        error = "--show doit etre un entier positif";
        return false;
    }
    return true;
}

int run(const Options& options) {
    std::vector<std::string> warnings;
    const auto warning = [&](const std::string& message) {
        if (message.rfind("[solver-progress]", 0) == 0 ||
            message.rfind("[solver-scenario]", 0) == 0) {
            if (options.verbose) std::cout << message << '\n' << std::flush;
            return;
        }
        warnings.push_back(message);
    };
    if (!options.solver_config_path.empty()) {
        auto solver_config = swcore::SolverConfig::load(
            options.solver_config_path, kGameDir, warning);
        if (!solver_config.errors.empty()) {
            for (const auto& error : solver_config.errors)
                std::cerr << "Erreur de configuration : " << error << '\n';
            return 2;
        }
        // Table des verrous (même format que data/user/locks.json du GUI).
        if (!options.locks_path.empty())
            solver_config.lock_table =
                swcore::LockTable::load(options.locks_path, warning);
        const auto mapping = swcore::RuneMapping::load(kGameDir, warning);
        const auto reference =
            swcore::ReferenceDB::load(kSwarfarmMonsters, warning);
        auto monsters = swcore::load_account(options.export_path, mapping, warning);
        const auto effects = swcore::SetEffects::load(
            kGameDir / files::set_effects, warning);
        auto runes = swcore::load_runes(options.export_path, mapping, warning);
        {
            // État du compte : export, puis équipement virtuel (--equip-actions,
            // même format et même fonction que le GUI).
            swcore::AccountState state{std::move(monsters), std::move(runes), {}};
            swcore::name_rune_sources(state, reference);
            for (auto& monster : state.monsters)
                monster.stats = swcore::calculate_monster_stats(monster, effects);
            if (!options.equip_actions_path.empty()) {
                auto log = swcore::EquipLog::load(options.equip_actions_path, warning);
                const auto wizard = swcore::read_wizard_id(options.export_path);
                if (log.wizard_id != 0 && wizard != 0 && log.wizard_id != wizard)
                    warning("Equip: --equip-actions belongs to another account; ignored");
                else
                    state = swcore::effective_state(state, log.actions, effects, reference);
            }
            monsters = std::move(state.monsters);
            runes = std::move(state.runes);
        }
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
        if (!options.marginal_kind.empty())
            return print_marginal(options, solver_config, *target, runes, effects);
        const std::size_t show_count =
            options.show_count > 0 ? static_cast<std::size_t>(options.show_count)
                                   : 10;
        solver_config.limits.detailed_builds =
            std::max(solver_config.limits.detailed_builds, show_count);
        // Même API que le GUI : recherche asynchrone, attendue ici.
        swcore::AsyncSolver async;
        async.start(*target, runes, effects, solver_config, kGameDir, warning);
        const auto solver = async.wait();
        if (!options.csv_export_path.empty() &&
            !export_builds_csv(options.csv_export_path, solver)) {
            std::cerr << "Erreur: impossible d'écrire "
                      << options.csv_export_path.string() << '\n';
            return 1;
        }
        json json_reports = json::array();
        const int width = console_width(options.width);
        const std::string rule(76, '=');
        bool first = true;
        for (const auto& scenario : solver.scenarios) {
            if (options.format == "json") {
                for (std::size_t i = 0; i < scenario.builds.size(); ++i)
                    json_reports.push_back(build_report_json(
                        swcore::make_build_report(*target, scenario.builds[i],
                                                  static_cast<int>(i + 1))));
                continue;
            }
            std::cout << (first ? "" : "\n") << rule << '\n';
            first = false;
            if (options.verbose) print_diagnostics(std::cout, scenario);
            if (scenario.incomplete)
                std::cout << "Attention : recherche incomplète (budget de temps "
                             "atteint). Ces builds ne sont pas garantis optimaux.\n";
            if (scenario.truncated)
                std::cout << "Résultats tronqués à "
                          << group_digits(static_cast<long long>(scenario.valid_builds))
                          << " builds (plafond).\n";
            if (scenario.builds.empty()) {
                print_no_build(std::cout, scenario, solver_config);
                continue;
            }
            if (scenario.all_builds_mode)
                std::cout << "Scénario " << scenario.name << " : "
                          << group_digits(static_cast<long long>(scenario.valid_builds))
                          << " builds valides trouvés.\n";
            else {
                const auto kept = swcore::kept_builds_limit(solver_config.limits);
                std::cout << "Scénario " << scenario.name << " : "
                          << scenario.builds.size()
                          << (scenario.builds.size() > 1 ? " meilleurs builds conservés"
                                                         : " meilleur build conservé")
                          << " (top_n = " << solver_config.limits.top_n;
                if (kept < static_cast<std::size_t>(solver_config.limits.top_n))
                    std::cout << ", plafonné à " << kept
                              << " par max_builds_per_scenario";
                std::cout << " ; top_n = 0 pour compter tous les builds valides).\n";
            }
            const std::size_t shown = std::min(show_count, scenario.builds.size());
            if (shown == 1)
                std::cout << "Affichage du 1er (--show).\n";
            else
                std::cout << "Affichage des " << shown << " premiers (--show).\n";
            for (std::size_t i = 0; i < shown; ++i) {
                const auto report = swcore::make_build_report(
                    *target, scenario.builds[i], static_cast<int>(i + 1));
                std::cout << '\n' << std::string(76, '-') << '\n';
                print_build(std::cout, report, *target, reference, mapping, width);
            }
        }
        if (options.format == "text" && solver.scenarios.empty())
            std::cout << "Aucun scénario de solveur chargé.\n";
        if (options.format == "json") {
            std::cout << json_reports.dump(2) << '\n';
        }
        std::cout << std::flush;
        print_warnings(std::cerr, warnings, &reference);
        return 0;
    }
    const auto reference = swcore::ReferenceDB::load(
        kSwarfarmMonsters, warning);
    const auto mapping = swcore::RuneMapping::load(
        kGameDir, warning);
    auto monsters = swcore::load_account(options.export_path, mapping, warning);
    const auto set_effects =
        swcore::SetEffects::load(kGameDir / files::set_effects, warning);
    for (auto& monster : monsters)
        monster.stats = swcore::calculate_monster_stats(monster, set_effects);
    if (options.check_white) {
        // Stats blanches 6* niveau 40 de l'export comparées à SWARFARM (max_lvl_*).
        std::size_t checked = 0;
        std::vector<std::string> mismatches;
        std::vector<std::string> without_stats;
        std::set<int> without_reference;
        for (const auto& monster : monsters) {
            if (monster.stars != 6 || monster.level != 40) continue;
            const auto* ref = reference.find(monster.unit_master_id);
            if (!ref) {
                without_reference.insert(monster.unit_master_id);
                continue;
            }
            const std::string label = ref->name + " (" +
                                      std::to_string(monster.unit_id) + ")";
            if (!ref->has_stats()) {
                without_stats.push_back(label + ", com2us_id " +
                                        std::to_string(monster.unit_master_id));
                continue;
            }
            ++checked;
            std::string differences;
            const auto compare = [&](const char* name, int mine, int expected) {
                if (mine != expected)
                    differences += std::string(differences.empty() ? "" : ", ") +
                                   name + " " + std::to_string(mine) + " au lieu de " +
                                   std::to_string(expected);
            };
            compare("PV", monster.con * 15, *ref->max_lvl_hp);
            compare("ATQ", monster.attack, *ref->max_lvl_attack);
            compare("DEF", monster.defense, *ref->max_lvl_defense);
            compare("VIT", monster.speed, ref->speed);
            compare("Tx crit.", monster.crit_rate, ref->crit_rate);
            compare("Dgts crit.", monster.crit_damage, ref->crit_damage);
            compare("RES", monster.resistance, ref->resistance);
            compare("Préc.", monster.accuracy, ref->accuracy);
            if (!differences.empty()) mismatches.push_back(label + " : " + differences);
        }
        std::cout << "Vérification des stats blanches (6* niveau 40) : "
                  << checked << " monstres vérifiés, "
                  << checked - mismatches.size() << " conformes, "
                  << mismatches.size() << " avec écart.\n";
        for (const auto& line : mismatches) std::cout << "  " << line << '\n';
        if (!without_stats.empty()) {
            std::cout << "Monstres du compte sans stats SWARFARM (non vérifiés) : "
                      << without_stats.size() << '\n';
            for (const auto& line : without_stats) std::cout << "  " << line << '\n';
        }
        if (!without_reference.empty()) {
            std::cout << "Monstres du compte absents de SWARFARM : "
                      << without_reference.size() << " (";
            bool first_id = true;
            for (const int id : without_reference) {
                std::cout << (first_id ? "" : ", ") << id;
                first_id = false;
            }
            std::cout << ")\n";
        }
        std::cout << std::flush;
        print_warnings(std::cerr, warnings, &reference);
        return 0;
    }
    const auto all_runes = swcore::load_runes(options.export_path, mapping, warning);
    if (options.audit_craft) {
        const auto grindstones =
            swcore::load_craft_ranges(kGameDir / files::grindstone, warning);
        const auto gems =
            swcore::load_craft_ranges(kGameDir / files::enchanted_gem, warning);
        print_craft_audit(std::cout, swcore::audit_craft(all_runes, grindstones, gems),
                          mapping);
        std::cout << std::flush;
        print_warnings(std::cerr, warnings, &reference);
        return 0;
    }
    const auto craft_items = swcore::load_craft_items(options.export_path, warning);
    const auto account_bonuses = swcore::AccountBonuses::load(
        options.export_path, kGameDir / files::account_bonuses, warning);
    const auto artifact_effect_labels =
        load_id_names(files::i18n_en, warning);
    const auto artifact_main_effects = artifact_effect_labels;
    const auto artifact_sub_effects = artifact_effect_labels;
    const auto artifact_types =
        load_id_names(kGameDir / files::artifact_types, warning);
    const auto artifact_attributes =
        load_id_names(kGameDir / files::monster_attributes, warning);
    const auto artifact_ranks =
        load_id_names(kGameDir / files::artifact_rank, warning);

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
        const auto optional_text = [](const std::optional<int>& value) {
            return value ? std::to_string(*value) : std::string{"absent"};
        };
        if (reference_monster) {
            std::cout << "  SWARFARM 6* niv.40 (max_lvl_*): hp="
                      << optional_text(reference_monster->max_lvl_hp)
                      << " atk=" << optional_text(reference_monster->max_lvl_attack)
                      << " def=" << optional_text(reference_monster->max_lvl_defense)
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
    std::cout << std::flush;
    print_warnings(std::cerr, warnings, &reference);
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
    if (!options.ascii) {
        try {
            return run(options);
        } catch (const std::exception& error) {
            std::cerr << "Erreur: " << error.what() << '\n';
            return 1;
        }
    }
    // --ascii : sortie capturée puis translittérée (sans accents ni symboles).
    std::ostringstream out;
    std::ostringstream err;
    auto* const out_buffer = std::cout.rdbuf(out.rdbuf());
    auto* const err_buffer = std::cerr.rdbuf(err.rdbuf());
    int status = 1;
    try {
        status = run(options);
    } catch (const std::exception& error) {
        std::cerr << "Erreur: " << error.what() << '\n';
    }
    std::cout.rdbuf(out_buffer);
    std::cerr.rdbuf(err_buffer);
    std::cout << to_ascii(out.str()) << std::flush;
    std::cerr << to_ascii(err.str()) << std::flush;
    return status;
}
