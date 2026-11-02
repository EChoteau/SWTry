#include "swcore/account.hpp"
#include "swcore/account_bonuses.hpp"
#include "swcore/craft_audit.hpp"
#include "swcore/reference_db.hpp"
#include "swcore/runes.hpp"
#include "swcore/storage_stacks.hpp"
#include "swcore/set_effects.hpp"
#include "swcore/solver_config.hpp"
#include "swcore/solver.hpp"

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
    std::string marginal_kind;  // "gems" ou "grinds" (--marginal)
    int marginal_max = 5;
    int width = 0;  // 0 = largeur de la console
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

// ---------------------------------------------------------------------------
// Texte : largeur d'affichage UTF-8, alignement, nombres, mode ASCII.
// ---------------------------------------------------------------------------

std::size_t utf8_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead >> 5) == 0x6) return 2;
    if ((lead >> 4) == 0xE) return 3;
    if ((lead >> 3) == 0x1E) return 4;
    return 1;
}

std::size_t display_width(const std::string& text) {
    std::size_t width = 0;
    for (const unsigned char c : text)
        if ((c & 0xC0) != 0x80) ++width;
    return width;
}

std::string truncate_to(const std::string& text, std::size_t width) {
    if (display_width(text) <= width) return text;
    if (width <= 3) return std::string(width, '.');
    std::string result;
    std::size_t used = 0;
    for (std::size_t i = 0; i < text.size() && used < width - 3;) {
        const auto length = utf8_length(static_cast<unsigned char>(text[i]));
        result.append(text, i, length);
        i += length;
        ++used;
    }
    return result + "...";
}

std::string pad_right(const std::string& text, std::size_t width) {
    const auto used = display_width(text);
    return used >= width ? text : text + std::string(width - used, ' ');
}

std::string pad_left(const std::string& text, std::size_t width) {
    const auto used = display_width(text);
    return used >= width ? text : std::string(width - used, ' ') + text;
}

std::string group_digits(long long value) {
    const bool negative = value < 0;
    const std::string digits =
        std::to_string(negative ? -static_cast<unsigned long long>(value)
                                : static_cast<unsigned long long>(value));
    std::string result;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) result += ' ';
        result += digits[i];
    }
    return negative ? "-" + result : result;
}

std::string signed_group(long long value) {
    return value > 0 ? "+" + group_digits(value) : group_digits(value);
}

std::string fixed2(double value) {
    if (std::abs(value) < 0.005) value = 0;
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << value;
    return output.str();
}

std::string signed_fixed2(double value) {
    const std::string text = fixed2(value);
    return value >= 0.005 ? "+" + text : text;
}

std::string format_score(double value) {
    if (value == std::floor(value) && std::abs(value) < 1e15)
        return group_digits(static_cast<long long>(value));
    return fixed2(value);
}

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

std::string effect_text(const swcore::RuneMapping& mapping,
                        const std::optional<swcore::StatValue>& effect) {
    if (!effect || effect->stat == swcore::StatType::None) return "aucun";
    return stat_name(mapping, effect->stat_id) + " " +
           std::to_string(effect->value);
}

std::string stars_text(int stars) {
    // class 11..16 = runes antiques (16 = 6* antique)
    return stars > 10 ? std::to_string(stars - 10) + "* antique"
                      : std::to_string(stars) + "*";
}

IdNames load_id_names(const fs::path& path, const swcore::WarningSink& warning);

// Nom de rareté (1-5) depuis le mapping rune_quality.json.
const std::string& rarity_name(int rarity) {
    static const IdNames names =
        load_id_names("data/mapping/rune_quality.json", swcore::WarningSink{});
    static std::map<int, std::string> unknown_names;
    const auto it = names.find(rarity);
    if (it != names.end()) return it->second;
    auto& fallback = unknown_names[rarity];
    if (fallback.empty()) fallback = unknown(rarity);
    return fallback;
}

constexpr std::size_t kCardInner = 34;  // carte = "| " + 34 + " |" = 38 colonnes
constexpr std::size_t kCardWidth = kCardInner + 4;
constexpr std::size_t kCardGap = 2;
constexpr int kCardsMinWidth = 120;

