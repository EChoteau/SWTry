// Rendu texte d'un build (cartes, tableau des stats, blocs de craft) et export
// CSV, partagés par swcli et swgui. Aucune sortie console : tout passe par un
// std::ostream fourni par l'appelant.

#include "swcore/solver/build_text.hpp"
#include "swcore/data_files.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace swcore::text {

namespace fs = std::filesystem;

namespace {
std::filesystem::path g_mapping_directory = data_files::game_dir;
}  // namespace

void set_mapping_directory(const std::filesystem::path& directory) {
    g_mapping_directory = directory;
}

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

std::string format_number(double value) {
    std::ostringstream output;
    output << std::setprecision(12) << value;
    return output.str();
}

const char* stat_label(std::size_t index) {
    static constexpr const char* labels[] = {
        "PV", "ATQ", "DEF", "VIT", "Tx crit.", "Dgts crit.", "RES", "Préc."};
    return index < 8 ? labels[index] : "?";
}

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

// Nom de rareté (1-5) depuis le mapping rune_quality.json.
const std::string& rarity_name(int rarity) {
    static const std::map<int, std::string> names = [] {
        std::map<int, std::string> result;
        std::ifstream input(g_mapping_directory / data_files::rune_quality);
        if (!input) return result;
        try {
            nlohmann::json root;
            input >> root;
            for (const auto& [id, name] : root.items())
                if (name.is_string()) result.emplace(std::stoi(id), name.get<std::string>());
        } catch (const std::exception&) {
        }
        return result;
    }();
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

namespace {

void write_csv_header(std::ostream& output) {
    output << "scenario;rang;score;rune_slot1;rune_slot2;rune_slot3;rune_slot4;"
              "rune_slot5;rune_slot6;PV;ATQ;DEF;VIT;Tx crit.;Dgts crit.;RES;Préc.;"
              "PV eff.;efficacite;meules;gemmes\n";
}

// Ligne du build `index` du scénario (builds ou all_builds selon le mode).
void write_csv_line(std::ostream& output, const swcore::ScenarioReport& scenario,
                    std::size_t index, std::size_t rank) {
    const auto line = [&](double score, const std::array<std::uint64_t, 6>& ids,
                          const swcore::StatValues& s, double ehp, double efficiency,
                          int grinds, int gems) {
        output << scenario.name << ';' << rank << ';' << format_number(score);
        for (const auto id : ids) output << ';' << id;
        output << ';' << s.hp << ';' << s.attack << ';' << s.defense << ';'
               << s.speed << ';' << s.crit_rate << ';' << s.crit_damage << ';'
               << s.resistance << ';' << s.accuracy << ';' << format_number(ehp)
               << ';' << fixed2(efficiency) << ';' << grinds << ';' << gems << '\n';
    };
    if (scenario.all_builds_mode) {
        const auto& b = scenario.all_builds[index];
        line(b.score, b.rune_ids, b.total, b.effective_hp, b.efficiency, b.grind_cost,
             b.gem_cost);
        return;
    }
    const auto& b = scenario.builds[index];
    std::array<std::uint64_t, 6> ids{};
    for (std::size_t s = 0; s < 6; ++s) ids[s] = b.runes[s].rune_id;
    line(b.score, ids, b.stats.total, b.metrics.effective_hp, b.efficiency, b.grind_cost,
         b.gem_cost);
}

}  // namespace

// Export CSV (séparateur ';') de tous les builds triés, scénario par scénario.
bool export_builds_csv(const fs::path& path, const swcore::SolverReport& solver) {
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    write_csv_header(output);
    for (const auto& scenario : solver.scenarios) {
        const std::size_t count = scenario.all_builds_mode ? scenario.all_builds.size()
                                                           : scenario.builds.size();
        for (std::size_t i = 0; i < count; ++i) write_csv_line(output, scenario, i, i + 1);
    }
    return static_cast<bool>(output);
}

bool export_builds_csv(const fs::path& path, const swcore::SolverReport& solver,
                       const std::vector<std::pair<std::size_t, std::size_t>>& order) {
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    write_csv_header(output);
    std::size_t rank = 0;
    for (const auto& [scenario, index] : order)
        if (scenario < solver.scenarios.size())
            write_csv_line(output, solver.scenarios[scenario], index, ++rank);
    return static_cast<bool>(output);
}

}  // namespace swcore::text
