// Barre d'onglets commune aux pages Monstres et Optimiseur : onglets fixes
// (liste des monstres, recherche) puis onglets fermables (fiches de monstre,
// builds). La fiche d'un monstre réutilise les composants des onglets de build.

#include "app.hpp"
#include "gui_common.hpp"
#include "gui_components.hpp"
#include "monster_view.hpp"
#include "text_util.hpp"

#include "swcore/build_text.hpp"
#include "swcore/equip.hpp"

#include <imgui.h>

#include <algorithm>
#include <map>
#include <sstream>

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

std::string ids_text(const swcore::AccountMonster& monster) {
    std::string text;
    for (const auto& view : equipped_views(monster))
        if (view.rune.rune_id != 0)
            text += (text.empty() ? "" : " ") + std::to_string(view.rune.rune_id);
    return text;
}

}  // namespace

void App::render_workspace() {
    if (!data_.export_file.loaded) {
        ImGui::TextColored(color::muted, "Chargez un export dans la page Données.");
        return;
    }
    if (ImGui::BeginTabBar("##workspace", ImGuiTabBarFlags_Reorderable |
                                              ImGuiTabBarFlags_FittingPolicyScroll)) {
        const auto fixed_flags = [&](FixedTab tab) -> ImGuiTabItemFlags {
            ImGuiTabItemFlags flags = ImGuiTabItemFlags_NoReorder;
            if (select_fixed_ == tab) {
                flags |= ImGuiTabItemFlags_SetSelected;
                select_fixed_ = FixedTab::None;
            }
            return flags;
        };
        if (ImGui::BeginTabItem("Monstres", nullptr, fixed_flags(FixedTab::Monsters))) {
            page_ = Page::Monsters;
            render_monsters_list();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Recherche", nullptr, fixed_flags(FixedTab::Search))) {
            page_ = Page::Optimizer;
            render_search_tab();
            ImGui::EndTabItem();
        }
        for (auto& tab : tabs_) {
            const std::string label = tab.title + "##tab" + std::to_string(tab.uid);
            const ImGuiTabItemFlags flags = tab.focus ? ImGuiTabItemFlags_SetSelected : 0;
            tab.focus = false;
            if (ImGui::BeginTabItem(label.c_str(), &tab.open, flags)) {
                if (tab.kind == WorkspaceTab::Kind::Monster) render_monster_tab(tab);
                else render_build_tab(tab);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
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
            tab.focus = true;
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
    tab.text = monster_sheet_text(tab);
    tabs_.push_back(std::move(tab));
}

void App::render_monster_tab(WorkspaceTab& tab) {
    const int master_id = tab.stored ? tab.unit_master_id : tab.target.unit_master_id;
    const auto* reference = data_.reference.find(master_id);
    const std::string name = monster_name(master_id);

    // En-tête.
    ImGui::Text("%s   |   %s   |   %s", name.c_str(),
                reference ? element_label(reference->element) : "-",
                reference ? archetype_label(reference->archetype) : "-");
    if (tab.stored) {
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
        if (ImGui::Button("Copier en texte")) ImGui::SetClipboardText(tab.text.c_str());
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
    if (ImGui::Button("Optimiser ce monstre")) {
        config_.pool.target_unit_id = monster.unit_id;
        page_ = Page::Optimizer;
        select_fixed_ = FixedTab::Search;
        set_status("Monstre sélectionné pour l'optimisation : " + name);
    }
    ImGui::SameLine();
    if (ImGui::Button("Copier en texte"))
        ImGui::SetClipboardText(monster_sheet_text(tab).c_str());  // état courant
    ImGui::SameLine();
    if (ImGui::Button("Copier les ids de runes"))
        ImGui::SetClipboardText(ids_text(monster).c_str());
    ImGui::SameLine();
    if (ImGui::Button("Tout verrouiller")) set_monster_locks(monster, true, true, true);
    ImGui::SameLine();
    if (ImGui::Button("Tout déverrouiller")) set_monster_locks(monster, false, true, true);

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
        const auto views = equipped_views(monster);
        if (const int slot = render_rune_cards(views, view.rune_slots, data_.mapping,
                                               &data_.locks, monster.unit_id))
            toggle_rune_lock(views[static_cast<std::size_t>(slot - 1)].rune);
        if (const auto rid = render_artifacts(monster, data_.root(), true, &data_.locks))
            for (const auto& artifact : monster.artifacts)
                if (artifact.rid == rid) toggle_artifact_lock(artifact);
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

std::string App::monster_sheet_text(const WorkspaceTab& tab) const {
    using namespace swcore::text;
    const int master_id = tab.stored ? tab.unit_master_id : tab.target.unit_master_id;
    const auto* reference = data_.reference.find(master_id);
    std::ostringstream out;
    out << monster_name(master_id) << " ("
        << (reference ? element_label(reference->element) : "-") << ", "
        << (reference ? archetype_label(reference->archetype) : "-") << ")\n";
    if (tab.stored) {
        out << "Monstre stocké, non optimisable - quantité " << tab.quantity << '\n';
        return out.str();
    }
    const auto& monster = tab.target;
    out << "Niveau " << monster.level << " - " << monster.stars << "* - "
        << awakening_label(reference) << "\n\n";
    const auto view = monster_stats_view(monster);
    constexpr std::size_t c1 = 20, c2 = 20, c3 = 24, c4 = 12;
    out << pad_right("Stat", c1) << pad_left("Base (sans runes)", c2)
        << pad_left("Actuel [+vert]", c3) << pad_left("Écart", c4) << '\n'
        << std::string(c1 + c2 + c3 + c4, '-') << '\n';
    for (std::size_t i = 0; i < 8; ++i) {
        const int green = view.current[i] - view.base[i];
        out << pad_right(stat_label(i), c1) << pad_left(group_digits(view.base[i]), c2)
            << pad_left(group_digits(view.current[i]) + " [" + signed_group(green) + "]", c3)
            << pad_left(signed_group(green), c4) << '\n';
    }
    const char* labels[] = {"DMG", "PV eff.", "PV eff. DEF brisée"};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto base = static_cast<long long>(view.base_metrics[i]);
        const auto current = static_cast<long long>(view.current_metrics[i]);
        out << pad_right(labels[i], c1) << pad_left(group_digits(base), c2)
            << pad_left(group_digits(current), c3) << pad_left(signed_group(current - base), c4)
            << '\n';
    }
    out << pad_right("Efficacité %", c1) << pad_left("-", c2)
        << pad_left(view.efficiency ? fixed2(*view.efficiency) : "-", c3) << '\n';
    const auto sets = active_sets(monster, data_.effects, data_.mapping);
    out << "\nSets actifs : " << (sets.empty() ? "aucun" : "") << '\n';
    for (const auto& line : sets) out << "  " << line << '\n';
    out << '\n';
    const auto views = equipped_views(monster);
    for (const int slot : {6, 1, 2, 5, 4, 3}) {
        const auto index = static_cast<std::size_t>(slot - 1);
        if (!view.rune_slots[index]) {
            out << "Slot " << slot << " : vide\n";
            continue;
        }
        for (const auto& line : rune_card(views[index], data_.mapping)) out << line << '\n';
    }
    return out.str();
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

}  // namespace swgui
