// Onglets de build et page "Builds sauvegardés" (data/user/saved_builds/).
// Une sauvegarde est autonome : elle contient l'instantané des runes, du
// monstre et des artefacts ; la réouverture affiche cet instantané.

#include "app.hpp"
#include "widgets/gui_common.hpp"
#include "widgets/gui_components.hpp"
#include "logic/snapshot.hpp"
#include "widgets/text_util.hpp"

#include "swcore/solver/build_text.hpp"
#include "swcore/account/locks.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <system_error>

namespace swgui {
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string now_text(const char* format) {
    const std::time_t time = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, format, &local);
    return buffer;
}

// Stats d'un build : copie du monstre avec les 6 runes de calcul (fonction pure).
swcore::StatBlock build_stats_of(const swcore::AccountMonster& target,
                                 const std::array<swcore::Rune, 6>& runes,
                                 const swcore::SetEffects& effects) {
    swcore::AccountMonster copy = target;
    copy.runes.assign(runes.begin(), runes.end());
    for (auto& rune : copy.runes) rune.occupancy = swcore::RuneOccupancy::Equipped;
    return swcore::calculate_monster_stats(copy, effects);
}

// "Fatal 4 + Lame 2" (sets des 6 runes, du plus fréquent au moins fréquent).
std::string sets_text(const std::array<swcore::Rune, 6>& runes,
                      const swcore::RuneMapping& mapping) {
    std::map<int, int> counts;
    for (const auto& rune : runes) ++counts[rune.set_id];
    std::vector<std::pair<int, int>> ordered(counts.begin(), counts.end());
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string text;
    for (const auto& [set_id, count] : ordered)
        text += (text.empty() ? "" : " + ") + swcore::text::set_name(mapping, set_id) + " " +
                std::to_string(count);
    return text;
}

}  // namespace

void App::open_result(std::size_t row_index) {
    if (!report_ || row_index >= rows_.size()) return;
    const auto& row = rows_[row_index];
    const auto& scenario = report_->scenarios[row.scenario];
    const auto* target = find_monster(report_target_);
    if (!target) {
        set_status("Monstre absent de l'export chargé.");
        return;
    }
    if (!row.compact) {
        open_build_tab(*target, scenario.builds[row.index], report_config_, scenario.name,
                       true);
        return;
    }
    // Build compact : détail recalculé par swcore (fonction de stats pure).
    auto build = swcore::expand_build(*target, data_.runes, data_.effects, report_config_,
                                      data_.game_directory(),
                                      scenario.all_builds[row.index], scenario.name);
    if (!build) {
        set_status("Impossible de recalculer ce build (rune absente de l'export).");
        return;
    }
    open_build_tab(*target, std::move(*build), report_config_, scenario.name, true);
}

void App::open_build_tab(const swcore::AccountMonster& target, swcore::SolverBuild build,
                         const swcore::SolverConfig& config, const std::string& scenario,
                         bool save, std::string warning) {
    // Déjà ouvert : on y revient.
    for (auto& tab : tabs_) {
        if (tab.kind != WorkspaceTab::Kind::Build) continue;
        bool same = tab.target.unit_id == target.unit_id &&
                    tab.build.variants == build.variants;
        for (std::size_t i = 0; same && i < 6; ++i)
            same = tab.build.runes[i].rune_id == build.runes[i].rune_id;
        if (same) {
            activate_tab(tab.uid);
            return;
        }
    }
    WorkspaceTab tab;
    tab.kind = WorkspaceTab::Kind::Build;
    tab.target = target;
    tab.build = std::move(build);
    tab.config = config;
    tab.scenario = scenario;
    tab.warning = std::move(warning);
    tab.report = swcore::make_build_report(tab.target, tab.build);
    tab.uid = next_tab_uid_++;
    std::ostringstream text;
    swcore::text::print_build(text, tab.report, tab.target, data_.reference, data_.mapping,
                              120);
    tab.text = text.str();
    // Titre : "Raoq - Fatal + Blade - ATQ 3 740" (métrique si l'objectif n'en a qu'une).
    std::string metric = "Score";
    if (config.objective.weights.size() == 1)
        for (const auto& info : kMetrics)
            if (config.objective.weights.begin()->first == info.key) metric = info.label;
    tab.title = monster_name(target.unit_master_id) + " - " + scenario + " - " + metric +
                " " + swcore::text::format_score(tab.build.score);
    if (save) save_build(tab);
    activate_tab(tab.uid);
    tabs_.push_back(std::move(tab));
}

