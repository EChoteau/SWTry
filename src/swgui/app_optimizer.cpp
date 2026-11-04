// Page "Optimiseur" : paramètres. Chaque contrôle modifie directement
// swcore::SolverConfig (la structure remplie par le parser de --optimize) ;
// la recherche passe par swcore::AsyncSolver, comme swcli.

#include "app.hpp"
#include "gui_common.hpp"
#include "gui_components.hpp"
#include "text_util.hpp"

#include "swcore/build_text.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include <portable-file-dialogs.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <system_error>
#include <string_view>
#include <utility>

namespace swgui {
namespace fs = std::filesystem;

namespace {

const char* const kRollModes[] = {"Off", "Min", "Max", "Valeur exacte"};
const char* const kProjection[] = {"Actuel", "+12", "+15"};

// Libellé du bonus d'un set (composant partagé avec la fiche monstre).
std::string set_bonus(const swcore::SetEffects& effects, int set_id) {
    return set_bonus_text(effects, set_id);
}

std::string safe_file_name(std::string name) {
    for (auto& c : name)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            c = '_';
    return name;
}

}  // namespace

void App::load_craft_tables() {
    grind_ranges_ = swcore::load_craft_ranges(data_.mapping_directory() / "grindstone.json");
    gem_ranges_ = swcore::load_craft_ranges(data_.mapping_directory() / "enchanted_gem.json");
    main_stat_ids_.clear();
    std::ifstream input(data_.mapping_directory() / "rune_mainstat.json");
    if (!input) return;
    try {
        nlohmann::json root;
        input >> root;
        for (const auto& [id, value] : root.items()) main_stat_ids_.push_back(std::stoi(id));
    } catch (const std::exception&) {
    }
    std::sort(main_stat_ids_.begin(), main_stat_ids_.end());
}

void App::reset_config() {
    swcore::SolverConfig fresh;
    fresh.pool.target_unit_id = config_.pool.target_unit_id;  // garde le monstre
    fresh.pool.minimum_rune_level = 12;
    fresh.pool.minimum_stars = 6;
    fresh.scenarios = {{"", {}, true}};  // nom généré : "Tous sets"
    fresh.objective.weights = {{"attack", 1.0}};
    fresh.limits.top_n = 50;
    fresh.limits.time_budget_ms = 30000;
    config_ = std::move(fresh);
}

// Noms de métriques canoniques (une config CLI peut utiliser SPD, ATK...).
void App::normalize_config() {
    std::map<std::string, swcore::StatConstraint> constraints;
    for (const auto& [name, constraint] : config_.constraints)
        constraints[canonical_metric(name)] = constraint;
    config_.constraints = std::move(constraints);
    std::map<std::string, double> weights;
    for (const auto& [name, weight] : config_.objective.weights)
        weights[canonical_metric(name)] = weight;
    config_.objective.weights = std::move(weights);
    std::map<std::string, double> caps;
    for (const auto& [name, cap] : config_.objective.caps) caps[canonical_metric(name)] = cap;
    config_.objective.caps = std::move(caps);
    for (auto& name : config_.objective.sort_metrics) name = canonical_metric(name);
}

// Trois zones verticales : paramètres (défilants, séparateur déplaçable),
// barre fixe de lancement, panneau de résultats toujours visible (>= 35 %).
void App::render_search_tab() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float total = ImGui::GetContentRegionAvail().y;
    const float bar = ImGui::GetFrameHeightWithSpacing() * 2.0f + style.WindowPadding.y;
    const float splitter = 6.0f;
    const float spacing = style.ItemSpacing.y * 3.0f;
    const float min_params = ImGui::GetFrameHeightWithSpacing() * 3.0f;
    const float max_params =
        std::max(min_params, total - bar - splitter - spacing - total * 0.35f);
    if (parameters_height_ <= 0) parameters_height_ = total * 0.45f;
    parameters_height_ = std::clamp(parameters_height_, min_params, max_params);
    // Sections repliées : la zone se réduit à leur hauteur (pas de grand vide).
    float height = parameters_height_;
    if (parameters_content_ > 0)
        height = std::clamp(parameters_content_ + style.WindowPadding.y * 2.0f, min_params,
                            parameters_height_);

