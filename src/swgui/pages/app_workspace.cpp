// Barre d'onglets commune aux pages Monstres et Optimiseur : onglets fixes
// (liste des monstres, recherche) puis onglets fermables (fiches de monstre,
// builds). La fiche d'un monstre réutilise les composants des onglets de build.

#include "app.hpp"
#include "widgets/gui_common.hpp"
#include "widgets/gui_components.hpp"
#include "logic/monster_view.hpp"
#include "widgets/text_util.hpp"

#include "swcore/solver/build_text.hpp"
#include "swcore/account/equip.hpp"

#include <imgui.h>

#include <algorithm>
#include <map>


namespace swgui {

namespace {

std::string awakening_label(const swcore::ReferenceMonster* reference) {
    if (!reference) return "-";
    switch (reference->awaken_level) {
    case 0: return "non éveillé";
    case 1: return "éveillé";
    case 2: return "second éveil";
    default: return std::to_string(reference->awaken_level);
    }
}

// Runes équipées par slot (valeurs réelles, sans simulation).
std::array<swcore::RuneView, 6> equipped_views(const swcore::AccountMonster& monster) {
    std::array<swcore::RuneView, 6> views{};
    for (const auto& rune : monster.runes) {
        if (rune.occupancy != swcore::RuneOccupancy::Equipped || rune.slot < 1 ||
            rune.slot > 6)
            continue;
        auto& view = views[static_cast<std::size_t>(rune.slot - 1)];
        view.rune = rune;
        view.efficiency = swcore::calculate_rune_efficiency(rune);
    }
    return views;
}

// Sets actifs : "Fatal x4 : ATQ +35 %".
std::vector<std::string> active_sets(const swcore::AccountMonster& monster,
                                     const swcore::SetEffects& effects,
                                     const swcore::RuneMapping& mapping) {
    std::vector<int> set_ids;
    std::map<int, int> pieces;
    for (const auto& rune : monster.runes) {
        if (rune.occupancy != swcore::RuneOccupancy::Equipped) continue;
        set_ids.push_back(rune.set_id);
        ++pieces[rune.set_id];
    }
    std::vector<std::string> lines;
    for (const auto& activation : swcore::calculate_set_activations(set_ids, effects)) {
        if (activation.activations <= 0) continue;
        lines.push_back(swcore::text::set_name(mapping, activation.set_id) + " x" +
                        std::to_string(pieces[activation.set_id]) + " : " +
                        set_bonus_text(effects, activation.set_id, activation.activations));
    }
    return lines;
}

// Bonus de runes cumulés (StatBlock de la fonction de stats).
std::vector<std::pair<const char*, int>> rune_bonuses(const swcore::StatBlock& stats) {
    const auto& p = stats.rune_percent;
    const auto& f = stats.rune_fixed;
    return {{"PV %", p.hp},
            {"ATQ %", p.attack},
            {"DEF %", p.defense},
            {"VIT", f.speed + p.speed},
            {"Tx crit.", f.crit_rate + p.crit_rate},
            {"Dgts crit.", f.crit_damage + p.crit_damage},
            {"RES", f.resistance + p.resistance},
            {"Préc.", f.accuracy + p.accuracy},
            {"PV (plats)", f.hp},
            {"ATQ (plats)", f.attack},
            {"DEF (plats)", f.defense}};
}

const char* lock_state_label(swcore::LockState state) {
    switch (state) {
    case swcore::LockState::Full: return "Verrouillé";
    case swcore::LockState::Partial: return "Partiel";
    case swcore::LockState::None: break;
    }
    return "Non verrouillé";
}

}  // namespace

WorkspaceTab* App::active_tab() {
    for (auto& tab : tabs_)
        if (view_.tab_active(tab.uid)) return &tab;
    return nullptr;
}

void App::activate_tab(int uid) { view_.show_tab(uid); }

void App::render_tab_strip() {
    if (tabs_.empty()) return;  // aucun onglet ouvert : barre masquée
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight() + style.ScrollbarSize + style.WindowPadding.y;
    ImGui::BeginChild("##tabs", ImVec2(0, height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& tab : tabs_) {
        const bool active = view_.tab_active(tab.uid);
        ImGui::PushID(tab.uid);
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(
                                                   active ? ImGuiCol_TabSelected : ImGuiCol_Tab));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImGui::GetStyleColorVec4(ImGuiCol_TabHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImGui::GetStyleColorVec4(ImGuiCol_TabHovered));
        if (ImGui::Button(tab.title.c_str())) activate_tab(tab.uid);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) tab.open = false;
        ImGui::SameLine(0, 0);
        if (ImGui::Button("x")) tab.open = false;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fermer l'onglet");
        ImGui::PopStyleColor(3);
        ImGui::PopID();
        ImGui::SameLine(0, style.ItemSpacing.x);
    }
    ImGui::NewLine();
    ImGui::EndChild();
    // Fermer l'onglet actif revient à la dernière page affichée.
    for (const auto& tab : tabs_)
        if (!tab.open) view_.tab_closed(tab.uid);
    tabs_.erase(std::remove_if(tabs_.begin(), tabs_.end(),
                               [](const WorkspaceTab& tab) { return !tab.open; }),
                tabs_.end());
}