std::vector<std::string> rune_card(const swcore::RuneView& view,
                                   const swcore::RuneMapping& mapping) {
    const auto& rune = view.rune;
    std::vector<std::string> body;
    body.push_back("+" + std::to_string(rune.upgrade_level) + " " +
                   set_name(mapping, rune.set_id) + " (slot " +
                   std::to_string(rune.slot) + ")  " + stars_text(rune.stars));
    body.push_back("Principale : " + effect_text(mapping, rune.primary));
    const bool has_innate =
        rune.innate && rune.innate->stat != swcore::StatType::None;
    body.push_back("Innate : " +
                   (has_innate ? effect_text(mapping, rune.innate) : std::string{"-"}));
    // Substats réelles ; chaque modification simulée est marquée sur la ligne
    // qu'elle touche (ligne remplacée par une gemme, meule à poser).
    for (std::size_t j = 0; j < rune.secondary.size(); ++j) {
        const auto& substat = rune.secondary[j];
        std::string line = "  " + stat_name(mapping, substat.value.stat_id) + " " +
                           std::to_string(substat.value.value + substat.grind);
        if (substat.grind != 0) line += " [+" + std::to_string(substat.grind) + "]";
        if (substat.enchanted) line += " (gemme)";
        const auto gem = std::find_if(view.gems.begin(), view.gems.end(),
                                      [&](const swcore::AppliedGem& g) {
                                          return g.line == static_cast<int>(j);
                                      });
        if (gem != view.gems.end()) line += " [remplacée]";
        body.push_back(line);
        // Gemme sur deux lignes (largeur de carte) : rareté, puis stat et valeur.
        if (gem != view.gems.end()) {
            body.push_back("  -> gemme " + rarity_name(gem->rarity) + " (simulée)");
            body.push_back("     " + stat_name(mapping, gem->stat_id) + " " +
                           std::to_string(gem->value));
        }
        for (const auto& grind : view.grinds)
            if (grind.line == static_cast<int>(j))
                body.push_back("  -> meule " + rarity_name(grind.rarity) + " +" +
                               std::to_string(grind.value) + " (simulée)");
    }
    for (std::size_t i = rune.secondary.size(); i < 4; ++i) body.emplace_back();
    body.push_back("Id : " + std::to_string(rune.rune_id));
    body.push_back("Origine : " + origin_name(rune));
    body.push_back("Efficacité : " + fixed2(view.efficiency) + " %");
    if (view.simulated_efficiency)
        body.push_back("  après simulation : " + fixed2(*view.simulated_efficiency) +
                       " %");

    std::vector<std::string> card;
    const std::string border = "+" + std::string(kCardWidth - 2, '-') + "+";
    card.push_back(border);
    for (const auto& line : body)
        card.push_back("| " + pad_right(truncate_to(line, kCardInner), kCardInner) +
                       " |");
    card.push_back(border);
    return card;
}

void print_card_row(std::ostream& out, const swcore::BuildReport& report,
                    const swcore::RuneMapping& mapping,
                    const std::array<int, 3>& slots) {
    std::array<std::vector<std::string>, 3> cards;
    std::size_t height = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        cards[i] = rune_card(report.runes[static_cast<std::size_t>(slots[i] - 1)],
                             mapping);
        height = std::max(height, cards[i].size());
    }
    for (std::size_t line = 0; line < height; ++line) {
        std::string row;
        for (std::size_t i = 0; i < 3; ++i) {
            if (i > 0) row += std::string(kCardGap, ' ');
            row += line < cards[i].size() ? cards[i][line]
                                          : std::string(kCardWidth, ' ');
        }
        out << row << '\n';
    }
}

void print_runes(std::ostream& out, const swcore::BuildReport& report,
                 const swcore::RuneMapping& mapping, int width) {
    if (width >= kCardsMinWidth) {
        print_card_row(out, report, mapping, {6, 1, 2});
        print_card_row(out, report, mapping, {5, 4, 3});
        return;
    }
    for (int slot = 1; slot <= 6; ++slot)
        for (const auto& line :
             rune_card(report.runes[static_cast<std::size_t>(slot - 1)], mapping))
            out << line << '\n';
}

std::optional<double> equipped_efficiency(const swcore::AccountMonster& target) {
    std::array<swcore::Rune, 6> runes{};
    std::array<bool, 6> present{};
    for (const auto& rune : target.runes) {
        if (rune.occupancy != swcore::RuneOccupancy::Equipped || rune.slot < 1 ||
            rune.slot > 6)
            continue;
        runes[static_cast<std::size_t>(rune.slot - 1)] = rune;
        present[static_cast<std::size_t>(rune.slot - 1)] = true;
    }
    if (std::find(present.begin(), present.end(), false) != present.end())
        return std::nullopt;
    return swcore::calculate_build_efficiency(runes);
}

