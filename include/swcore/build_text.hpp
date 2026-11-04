#pragma once

#include "swcore/account.hpp"
#include "swcore/reference_db.hpp"
#include "swcore/runes.hpp"
#include "swcore/solver.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

// Rendu texte partagé par swcli et swgui ("Copier en texte" = sortie CLI).
namespace swcore::text {

// Dossier du mapping pour les noms de rareté (défaut : data/mapping).
void set_mapping_directory(const std::filesystem::path& directory);

std::string unknown(int id);
std::string element_name(swcore::Element element);
std::string stat_name(const swcore::RuneMapping& mapping, int id);
std::string set_name(const swcore::RuneMapping& mapping, int id);
std::string origin_name(const swcore::Rune& rune);
std::string format_number(double value);
const char* stat_label(std::size_t index);  // PV, ATQ, DEF, VIT, Tx crit., ...

std::size_t utf8_length(unsigned char lead);
std::size_t display_width(const std::string& text);
std::string truncate_to(const std::string& text, std::size_t width);
std::string pad_right(const std::string& text, std::size_t width);
std::string pad_left(const std::string& text, std::size_t width);
std::string group_digits(long long value);
std::string signed_group(long long value);
std::string fixed2(double value);
std::string signed_fixed2(double value);
std::string format_score(double value);

std::string effect_text(const swcore::RuneMapping& mapping,
                        const std::optional<swcore::StatValue>& effect);
std::string stars_text(int stars);
const std::string& rarity_name(int rarity);
std::optional<double> equipped_efficiency(const swcore::AccountMonster& target);

// Carte texte d'une rune (lignes encadrées de 38 colonnes), comme swcli.
std::vector<std::string> rune_card(const swcore::RuneView& view,
                                   const swcore::RuneMapping& mapping);

// Build complet, tel que l'affiche swcli (cartes en 2 rangées si width >= 120).
void print_build(std::ostream& out, const swcore::BuildReport& report,
                 const swcore::AccountMonster& target,
                 const swcore::ReferenceDB& reference,
                 const swcore::RuneMapping& mapping, int width);
void print_stats_table(std::ostream& out, const swcore::BuildReport& report,
                       const swcore::AccountMonster& target);
void print_craft_blocks(std::ostream& out, const swcore::BuildReport& report,
                        const swcore::RuneMapping& mapping);

// Export CSV de swcli --export (séparateur ';').
bool export_builds_csv(const std::filesystem::path& path,
                       const swcore::SolverReport& solver);
// Même format, dans l'ordre donné (scénario, index dans builds ou all_builds) ;
// le rang est la position dans cet ordre (tri du tableau de résultats du GUI).
bool export_builds_csv(const std::filesystem::path& path, const swcore::SolverReport& solver,
                       const std::vector<std::pair<std::size_t, std::size_t>>& order);

}  // namespace swcore::text