void App::open_monster_tab(const MonsterRow& row) {
    // Déjà ouvert : on active l'onglet existant.
    for (auto& tab : tabs_) {
        if (tab.kind != WorkspaceTab::Kind::Monster) continue;
        const bool same = row.stored ? tab.stored && tab.unit_master_id == row.unit_master_id
                                     : !tab.stored && tab.target.unit_id == row.unit_id;
        if (same) {
            activate_tab(tab.uid);
            return;
        }
    }
    WorkspaceTab tab;
    tab.kind = WorkspaceTab::Kind::Monster;
    tab.stored = row.stored;
    tab.unit_master_id = row.unit_master_id;
    tab.quantity = row.quantity;
    if (!row.stored) {
        const auto* monster = find_monster(row.unit_id);
        if (!monster) return;
        tab.target = *monster;
    }
    tab.title = "Monstre - " + row.name;
    tab.uid = next_tab_uid_++;
    activate_tab(tab.uid);
    tabs_.push_back(std::move(tab));
}

void App::render_monster_lock_panel(const swcore::AccountMonster& monster) {
    const auto runes = lock_scope_state(data_.locks, monster, swcore::LockKind::Rune);
    const auto artifacts = lock_scope_state(data_.locks, monster, swcore::LockKind::Artifact);
    const auto state = combined_lock_state(runes, artifacts);
    const std::string name = monster_name(monster.unit_master_id);

    // Gros cadenas (état global) et son libellé.
    const float size = 40.0f;
    ImGui::BeginGroup();
    big_padlock(state, size);
    ImGui::TextColored(state == swcore::LockState::None ? color::muted : color::accent, "%s",
                       lock_state_label(state));
    ImGui::EndGroup();
    ImGui::SameLine(0, ImGui::GetFontSize());

    // Un bouton par périmètre : verrouille le reste, ou déverrouille si tout l'est.
    const auto toggle = [&](bool rune_scope, const LockScopeState& scope) {
        const swcore::LockTable before = data_.locks;
        const char* what = rune_scope ? "Runes" : "Artefacts";
        std::string text;
        if (scope.all_locked()) {
            swcore::unlock_monster(data_.locks, monster, rune_scope, !rune_scope);
            text = std::string(what) + " déverrouillé(e)s pour " + name;
        } else {
            swcore::lock_monster(data_.locks, monster, name, rune_scope, !rune_scope);
            text = std::string(what) + " verrouillé(e)s pour " + name + " (" +
                   std::to_string(scope.total) + ")";
        }
        locks_changed();
        show_toast(text, [this, before] {
            data_.locks = before;
            locks_changed();
        });
    };
    const float width = ImGui::CalcTextSize("Déverrouiller les artefacts").x +
                        ImGui::GetStyle().FramePadding.x * 2.0f;
    const auto button = [&](bool rune_scope, const LockScopeState& scope) {
        const char* label = rune_scope
                                ? (scope.all_locked() ? "Déverrouiller les runes"
                                                      : "Verrouiller les runes")
                                : (scope.all_locked() ? "Déverrouiller les artefacts"
                                                      : "Verrouiller les artefacts");
        if (scope.all_locked()) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.36f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.45f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.50f, 0.22f, 1.0f));
        }
        ImGui::BeginDisabled(scope.disabled());
        if (ImGui::Button(label, ImVec2(width, 0))) toggle(rune_scope, scope);
        ImGui::EndDisabled();
        if (scope.all_locked()) ImGui::PopStyleColor(3);
        if (scope.disabled() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(rune_scope ? "Aucune rune équipée." : "Aucun artefact équipé.");
    };
    ImGui::BeginGroup();
    button(true, runes);
    button(false, artifacts);
    ImGui::TextColored(color::muted, "Runes %d/%d · Artefacts %d/%d", runes.locked, runes.total,
                       artifacts.locked, artifacts.total);
    ImGui::EndGroup();
}