void App::render_build_tab(WorkspaceTab& tab) {
    const auto& target = tab.target;
    const auto& report = tab.report;
    if (ImGui::Button("Copier en texte")) ImGui::SetClipboardText(tab.text.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Fermer")) {
        tab.open = false;
        view_.tab_closed(tab.uid);
    }

    // Équiper puis Verrouiller : deux boutons scindés, deux clics indépendants.
    render_build_actions(tab);
    if (!tab.warning.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, color::warning);
        ImGui::TextWrapped("%s", tab.warning.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Text("%s - niveau %d   |   Scénario : %s   |   Score %s",
                monster_name(target.unit_master_id).c_str(), target.level,
                tab.scenario.c_str(), swcore::text::format_score(report.score).c_str());
    if (report.projection_level > 0)
        ImGui::TextColored(color::accent,
                           "Stats calculées avec runes projetées à +%d (stat principale)",
                           report.projection_level);
    if (report.simulated_craft)
        ImGui::TextColored(color::accent, "Stats calculées avec meules et gemmes simulées");

    ImGui::BeginChild("##build", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (ImGui::BeginTable("##layout", 2,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("stats", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupColumn("runes", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        StatComparison stats;
        stats.headers = {"Équipé aujourd'hui", "Nouveau total [+vert]", "Diff."};
        for (std::size_t i = 0; i < 8; ++i) {
            stats.left[i] = report.equipped_stats[i].total;
            stats.right[i] = report.stats[i].total;
            stats.green[i] = report.stats[i].green;
        }
        const auto equipped = swcore::calculate_derived_metrics(target.stats.total);
        stats.left_metrics = {equipped.expected_damage, equipped.effective_hp,
                              equipped.effective_hp_broken_def};
        stats.right_metrics = {report.metrics.expected_damage, report.metrics.effective_hp,
                               report.metrics.effective_hp_broken_def};
        stats.left_efficiency = swcore::text::equipped_efficiency(target);
        stats.right_efficiency = report.metrics.rune_efficiency;
        render_stat_comparison(stats);

        ImGui::TableSetColumnIndex(1);
        std::array<bool, 6> present{};
        present.fill(true);
        render_rune_cards(report.runes, present, data_.mapping);
        render_artifacts(target, data_.root(), false);
        ImGui::EndTable();
    }
    // Blocs agrégés "Gemmes à utiliser" / "Meules à utiliser" (texte de swcore).
    std::ostringstream blocks;
    swcore::text::print_craft_blocks(blocks, report, data_.mapping);
    if (!blocks.str().empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(blocks.str().c_str());
    }
    ImGui::EndChild();
}

void App::save_build(const WorkspaceTab& tab) {
    // Déjà enregistré (même monstre, mêmes runes, mêmes variantes) : rien à faire.
    if (!saved_loaded_) refresh_saved_builds();
    for (const auto& saved : saved_builds_) {
        if (saved.unit_id != tab.target.unit_id || saved.variants != tab.build.variants)
            continue;
        bool same = true;
        for (std::size_t slot = 0; same && slot < 6; ++slot)
            same = saved.rune_ids[slot] == tab.build.runes[slot].rune_id;
        if (same) return;
    }
    const fs::path directory = data_.user_directory() / "saved_builds";
    std::error_code error;
    fs::create_directories(directory, error);
    json runes = json::array();         // runes de calcul (projection, craft)
    json actual_runes = json::array();  // runes réelles
    json ids = json::array();
    for (std::size_t slot = 0; slot < 6; ++slot) {
        runes.push_back(rune_to_json(tab.build.runes[slot]));
        actual_runes.push_back(rune_to_json(tab.build.actual_runes[slot]));
        ids.push_back(tab.build.runes[slot].rune_id);
    }
    const json root{
        {"version", 2},
        {"date", now_text("%Y-%m-%d %H:%M")},
        {"export_file", to_utf8(data_.export_file.path.filename())},
        {"monster",
         {{"unit_id", tab.target.unit_id},
          {"unit_master_id", tab.target.unit_master_id},
          {"name", monster_name(tab.target.unit_master_id)}}},
        {"scenario", tab.scenario},
        {"score", tab.build.score},
        {"rune_ids", ids},
        {"variants", tab.build.variants},
        {"projection_level", tab.build.projection_level},
        {"config", json::parse(tab.config.to_json())},
        {"snapshot",
         {{"monster", monster_to_json(tab.target)},
          {"runes", runes},
          {"actual_runes", actual_runes}}}};
    const std::string name = now_text("%Y%m%d-%H%M%S") + "_" +
                             std::to_string(tab.target.unit_id) + "_" +
                             std::to_string(static_cast<long long>(tab.build.score)) + ".json";
    std::ofstream output(directory / name, std::ios::binary);
    if (output) output << root.dump(2) << '\n';
    saved_loaded_ = false;  // la liste sera relue
}

void App::refresh_saved_builds() {
    saved_builds_.clear();
    const fs::path directory = data_.user_directory() / "saved_builds";
    std::error_code error;
    if (fs::is_directory(directory, error)) {
        for (const auto& entry : fs::directory_iterator(directory, error)) {
            if (entry.path().extension() != ".json") continue;
            std::ifstream input(entry.path());
            try {
                json root;
                input >> root;
                SavedBuild saved;
                saved.file = entry.path();
                saved.monster = root.at("monster").value("name", std::string{});
                saved.scenario = root.value("scenario", std::string{});
                saved.date = root.value("date", std::string{});
                saved.score = root.value("score", 0.0);
                saved.unit_id = root.at("monster").value("unit_id", std::uint64_t{0});
                saved.rune_ids =
                    root.value("rune_ids", std::array<std::uint64_t, 6>{});
                saved.variants = root.value("variants", std::array<int, 6>{});
                // Sets : depuis les runes de l'instantané (sinon inconnus).
                if (const auto snapshot = root.find("snapshot"); snapshot != root.end()) {
                    std::array<swcore::Rune, 6> runes{};
                    for (std::size_t slot = 0; slot < 6; ++slot)
                        runes[slot] = rune_from_json(snapshot->at("runes").at(slot));
                    saved.sets = sets_text(runes, data_.mapping);
                }
                saved_builds_.push_back(std::move(saved));
            } catch (const std::exception&) {
                // Fichier illisible : ignoré.
            }
        }
    }
    saved_loaded_ = true;
}

void App::open_saved_build(const SavedBuild& saved) {
    std::ifstream input(saved.file);
    try {
        json root;
        input >> root;
        const auto config =
            swcore::SolverConfig::parse(root.at("config").dump(), data_.game_directory());
        const std::string scenario = root.value("scenario", std::string{});
        if (!root.contains("snapshot")) {
            // Ancien format (sans instantané) : recalcul depuis l'export courant, signalé.
            const auto unit_id = root.at("monster").at("unit_id").get<std::uint64_t>();
            const auto* target = find_monster(unit_id);
            swcore::CompactBuild compact;
            compact.rune_ids = root.at("rune_ids").get<std::array<std::uint64_t, 6>>();
            compact.variants = root.at("variants").get<std::array<int, 6>>();
            auto build = target ? swcore::expand_build(*target, data_.runes, data_.effects,
                                                       config, data_.game_directory(),
                                                       compact, scenario)
                                : std::nullopt;
            if (!build) {
                set_status("Ancienne sauvegarde sans instantané : runes absentes de l'export.");
                return;
            }
            open_build_tab(*target, std::move(*build), config, scenario, false,
                           "Ancienne sauvegarde sans instantané : build recalculé depuis "
                           "l'export courant.");
            return;
        }

        // Instantané : monstre, runes de calcul et runes réelles tels que sauvegardés.
        const auto& snapshot = root.at("snapshot");
        swcore::AccountMonster target = monster_from_json(snapshot.at("monster"));
        target.stats = swcore::calculate_monster_stats(target, data_.effects);
        swcore::SolverBuild build;
        build.scenario = scenario;
        for (std::size_t slot = 0; slot < 6; ++slot) {
            build.runes[slot] = rune_from_json(snapshot.at("runes").at(slot));
            build.actual_runes[slot] = rune_from_json(snapshot.at("actual_runes").at(slot));
        }
        build.variants = root.value("variants", std::array<int, 6>{});
        build.projection_level = root.value("projection_level", 0);
        build.stats = build_stats_of(target, build.runes, data_.effects);
        build.efficiency = swcore::calculate_build_efficiency(build.runes);
        build.metrics = swcore::calculate_derived_metrics(build.stats.total, build.efficiency);
        build.score = root.value("score", 0.0);
        for (const auto& rune : build.runes) {
            build.grind_cost += static_cast<int>(rune.applied_grindstones.size());
            build.gem_cost += static_cast<int>(rune.applied_gems.size());
        }

        // Comparaison avec l'export courant : on signale, on n'empêche rien.
        std::string missing;
        std::string changed;
        for (const auto& rune : build.actual_runes) {
            const auto it = std::find_if(data_.runes.begin(), data_.runes.end(),
                                         [&](const swcore::Rune& current) {
                                             return current.rune_id == rune.rune_id;
                                         });
            if (it == data_.runes.end())
                missing += (missing.empty() ? "" : ", ") + std::to_string(rune.rune_id);
            else if (!same_rune(*it, rune))
                changed += (changed.empty() ? "" : ", ") + std::to_string(rune.rune_id);
        }
        std::string warning;
        if (!missing.empty() || !changed.empty()) {
            warning = "Instantané du " + root.value("date", std::string{}) + " (export " +
                      root.value("export_file", std::string{"?"}) +
                      ") : l'export courant diffère.";
            if (!missing.empty()) warning += " Runes absentes : " + missing + ".";
            if (!changed.empty()) warning += " Runes modifiées : " + changed + ".";
        }
        open_build_tab(target, std::move(build), config, scenario, false, std::move(warning));
    } catch (const std::exception& error) {
        set_status(std::string("Build sauvegardé illisible : ") + error.what());
    }
}

void App::render_saved_builds_page() {
    bool resort = !saved_loaded_;
    if (!saved_loaded_) refresh_saved_builds();
    ImGui::TextUnformatted("Builds sauvegardés");
    ImGui::Separator();
    if (ImGui::Button("Actualiser")) {
        refresh_saved_builds();
        resort = true;
    }
    ImGui::SameLine();
    ImGui::TextColored(color::muted, "Chaque build ouvert est enregistré automatiquement.");
    if (saved_builds_.empty()) {
        ImGui::TextColored(color::muted, "Aucun build sauvegardé.");
        return;
    }
    enum { kMonster, kScenario, kSets, kScore, kDate, kActions };
    const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti |
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
    if (!ImGui::BeginTable("##saved", 6, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Monstre", 0, 0.0f, kMonster);
    ImGui::TableSetupColumn("Scénario", 0, 0.0f, kScenario);
    ImGui::TableSetupColumn("Sets", 0, 0.0f, kSets);
    ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_PreferSortDescending, 0.0f, kScore);
    ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_DefaultSort |
                                        ImGuiTableColumnFlags_PreferSortDescending,
                            0.0f, kDate);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort, 0.0f, kActions);
    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
        specs && (specs->SpecsDirty || resort)) {
        std::stable_sort(saved_builds_.begin(), saved_builds_.end(),
                         [&](const SavedBuild& a, const SavedBuild& b) {
                             for (int i = 0; i < specs->SpecsCount; ++i) {
                                 const auto& spec = specs->Specs[i];
                                 int result = 0;
                                 switch (spec.ColumnUserID) {
                                 case kMonster:
                                     result = fold(a.monster).compare(fold(b.monster));
                                     break;
                                 case kScenario: result = a.scenario.compare(b.scenario); break;
                                 case kSets: result = a.sets.compare(b.sets); break;
                                 case kScore:
                                     result = a.score < b.score ? -1 : (a.score > b.score ? 1 : 0);
                                     break;
                                 case kDate: result = a.date.compare(b.date); break;
                                 default: break;
                                 }
                                 if (result != 0)
                                     return spec.SortDirection == ImGuiSortDirection_Ascending
                                                ? result < 0
                                                : result > 0;
                             }
                             return false;
                         });
        specs->SpecsDirty = false;
    }
    std::optional<std::size_t> open;
    std::optional<std::size_t> remove;
    for (std::size_t i = 0; i < saved_builds_.size(); ++i) {
        const auto& saved = saved_builds_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(kMonster);
        ImGui::TextUnformatted(saved.monster.c_str());
        ImGui::TableSetColumnIndex(kScenario);
        ImGui::TextUnformatted(saved.scenario.c_str());
        ImGui::TableSetColumnIndex(kSets);
        if (saved.sets.empty()) ImGui::TextColored(color::muted, "-");
        else ImGui::TextUnformatted(saved.sets.c_str());
        ImGui::TableSetColumnIndex(kScore);
        ImGui::TextUnformatted(swcore::text::format_score(saved.score).c_str());
        ImGui::TableSetColumnIndex(kDate);
        ImGui::TextUnformatted(saved.date.c_str());
        ImGui::TableSetColumnIndex(kActions);
        if (ImGui::SmallButton("Ouvrir")) open = i;
        ImGui::SameLine();
        if (delete_armed_ == i) {
            if (ImGui::SmallButton("Confirmer la suppression")) remove = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("Annuler")) delete_armed_ = static_cast<std::size_t>(-1);
        } else if (ImGui::SmallButton("Supprimer")) {
            delete_armed_ = i;
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (open) open_saved_build(saved_builds_[*open]);
    if (remove) {
        std::error_code error;
        fs::remove(saved_builds_[*remove].file, error);
        delete_armed_ = static_cast<std::size_t>(-1);
        saved_loaded_ = false;  // relu et retrié à la prochaine image
        set_status(error ? "Suppression impossible." : "Build supprimé.");
    }
}

}  // namespace swgui