void print_stats_table(std::ostream& out, const swcore::BuildReport& report,
                       const swcore::AccountMonster& target) {
    constexpr std::size_t c1 = 20, c2 = 20, c3 = 24, c4 = 12;
    out << pad_right("Stat", c1) << pad_left("Équipé aujourd'hui", c2)
        << pad_left("Nouveau total [+vert]", c3) << pad_left("Diff.", c4) << '\n'
        << std::string(c1 + c2 + c3 + c4, '-') << '\n';
    for (std::size_t i = 0; i < report.stats.size(); ++i) {
        const auto& now = report.equipped_stats[i];
        const auto& next = report.stats[i];
        const std::string green = next.green >= 0
                                      ? "+" + group_digits(next.green)
                                      : group_digits(next.green);
        out << pad_right(stat_label(i), c1) << pad_left(group_digits(now.total), c2)
            << pad_left(group_digits(next.total) + " [" + green + "]", c3)
            << pad_left(signed_group(next.total - now.total), c4) << '\n';
    }
    const auto equipped = swcore::calculate_derived_metrics(target.stats.total);
    const auto metric_row = [&](const char* label, double now, double next) {
        const auto a = static_cast<long long>(now);
        const auto b = static_cast<long long>(next);
        out << pad_right(label, c1) << pad_left(group_digits(a), c2)
            << pad_left(group_digits(b), c3) << pad_left(signed_group(b - a), c4)
            << '\n';
    };
    metric_row("DMG", equipped.expected_damage, report.metrics.expected_damage);
    metric_row("PV eff.", equipped.effective_hp, report.metrics.effective_hp);
    metric_row("PV eff. DEF brisée", equipped.effective_hp_broken_def,
               report.metrics.effective_hp_broken_def);
    const auto now_efficiency = equipped_efficiency(target);
    out << pad_right("Efficacité %", c1)
        << pad_left(now_efficiency ? fixed2(*now_efficiency) : "-", c2)
        << pad_left(fixed2(report.metrics.rune_efficiency), c3)
        << pad_left(now_efficiency
                        ? signed_fixed2(report.metrics.rune_efficiency -
                                        *now_efficiency)
                        : "-",
                    c4)
        << '\n';
}

// Blocs "Meules à utiliser" et "Gemmes à utiliser" (absents si rien).
void print_craft_blocks(std::ostream& out, const swcore::BuildReport& report,
                        const swcore::RuneMapping& mapping) {
    std::vector<std::string> grinds;
    std::vector<std::string> gems;
    for (const auto& view : report.runes) {
        const std::string where = "slot " + std::to_string(view.rune.slot) +
                                  "  rune " + std::to_string(view.rune.rune_id) +
                                  "  " + origin_name(view.rune) + "  ";
        for (const auto& gem : view.gems)
            gems.push_back(where + "ligne " + stat_name(mapping, gem.old_stat_id) + " " +
                           std::to_string(gem.old_value) + " remplacée par " +
                           stat_name(mapping, gem.stat_id) + " " +
                           std::to_string(gem.value) + " (" + rarity_name(gem.rarity) +
                           ")");
        for (const auto& grind : view.grinds)
            grinds.push_back(where + stat_name(mapping, grind.stat_id) + " : meule +" +
                             std::to_string(grind.previous) + " -> +" +
                             std::to_string(grind.value) + " (" +
                             rarity_name(grind.rarity) + ")");
    }
    if (!gems.empty()) {
        out << "\nGemmes à utiliser (" << gems.size() << ") :\n";
        for (const auto& line : gems) out << "  " << line << '\n';
    }
    if (!grinds.empty()) {
        out << "\nMeules à utiliser (" << grinds.size() << ") :\n";
        for (const auto& line : grinds) out << "  " << line << '\n';
    }
}