void App::render_monster_tab(WorkspaceTab& tab) {
    const int master_id = tab.stored ? tab.unit_master_id : tab.target.unit_master_id;
    const auto* reference = data_.reference.find(master_id);
    const std::string name = monster_name(master_id);

    // En-tête (à gauche ; le panneau de verrous est aligné à droite).
    const float header_y = ImGui::GetCursorPosY();
    ImGui::BeginGroup();
    ImGui::Text("%s   |   %s   |   %s", name.c_str(),
                reference ? element_label(reference->element) : "-",
                reference ? archetype_label(reference->archetype) : "-");
    if (tab.stored) {
        ImGui::EndGroup();
        ImGui::TextColored(color::warning, "Monstre stocké, non optimisable   |   quantité %d",
                           tab.quantity);
        if (!reference || !reference->has_stats()) {
            ImGui::TextColored(color::muted, "Stats de base indisponibles (SWARFARM).");
            return;
        }
        // Stats de base uniquement (SWARFARM, 6* niveau 40).
        if (ImGui::BeginTable("##stored_stats", 2,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            const std::array<int, 8> base{*reference->max_lvl_hp,  *reference->max_lvl_attack,
                                          *reference->max_lvl_defense, reference->speed,
                                          reference->crit_rate,     reference->crit_damage,
                                          reference->resistance,    reference->accuracy};
            for (std::size_t i = 0; i < 8; ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(swcore::text::stat_label(i));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(group_digits(base[i]).c_str());
            }
            ImGui::EndTable();
        }
        return;
    }

    // Fiche "en l'état" : état effectif courant (équipement virtuel compris).
    if (const auto* current = find_monster(tab.target.unit_id)) tab.target = *current;
    const auto& monster = tab.target;
    const auto changes = swcore::pending_changes(data_.equip_log.actions);
    if (const auto it = changes.find(monster.unit_id); it != changes.end())
        ImGui::TextColored(color::warning,
                           "Équipement virtuel : %d changement(s) non appliqués en jeu",
                           it->second);
    ImGui::Text("Niveau %d   |   %d*   |   %s", monster.level, monster.stars,
                awakening_label(reference).c_str());
    ImGui::EndGroup();
    const float left_bottom = ImGui::GetItemRectMax().y;

    // En haut à droite : cadenas global et boutons de verrouillage.
    ImGui::SameLine();
    const float panel_width = 40.0f + ImGui::GetFontSize() * 15.0f;
    const float right = ImGui::GetContentRegionMax().x;
    ImGui::SetCursorPos(ImVec2(std::max(ImGui::GetCursorPosX(), right - panel_width), header_y));
    render_monster_lock_panel(monster);
    const ImVec2 below = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(
        ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x -
                   ImGui::GetScrollX(),
               std::max(below.y, left_bottom + ImGui::GetStyle().ItemSpacing.y)));
    ImGui::Spacing();

    // Même calcul que la ligne du tableau (monster_stats_view).
    const auto view = monster_stats_view(monster);
    ImGui::BeginChild("##monster", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (ImGui::BeginTable("##layout", 2,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("stats", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupColumn("runes", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        StatComparison stats;
        stats.headers = {"Base (sans runes)", "Actuel [+vert]", "Écart"};
        stats.left = view.base;
        stats.right = view.current;
        for (std::size_t i = 0; i < 8; ++i) stats.green[i] = view.current[i] - view.base[i];
        stats.left_metrics = view.base_metrics;
        stats.right_metrics = view.current_metrics;
        stats.right_efficiency = view.efficiency;
        render_stat_comparison(stats);

        ImGui::SeparatorText("Sets actifs");
        const auto sets = active_sets(monster, data_.effects, data_.mapping);
        if (sets.empty()) ImGui::TextColored(color::muted, "Aucun set actif.");
        for (const auto& line : sets) ImGui::TextUnformatted(line.c_str());

        ImGui::SeparatorText("Bonus des runes");
        if (ImGui::BeginTable("##bonuses", 2,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            for (const auto& [label, value] : rune_bonuses(monster.stats)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(group_digits(value).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::TableSetColumnIndex(1);
        render_rune_cards(equipped_views(monster), view.rune_slots, data_.mapping);
        render_artifacts(monster, data_.root(), true);
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

std::vector<std::string> App::selftest_monster_sheets() {
    std::vector<std::string> errors;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1600.0f, 900.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    include_storage_ = false;
    rebuild_monster_rows();
    int without_runes = 0;
    int without_artifacts = 0;
    for (const auto& row : monster_rows_) {
        open_monster_tab(row);
        auto& tab = tabs_.back();
        // La fiche affiche les stats de monster_stats_view : mêmes valeurs que la ligne.
        const auto view = monster_stats_view(tab.target);
        if (view.base != row.base || view.current != row.current ||
            static_cast<long long>(view.current_metrics[0]) != row.current_derived[0] ||
            static_cast<long long>(view.current_metrics[1]) != row.current_derived[1] ||
            view.efficiency != row.efficiency)
            errors.push_back("stats différentes entre la fiche et le tableau : " + row.name);
        without_runes += view.runes == 0 ? 1 : 0;
        without_artifacts += view.artifacts == 0 ? 1 : 0;
        ImGui::NewFrame();
        ImGui::Begin("selftest");
        render_monster_tab(tab);
        ImGui::End();
        ImGui::Render();
    }
    if (without_runes == 0) errors.push_back("aucun monstre sans rune dans l'export de test");
    if (without_artifacts == 0)
        errors.push_back("aucun monstre sans artefact dans l'export de test");
    tabs_.clear();
    ImGui::DestroyContext();
    return errors;
}

std::vector<std::string> App::selftest_pages(const swcore::SolverReport& report,
                                             const swcore::SolverConfig& config) {
    std::vector<std::string> errors;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1600.0f, 900.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    const auto frames = [&](int count) {
        for (int i = 0; i < count; ++i) {
            ImGui::NewFrame();
            render();
            ImGui::Render();
        }
    };

    // Résultats chargés comme après une recherche.
    config_ = config;
    report_config_ = config;
    report_target_ = config.pool.target_unit_id.value_or(0);
    report_ = report;
    build_result_rows();
    std::size_t builds = 0;
    double best = -1;
    for (const auto& scenario : report.scenarios) {
        builds += scenario.all_builds_mode ? scenario.all_builds.size() : scenario.builds.size();
        if (!scenario.builds.empty()) best = std::max(best, scenario.builds.front().score);
    }
    if (rows_.size() != builds || view_order_.size() != builds)
        errors.push_back("tableau de résultats : nombre de lignes incorrect");

    // Chaque page du menu, puis l'Optimiseur (tri initial : Score décroissant).
    for (const Page entry : {Page::Data, Page::Monsters, Page::Optimizer, Page::SavedBuilds,
                             Page::EquipLocks}) {
        show_page(entry);
        frames(3);
        if (!view_.page_active(static_cast<int>(entry)))
            errors.push_back("page non affichée après un clic sur le menu");
    }
    show_page(Page::Optimizer);
    frames(2);
    if (view_order_.empty() || rows_[view_order_.front()].score != best)
        errors.push_back("tableau de résultats : la première ligne n'est pas le meilleur score");

    // Onglet de build (non enregistré) : actif, puis fermé -> retour à la page.
    const auto* target = find_monster(report_target_);
    const auto scenario = std::find_if(report.scenarios.begin(), report.scenarios.end(),
                                       [](const auto& s) { return !s.builds.empty(); });
    if (target && scenario != report.scenarios.end()) {
        open_build_tab(*target, scenario->builds.front(), config, scenario->name, false);
        frames(3);
        WorkspaceTab* tab = active_tab();
        if (!tab || tab->kind != WorkspaceTab::Kind::Build)
            errors.push_back("l'onglet de build ouvert n'est pas actif");
        show_page(Page::Monsters);  // le menu affiche la page, l'onglet reste ouvert
        frames(2);
        if (active_tab() || tabs_.empty())
            errors.push_back("clic sur le menu : l'onglet doit rester ouvert mais inactif");
        if (!tabs_.empty()) {
            activate_tab(tabs_.back().uid);
            tabs_.back().open = false;
            view_.tab_closed(tabs_.back().uid);
            frames(2);
            if (!view_.page_active(static_cast<int>(Page::Monsters)) || !tabs_.empty())
                errors.push_back("fermer l'onglet actif doit revenir à la dernière page");
        }
    } else {
        errors.push_back("aucun build à ouvrir dans un onglet");
    }
    tabs_.clear();
    report_.reset();
    build_result_rows();
    ImGui::DestroyContext();
    return errors;
}

}  // namespace swgui