    ImGui::BeginChild("##parameters", ImVec2(0, height), ImGuiChildFlags_Borders);
    render_config_files();
    render_target_and_pool();
    render_lock_settings();
    render_scenarios();
    render_main_stats();
    render_constraints();
    render_objective();
    render_craft(false);
    render_craft(true);
    render_limits();
    parameters_content_ = ImGui::GetCursorPosY() - style.WindowPadding.y;
    ImGui::EndChild();

    // Séparateur déplaçable à la souris.
    ImGui::InvisibleButton("##splitter", ImVec2(-1.0f, splitter));
    if (ImGui::IsItemActive()) {
        parameters_height_ = std::clamp(height + ImGui::GetIO().MouseDelta.y, min_params,
                                        max_params);
        parameters_content_ = 0;  // l'utilisateur choisit la hauteur
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float y = (min.y + max.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(min.x, y), ImVec2(max.x, y),
            ImGui::GetColorU32(ImGui::IsItemHovered() || ImGui::IsItemActive()
                                   ? ImGuiCol_SeparatorHovered
                                   : ImGuiCol_Separator),
            2.0f);
    }

    ImGui::BeginChild("##run_bar", ImVec2(0, bar), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    render_run_controls();
    ImGui::EndChild();

    ImGui::BeginChild("##results_panel", ImVec2(0, 0), ImGuiChildFlags_Borders);
    render_results();
    ImGui::EndChild();
}

bool App::section(const char* label) {
    // Premier lancement : tout replié sauf "Monstre et pool" ; ensuite l'état
    // ouvert / fermé est mémorisé dans settings.json.
    const auto it = settings_.sections.find(label);
    const bool stored = it != settings_.sections.end()
                            ? it->second
                            : std::string_view(label) == "Monstre et pool";
    ImGui::SetNextItemOpen(stored, ImGuiCond_Once);
    const bool open = ImGui::CollapsingHeader(label);
    if (open != stored) {
        settings_.sections[label] = open;
        save_settings(data_.user_directory(), settings_);
    }
    return open;
}

void App::render_config_files() {
    if (!section("Configurations")) return;
    const fs::path directory = data_.user_directory() / "configs";
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    ImGui::InputTextWithHint("##config_name", "Nom de la configuration", config_name_,
                             sizeof config_name_);
    ImGui::SameLine();
    ImGui::BeginDisabled(config_name_[0] == '\0');
    if (ImGui::Button("Enregistrer")) {
        std::error_code error;
        fs::create_directories(directory, error);
        const fs::path file = directory / from_utf8(safe_file_name(config_name_) + ".json");
        std::ofstream output(file, std::ios::binary);
        if (output) {
            output << config_.to_json() << '\n';
            set_status("Configuration enregistrée : " + to_utf8(file.filename()));
        }
    }
    ImGui::EndDisabled();

    // Configurations nommées existantes.
    std::vector<fs::path> files;
    std::error_code error;
    if (fs::is_directory(directory, error))
        for (const auto& entry : fs::directory_iterator(directory, error))
            if (entry.path().extension() == ".json" && entry.path().filename() != "last.json")
                files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    if (ImGui::BeginCombo("##configs", "Charger une configuration")) {
        for (const auto& file : files) {
            if (ImGui::Selectable(to_utf8(file.stem()).c_str())) {
                config_ = swcore::SolverConfig::load(file, data_.mapping_directory());
                normalize_config();
                set_status("Configuration chargée : " + to_utf8(file.filename()));
            }
        }
        if (files.empty()) ImGui::TextColored(color::muted, "Aucune configuration");
        ImGui::EndCombo();
    }

    if (ImGui::Button("Importer un JSON...")) {
        const auto selection = pfd::open_file("Importer une configuration",
                                              to_utf8(data_.root()),
                                              {"Configuration JSON", "*.json"})
                                   .result();
        if (!selection.empty()) {
            std::vector<std::string> messages;
            config_ = swcore::SolverConfig::load(
                from_utf8(selection.front()), data_.mapping_directory(),
                [&](const std::string& message) { messages.push_back(message); });
            normalize_config();
            data_.warnings.insert(data_.warnings.end(), messages.begin(), messages.end());
            set_status("Configuration importée (" + std::to_string(messages.size()) +
                       " avertissement(s)).");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Exporter en JSON...")) {
        const auto target = pfd::save_file("Exporter la configuration",
                                           to_utf8(data_.root() / "config.json"),
                                           {"Configuration JSON", "*.json"})
                                .result();
        if (!target.empty()) {
            std::ofstream output(from_utf8(target), std::ios::binary);
            if (output) {
                output << config_.to_json() << '\n';
                set_status("Configuration exportée : " + target);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Valeurs par défaut")) reset_config();
    for (const auto& error_text : config_.errors)
        ImGui::TextColored(color::error, "Erreur de configuration : %s", error_text.c_str());
}

void App::render_target_and_pool() {
    if (!section("Monstre et pool")) return;

    // Monstre cible : liste déroulante avec recherche.
    const auto* target =
        config_.pool.target_unit_id ? find_monster(*config_.pool.target_unit_id) : nullptr;
    const std::string preview =
        target ? monster_name(target->unit_master_id) + " (niv. " +
                     std::to_string(target->level) + ")"
               : std::string{"Choisir un monstre"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
    if (ImGui::BeginCombo("Monstre cible", preview.c_str(),
                          ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##target_filter", "Rechercher", target_filter_,
                                 sizeof target_filter_);
        const std::string filter = fold(target_filter_);
        for (const auto& monster : data_.monsters) {
            const std::string name = monster_name(monster.unit_master_id);
            if (!filter.empty() && fold(name).find(filter) == std::string::npos) continue;
            const std::string label = name + "  niv. " + std::to_string(monster.level) +
                                      "  " + std::to_string(monster.stars) + "*##" +
                                      std::to_string(monster.unit_id);
            if (ImGui::Selectable(label.c_str(), target == &monster))
                config_.pool.target_unit_id = monster.unit_id;
        }
        ImGui::EndCombo();
    }

    ImGui::Checkbox("Runes de l'inventaire uniquement", &config_.pool.inventory_only);

    // Exclusion temporaire (locked_unit_ids de la config nommée) : sans rapport
    // avec la table des verrous.
    auto& locked = config_.pool.locked_unit_ids;
    const std::string locked_preview = std::to_string(locked.size()) + " monstre(s)";
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
    if (ImGui::BeginCombo("Monstres exclus", locked_preview.c_str(),
                          ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##locked_filter", "Rechercher", locked_filter_,
                                 sizeof locked_filter_);
        const std::string filter = fold(locked_filter_);
        for (const auto& monster : data_.monsters) {
            // Rien à exclure sans rune ni artefact porté.
            if (monster.runes.empty() && monster.artifacts.empty()) continue;
            const std::string name = monster_name(monster.unit_master_id);
            if (!filter.empty() && fold(name).find(filter) == std::string::npos) continue;
            bool checked = std::find(locked.begin(), locked.end(), monster.unit_id) !=
                           locked.end();
            const std::string label = name + "  niv. " + std::to_string(monster.level) +
                                      "##lock" + std::to_string(monster.unit_id);
            if (ImGui::Checkbox(label.c_str(), &checked)) {
                if (checked) locked.push_back(monster.unit_id);
                else locked.erase(std::remove(locked.begin(), locked.end(), monster.unit_id),
                                  locked.end());
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(locked.empty());
    if (ImGui::Button("Vider##excluded")) locked.clear();
    ImGui::EndDisabled();
    ImGui::TextColored(color::muted,
                       "Exclut les runes et artefacts actuellement portés par ces monstres de "
                       "CETTE recherche uniquement. Rien n'est verrouillé.");

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::SliderInt("Niveau minimum des runes", &config_.pool.minimum_rune_level, 0, 15);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::SliderInt("Étoiles minimales", &config_.pool.minimum_stars, 1, 6);

    int projection = config_.pool.projection_level == 12   ? 1
                     : config_.pool.projection_level == 15 ? 2
                                                            : 0;
    ImGui::TextUnformatted("Projection des runes :");
    for (int i = 0; i < 3; ++i) {
        ImGui::SameLine();
        ImGui::RadioButton(kProjection[i], &projection, i);
    }
    config_.pool.projection_level =
        projection == 1 ? 12 : projection == 2 ? 15 : swcore::kProjectionCurrent;
    ImGui::SameLine();
    ImGui::TextColored(color::muted, "stat principale projetée, substats inchangées");
}

void App::render_lock_settings() {
    if (!section("Verrous permanents")) return;
    int mode = config_.locks.mode == swcore::LockMode::Ignore ? 1 : 0;
    ImGui::RadioButton("Respecter les verrous", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Ignorer les verrous", &mode, 1);
    config_.locks.mode = mode == 1 ? swcore::LockMode::Ignore : swcore::LockMode::Respect;
    ImGui::BeginDisabled(mode == 1);
    ImGui::Checkbox("Garder les runes verrouillées de ce monstre (slots figés)",
                    &config_.locks.keep_target_locked);
    ImGui::EndDisabled();
    ImGui::TextColored(color::muted, "%s runes et %s artefacts exclus par verrou",
                       group_digits(static_cast<long long>(lock_excluded_)).c_str(),
                       group_digits(static_cast<long long>(lock_excluded_artifacts_)).c_str());
    ImGui::SameLine();
    if (ImGui::TextLink("Gérer les verrous")) show_page(Page::Monsters);
}

void App::render_scenarios() {
    if (!section("Sets")) return;
    // Sets connus du mapping, triés par nom.
    std::vector<std::pair<std::string, int>> sets;
    for (const auto& [id, name] : data_.mapping.set_names) sets.emplace_back(name, id);
    std::sort(sets.begin(), sets.end());

    auto& scenarios = config_.scenarios;
    std::optional<std::size_t> remove;
    std::optional<std::size_t> duplicate;
    for (std::size_t i = 0; i < scenarios.size(); ++i) {
        auto& scenario = scenarios[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SeparatorText(("Scénario " + std::to_string(i + 1)).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        // Vide : nom généré depuis les sets (affiché en indication) ; un nom saisi prime.
        const std::string generated = swcore::scenario_label(scenario, data_.mapping.set_names);
        ImGui::InputTextWithHint("Nom", generated.c_str(), &scenario.name);
        ImGui::SameLine();
        ImGui::Checkbox("Tous sets", &scenario.any_set);
        ImGui::SameLine();
        if (ImGui::SmallButton("Dupliquer")) duplicate = i;
        ImGui::SameLine();
        if (ImGui::SmallButton("Supprimer")) remove = i;
        if (!scenario.any_set) {
            std::optional<std::size_t> remove_set;
            int pieces = 0;
            for (std::size_t j = 0; j < scenario.sets.size(); ++j) {
                auto& requirement = scenario.sets[j];
                ImGui::PushID(static_cast<int>(j));
                const auto name = data_.mapping.set_names.find(requirement.set_id);
                const std::string current =
                    name == data_.mapping.set_names.end()
                        ? swcore::text::unknown(requirement.set_id)
                        : name->second;
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
                if (ImGui::BeginCombo("##set", current.c_str(), ImGuiComboFlags_HeightLarge)) {
                    for (const auto& [set_name, set_id] : sets) {
                        const std::string bonus = set_bonus(data_.effects, set_id);
                        const std::string label =
                            set_name + (bonus.empty() ? "" : "   (" + bonus + ")");
                        if (ImGui::Selectable(label.c_str(), set_id == requirement.set_id))
                            requirement.set_id = set_id;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
                ImGui::InputInt("pièces", &requirement.pieces);
                requirement.pieces = std::clamp(requirement.pieces, 1, 6);
                pieces += requirement.pieces;
                ImGui::SameLine();
                ImGui::TextColored(color::muted, "%s",
                                   set_bonus(data_.effects, requirement.set_id).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Retirer")) remove_set = j;
                ImGui::PopID();
            }
            if (remove_set) scenario.sets.erase(scenario.sets.begin() +
                                                static_cast<std::ptrdiff_t>(*remove_set));
            if (ImGui::SmallButton("Ajouter un set") && !sets.empty())
                scenario.sets.push_back({sets.front().second, 2});
            if (pieces != 6)
                ImGui::TextColored(color::warning,
                                   "Les pièces totalisent %d : il en faut 6 (sinon aucun "
                                   "build).",
                                   pieces);
        }
        ImGui::PopID();
    }
    if (duplicate)
        scenarios.insert(scenarios.begin() + static_cast<std::ptrdiff_t>(*duplicate) + 1,
                         scenarios[*duplicate]);
    if (remove) scenarios.erase(scenarios.begin() + static_cast<std::ptrdiff_t>(*remove));
    if (ImGui::Button("Ajouter un scénario"))
        scenarios.push_back({"", {}, false});  // nom généré depuis les sets
    if (scenarios.empty())
        ImGui::TextColored(color::warning, "Aucun scénario : aucune recherche ne sera lancée.");
}

void App::render_main_stats() {
    if (!section("Stats principales (slots 2, 4, 6)")) return;
    ImGui::TextColored(color::muted, "Slots 1, 3 et 5 : stat principale fixe. "
                                     "Aucune case cochée = toutes autorisées.");
    for (const int slot : {2, 4, 6}) {
        auto& selection = config_.main_stats[static_cast<std::size_t>(slot)];
        ImGui::PushID(slot);
        ImGui::Text("Slot %d", slot);
        ImGui::SameLine(ImGui::GetFontSize() * 4.0f);
        bool all = selection.all || selection.stat_ids.empty();
        if (ImGui::Checkbox("Toutes", &all) && all) {
            selection.all = false;
            selection.stat_ids.clear();
        }
        for (const int id : main_stat_ids_) {
            ImGui::SameLine();
            bool checked = !selection.all &&
                           std::find(selection.stat_ids.begin(), selection.stat_ids.end(),
                                     id) != selection.stat_ids.end();
            const std::string label = swcore::text::stat_name(data_.mapping, id) + "##" +
                                      std::to_string(id);
            if (ImGui::Checkbox(label.c_str(), &checked)) {
                selection.all = false;
                if (checked) selection.stat_ids.push_back(id);
                else
                    selection.stat_ids.erase(std::remove(selection.stat_ids.begin(),
                                                         selection.stat_ids.end(), id),
                                             selection.stat_ids.end());
                std::sort(selection.stat_ids.begin(), selection.stat_ids.end());
            }
        }
        ImGui::PopID();
    }
}

void App::render_constraints() {
    if (!section("Contraintes")) return;
    if (!ImGui::BeginTable("##constraints", 3,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                               ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("Stat");
    ImGui::TableSetupColumn("Minimum");
    ImGui::TableSetupColumn("Maximum");
    ImGui::TableHeadersRow();
    for (const auto& metric : kMetrics) {
        ImGui::PushID(metric.key);
        const auto it = config_.constraints.find(metric.key);
        std::optional<double> minimum =
            it == config_.constraints.end() ? std::nullopt : it->second.minimum;
        std::optional<double> maximum =
            it == config_.constraints.end() ? std::nullopt : it->second.maximum;
        bool use_min = minimum.has_value();
        bool use_max = maximum.has_value();
        double min_value = minimum.value_or(0);
        double max_value = maximum.value_or(0);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(metric.label);
        ImGui::TableSetColumnIndex(1);
        ImGui::Checkbox("##use_min", &use_min);
        ImGui::SameLine();
        ImGui::BeginDisabled(!use_min);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        const char* number_format =
            std::string_view(metric.key) == "rune_efficiency" ? "%.2f" : "%.0f";
        ImGui::InputDouble("##min", &min_value, 0, 0, number_format);
        ImGui::EndDisabled();
        ImGui::TableSetColumnIndex(2);
        ImGui::Checkbox("##use_max", &use_max);
        ImGui::SameLine();
        ImGui::BeginDisabled(!use_max);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        ImGui::InputDouble("##max", &max_value, 0, 0, number_format);
        ImGui::EndDisabled();
        if (use_min || use_max)
            config_.constraints[metric.key] = {
                use_min ? std::optional<double>{min_value} : std::nullopt,
                use_max ? std::optional<double>{max_value} : std::nullopt};
        else if (it != config_.constraints.end())
            config_.constraints.erase(it);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void App::render_objective() {
    if (!section("Objectif")) return;
    if (ImGui::Button("Réinitialiser l'objectif")) {
        config_.objective.weights = {{"attack", 1.0}};
        config_.objective.caps.clear();
        config_.objective.sort_metrics.clear();
    }
    ImGui::SameLine();
    const std::string sort_preview =
        config_.objective.sort_metrics.empty() ? "Aucun" : config_.objective.sort_metrics.front();
    std::string sort_label = "Aucun";
    for (const auto& metric : kMetrics)
        if (sort_preview == metric.key) sort_label = metric.label;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::BeginCombo("Tri à score égal", sort_label.c_str())) {
        if (ImGui::Selectable("Aucun", config_.objective.sort_metrics.empty()))
            config_.objective.sort_metrics.clear();
        for (const auto& metric : kMetrics)
            if (ImGui::Selectable(metric.label, sort_preview == metric.key))
                config_.objective.sort_metrics = {metric.key};
        ImGui::EndCombo();
    }
    if (!ImGui::BeginTable("##objective", 3,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                               ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("Stat");
    ImGui::TableSetupColumn("Poids");
    ImGui::TableSetupColumn("Plafond");
    ImGui::TableHeadersRow();
    for (const auto& metric : kMetrics) {
        ImGui::PushID(metric.key);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(metric.label);
        ImGui::TableSetColumnIndex(1);
        const auto weight_it = config_.objective.weights.find(metric.key);
        double weight = weight_it == config_.objective.weights.end() ? 0 : weight_it->second;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        if (ImGui::InputDouble("##weight", &weight, 0, 0, "%.3g")) {
            if (weight == 0) config_.objective.weights.erase(metric.key);
            else config_.objective.weights[metric.key] = weight;
        }
        ImGui::TableSetColumnIndex(2);
        const auto cap_it = config_.objective.caps.find(metric.key);
        bool use_cap = cap_it != config_.objective.caps.end();
        double cap = use_cap ? cap_it->second : 0;
        if (ImGui::Checkbox("##use_cap", &use_cap) && !use_cap)
            config_.objective.caps.erase(metric.key);
        ImGui::SameLine();
        ImGui::BeginDisabled(!use_cap);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        ImGui::InputDouble("##cap", &cap, 0, 0, "%.0f");
        ImGui::EndDisabled();
        if (use_cap) config_.objective.caps[metric.key] = cap;
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void App::render_craft(bool gems) {
    if (!section(gems ? "Gemmes" : "Meules")) return;
    ImGui::PushID(gems ? "gems" : "grinds");
    render_craft_rules(config_, gems, craft_global_[gems ? 1 : 0]);
    render_craft_limits(gems);
    ImGui::PopID();
}

// Règles par stat (meules ou gemmes) avec un réglage commun : choisir un mode
// ou une rareté dans les menus communs l'applique tout de suite à toutes les
// lignes ; chaque ligne reste modifiable ensuite (valeur exacte, etc.).
void App::render_craft_rules(swcore::SolverConfig& config, bool gems, CraftGlobal& global) {
    auto& rules = gems ? config.gems : config.grindstones;
    const auto& ranges = gems ? gem_ranges_ : grind_ranges_;
    std::vector<int> stats;
    for (const auto& [stat, by_rarity] : ranges)
        if (gems || swcore::is_grindable_stat(stat)) stats.push_back(stat);

    const auto apply_all = [&] {
        rules.clear();
        if (global.mode != 0)
            for (const int stat : stats)
                rules[stat] = {static_cast<swcore::GrindstoneMode>(global.mode), global.rarity, 0};
    };
    ImGui::TextUnformatted("Toutes les stats :");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    if (ImGui::Combo("##all_mode", &global.mode, "Off\0Min\0Max\0")) apply_all();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    if (ImGui::BeginCombo("##all_rarity", rarity_label(global.rarity))) {
        for (int r = 1; r <= 5; ++r)
            if (ImGui::Selectable(rarity_label(r), global.rarity == r)) {
                global.rarity = r;
                apply_all();
            }
        ImGui::EndCombo();
    }
    ImGui::TextColored(color::muted, gems ? "Stats autorisées pour la gemme (par stat)."
                                          : "Stats meulables : PV, ATQ, DEF (fixe et %%), VIT.");

    if (ImGui::BeginTable("##rules", 4, ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_BordersInnerV |
                                           ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Stat");
        ImGui::TableSetupColumn("Mode");
        ImGui::TableSetupColumn("Rareté");
        ImGui::TableSetupColumn("Valeur exacte");
        ImGui::TableHeadersRow();
        for (const int stat : stats) {
            ImGui::PushID(stat);
            const auto it = rules.find(stat);
            swcore::GrindstoneRule rule =
                it == rules.end() ? swcore::GrindstoneRule{swcore::GrindstoneMode::Off, 5, 0}
                                  : it->second;
            if (rule.rarity < 1 || rule.rarity > 5) rule.rarity = 5;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(swcore::text::stat_name(data_.mapping, stat).c_str());
            ImGui::TableSetColumnIndex(1);
            int mode = static_cast<int>(rule.mode);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
            ImGui::Combo("##mode", &mode, kRollModes, IM_ARRAYSIZE(kRollModes));
            rule.mode = static_cast<swcore::GrindstoneMode>(mode);
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
            if (ImGui::BeginCombo("##rarity", rarity_label(rule.rarity))) {
                for (int r = 1; r <= 5; ++r)
                    if (ImGui::Selectable(rarity_label(r), rule.rarity == r)) rule.rarity = r;
                ImGui::EndCombo();
            }
            ImGui::TableSetColumnIndex(3);
            // Plage [min, max] de la rareté choisie (grindstone / enchanted_gem.json).
            const std::pair<int, int>* range = nullptr;
            if (const auto by_stat = ranges.find(stat); by_stat != ranges.end())
                if (const auto found = by_stat->second.find(rule.rarity);
                    found != by_stat->second.end())
                    range = &found->second;
            const bool known = range != nullptr;
            if (rule.mode == swcore::GrindstoneMode::Exact && known) {
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
                ImGui::InputInt("##exact", &rule.exact, 0);
                rule.exact = std::clamp(rule.exact, range->first, range->second);
                ImGui::SameLine();
                ImGui::TextColored(color::muted, "(%d - %d)", range->first, range->second);
            } else if (known) {
                ImGui::TextColored(color::muted, "%d - %d", range->first, range->second);
            }
            if (rule.mode == swcore::GrindstoneMode::Off || !known) rules.erase(stat);
            else rules[stat] = rule;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// Plafonds (max_new_*), regem et niveau minimum de craft de l'Optimiseur.
void App::render_craft_limits(bool gems) {
    // Gemmes : plafond visible. Meules : meulage complet par défaut ; le plafond
    // (max_new_grinds) est rangé dans "Avancé", replié.
    const bool show_limit = gems || ImGui::TreeNode("Avancé");
    if (show_limit) {
        auto& limit = gems ? config_.max_new_gems : config_.max_new_grinds;
        if (!gems)
            ImGui::TextColored(color::muted,
                               "Limite le nombre de meules ; active le choix des meules à "
                               "poser, sinon toutes les substats sélectionnées sont meulées.");
        bool unlimited = !limit.has_value();
        if (ImGui::Checkbox(gems ? "Nombre de nouvelles gemmes illimité"
                                 : "Aucun plafond (meulage complet)",
                            &unlimited))
            limit = unlimited ? std::nullopt : std::optional<int>{2};
        if (limit) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
            int value = *limit;
            ImGui::InputInt(gems ? "max_new_gems" : "max_new_grinds", &value);
            limit = std::max(0, value);
        }
        if (!gems) ImGui::TreePop();
    }
    if (gems)
        ImGui::Checkbox("Regemmer les runes déjà gemmées (regem_existing)",
                        &config_.gem_regem_existing);
    else {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        ImGui::InputInt("Niveau minimum pour meules et gemmes (craft_min_level)",
                        &config_.craft_min_level);
        config_.craft_min_level = std::clamp(config_.craft_min_level, 0, 15);
    }
}

void App::render_limits() {
    if (!section("Limites")) return;
    auto& limits = config_.limits;
    bool all = limits.top_n == 0;
    if (ImGui::Checkbox("Tous les builds valides", &all)) limits.top_n = all ? 0 : 50;
    if (!all) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        ImGui::InputInt("Meilleurs builds conservés (top_n)", &limits.top_n);
        limits.top_n = std::max(1, limits.top_n);
    } else {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
        ImGui::InputScalar("Plafond de builds (max_results)", ImGuiDataType_U64,
                           &limits.max_results);
        if (limits.max_results > 1'000'000)
            ImGui::TextColored(color::warning,
                               "Jusqu'à %s builds en mémoire (environ 100 octets chacun).",
                               group_digits(static_cast<long long>(limits.max_results)).c_str());
    }
    double seconds = static_cast<double>(limits.time_budget_ms) / 1000.0;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::InputDouble("Temps maximum (s, 0 = illimité)", &seconds, 0, 0, "%.0f"))
        limits.time_budget_ms = static_cast<std::int64_t>(std::max(0.0, seconds) * 1000.0);
    bool use_min_score = limits.min_score.has_value();
    double min_score = limits.min_score.value_or(0);
    ImGui::Checkbox("Score minimum", &use_min_score);
    ImGui::SameLine();
    ImGui::BeginDisabled(!use_min_score);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    ImGui::InputDouble("##min_score", &min_score, 0, 0, "%.0f");
    ImGui::EndDisabled();
    limits.min_score = use_min_score ? std::optional<double>{min_score} : std::nullopt;
    if (ImGui::TreeNode("Avancé")) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
        ImGui::InputScalar("Nœuds maximum (0 = illimité)", ImGuiDataType_U64,
                           &limits.node_limit);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        ImGui::InputInt("max_builds_per_scenario", &limits.max_builds_per_scenario);
        limits.max_builds_per_scenario = std::max(0, limits.max_builds_per_scenario);
        ImGui::TreePop();
    }
}

void App::render_run_controls() {
    // Runes candidates par slot (filtres du pool uniquement, sans recherche),
    // recalculées quand la config ou les données changent.
    const std::string signature = config_.to_json() + std::to_string(data_.generation) +
                                  "/" + std::to_string(data_.locks_revision);
    if (signature != config_signature_) {
        auto counted = config_;
        counted.lock_table = data_.locks;
        candidate_counts_ = swcore::count_candidate_runes(data_.runes, counted);
        lock_excluded_ = swcore::count_lock_excluded_runes(data_.runes, counted);
        // Artefacts (portés ou non) que les verrous réservent à un autre monstre.
        lock_excluded_artifacts_ = 0;
        if (config_.locks.mode == swcore::LockMode::Respect) {
            const std::uint64_t target = config_.pool.target_unit_id.value_or(0);
            const auto count = [&](const swcore::Artifact& artifact) {
                lock_excluded_artifacts_ +=
                    swcore::artifact_usable_by(data_.locks, artifact, target) ? 0 : 1;
            };
            for (const auto& monster : data_.monsters)
                for (const auto& artifact : monster.artifacts) count(artifact);
            for (const auto& artifact : data_.state.spare_artifacts) count(artifact);
        }
        config_signature_ = signature;
    }
    std::string counts;
    for (std::size_t slot = 0; slot < 6; ++slot)
        counts += (slot ? " / " : "") + std::to_string(candidate_counts_[slot]);
    ImGui::Text("Runes candidates par slot : %s", counts.c_str());

    if (!solving_) {
        const bool ready = config_.pool.target_unit_id &&
                           find_monster(*config_.pool.target_unit_id) != nullptr;
        ImGui::BeginDisabled(!ready || !config_.errors.empty());
        if (ImGui::Button("Optimiser")) start_search();
        ImGui::EndDisabled();
        if (!ready) {
            ImGui::SameLine();
            ImGui::TextColored(color::muted, "Choisissez un monstre cible.");
        } else if (report_) {
            ImGui::SameLine();
            ImGui::TextColored(color::muted, "Dernière recherche : %.1f s, %s builds trouvés",
                               static_cast<double>(last_elapsed_ms_) / 1000.0,
                               group_digits(static_cast<long long>(rows_.size())).c_str());
        }
    } else {
        if (ImGui::Button("Annuler")) {
            solver_.cancel();
            report_cancelled_ = true;
        }
        const auto snapshot = solver_.snapshot();
        ImGui::SameLine();
        ImGui::Text("En cours : %.1f s, %s builds trouvés",
                    static_cast<double>(snapshot.elapsed_ms) / 1000.0,
                    group_digits(static_cast<long long>(snapshot.builds_found)).c_str());
    }
}

void App::start_search() {
    const auto* target = find_monster(*config_.pool.target_unit_id);
    if (!target) return;
    stop_sort();
    rows_.clear();
    row_order_.clear();
    report_.reset();
    selected_row_ = static_cast<std::size_t>(-1);
    report_config_ = config_;
    report_config_.lock_table = data_.locks;  // verrous du GUI (data/user/locks.json)
    report_target_ = target->unit_id;
    report_cancelled_ = false;
    solver_.start(*target, data_.runes, data_.effects, report_config_,
                  data_.mapping_directory());
    solving_ = true;
    save_state();
    set_status("Recherche lancée pour " + monster_name(target->unit_master_id) + ".");
}

void App::poll_search() {
    if (!solving_) return;
    const auto snapshot = solver_.snapshot();
    if (!snapshot.finished) return;
    last_elapsed_ms_ = snapshot.elapsed_ms;
    report_ = solver_.take_result();
    solving_ = false;
    for (auto& message : solver_.take_warnings())
        if (message.rfind("[solver-", 0) != 0) data_.warnings.push_back(std::move(message));
    build_result_rows();
    set_status(report_cancelled_ ? "Recherche annulée : résultats partiels conservés."
                                 : "Recherche terminée.");
}

}  // namespace swgui