void print_build(std::ostream& out, const swcore::BuildReport& report,
                 const swcore::AccountMonster& target,
                 const swcore::ReferenceDB& reference,
                 const swcore::RuneMapping& mapping, int width) {
    const auto* monster = reference.find(report.monster_master_id);
    const std::string name =
        monster ? monster->name : unknown(report.monster_master_id);
    const std::string element =
        monster ? element_name(monster->element) : "[inconnu: element]";
    out << name << " (" << element << ") - niveau " << report.monster_level
        << '\n'
        << "Scénario : " << report.scenario << "   |   Rang " << report.rank
        << "   |   Score " << format_score(report.score) << '\n';
    if (report.projection_level > 0)
        out << "Stats calculées avec runes projetées à +" << report.projection_level
            << " (stat principale)\n";
    if (report.simulated_craft)
        out << "Stats calculées avec meules/gemmes simulées (voir plus bas)\n";
    out << '\n';
    print_stats_table(out, report, target);
    out << '\n';
    print_runes(out, report, mapping, width);
    print_craft_blocks(out, report, mapping);
}

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
        << group_digits(static_cast<long long>(scenario.cuts_craft_budget)) << '\n';
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
        const auto solver =
            swcore::solve_runes(target, runes, effects, run, "data/mapping", {});
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

// Export CSV (séparateur ';') de tous les builds triés, scénario par scénario.
bool export_builds_csv(const fs::path& path, const swcore::SolverReport& solver) {
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    output << "scenario;rang;score;rune_slot1;rune_slot2;rune_slot3;rune_slot4;"
              "rune_slot5;rune_slot6;PV;ATQ;DEF;VIT;Tx crit.;Dgts crit.;RES;Préc.;"
              "PV eff.;efficacite;meules;gemmes\n";
    const auto line = [&](const std::string& scenario, std::size_t rank,
                          double score,
                          const std::array<std::uint64_t, 6>& ids,
                          const swcore::StatValues& s, double ehp,
                          double efficiency, int grinds, int gems) {
        output << scenario << ';' << rank << ';' << format_number(score);
        for (const auto id : ids) output << ';' << id;
        output << ';' << s.hp << ';' << s.attack << ';' << s.defense << ';'
               << s.speed << ';' << s.crit_rate << ';' << s.crit_damage << ';'
               << s.resistance << ';' << s.accuracy << ';' << format_number(ehp)
               << ';' << fixed2(efficiency) << ';' << grinds << ';' << gems << '\n';
    };
    for (const auto& scenario : solver.scenarios) {
        if (scenario.all_builds_mode) {
            for (std::size_t i = 0; i < scenario.all_builds.size(); ++i) {
                const auto& b = scenario.all_builds[i];
                line(scenario.name, i + 1, b.score, b.rune_ids, b.total,
                     b.effective_hp, b.efficiency, b.grind_cost, b.gem_cost);
            }
            continue;
        }
        for (std::size_t i = 0; i < scenario.builds.size(); ++i) {
            const auto& b = scenario.builds[i];
            std::array<std::uint64_t, 6> ids{};
            for (std::size_t s = 0; s < 6; ++s) ids[s] = b.runes[s].rune_id;
            line(scenario.name, i + 1, b.score, ids, b.stats.total,
                 b.metrics.effective_hp, b.efficiency, b.grind_cost, b.gem_cost);
        }
    }
    return static_cast<bool>(output);
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
              " [--export <builds.csv>] [--verbose] [--width N] [--ascii]\n"
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
            argument == "--width" || argument == "--export") {
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
            options.solver_config_path, "data/mapping", warning);
        if (!solver_config.errors.empty()) {
            for (const auto& error : solver_config.errors)
                std::cerr << "Erreur de configuration : " << error << '\n';
            return 2;
        }
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
        if (!options.marginal_kind.empty())
            return print_marginal(options, solver_config, *target, runes, effects);
        const std::size_t show_count =
            options.show_count > 0 ? static_cast<std::size_t>(options.show_count)
                                   : 10;
        solver_config.limits.detailed_builds =
            std::max(solver_config.limits.detailed_builds, show_count);
        const auto solver = swcore::solve_runes(
            *target, runes, effects, solver_config, "data/mapping",
            warning);
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
        "data/swarfarm/monsters.json", warning);
    const auto mapping = swcore::RuneMapping::load(
        "data/mapping", warning);
    auto monsters = swcore::load_account(options.export_path, mapping, warning);
    const auto set_effects =
        swcore::SetEffects::load("data/mapping/set_effects.json", warning);
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
            swcore::load_craft_ranges("data/mapping/grindstone.json", warning);
        const auto gems =
            swcore::load_craft_ranges("data/mapping/enchanted_gem.json", warning);
        print_craft_audit(std::cout, swcore::audit_craft(all_runes, grindstones, gems),
                          mapping);
        std::cout << std::flush;
        print_warnings(std::cerr, warnings, &reference);
        return 0;
    }
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
