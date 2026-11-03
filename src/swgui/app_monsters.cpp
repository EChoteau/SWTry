// Page "Monstres" : tableau des monstres possédés (et stockés en option).

#include "app.hpp"
#include "gui_common.hpp"
#include "gui_components.hpp"
#include "monster_view.hpp"
#include "text_util.hpp"

#include "swcore/build_text.hpp"
#include "swcore/equip.hpp"
#include "swcore/locks.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <numeric>

namespace swgui {

namespace {

enum MonsterColumn {
    kName, kLock, kLevel, kStars, kElement, kArchetype,
    kHp, kAttack, kDefense, kSpeed, kCritRate, kCritDamage, kResistance, kAccuracy,
    kDamage, kEffectiveHp, kEfficiency, kRunes, kArtifacts, kAction, kColumnCount
};

const char* const kElementFilters[] = {"Tous les éléments", "Feu", "Eau", "Vent",
                                       "Lumière", "Ténèbres"};
const swcore::Element kElementValues[] = {swcore::Element::Unknown, swcore::Element::Fire,
                                          swcore::Element::Water, swcore::Element::Wind,
                                          swcore::Element::Light, swcore::Element::Dark};
const char* const kArchetypeFilters[] = {"Tous les archétypes", "Attaque", "Défense",
                                         "PV", "Support", "Matériau"};
const char* const kArchetypeValues[] = {"", "Attack", "Defense", "HP", "Support",
                                        "Material"};
const char* const kStarFilters[] = {"Toutes étoiles", "1*", "2*", "3*", "4*", "5*", "6*"};
const char* const kRuneFilters[] = {"Avec ou sans runes", "Avec runes", "Sans runes"};
const char* const kLockFilters[] = {"Tous les verrous", "Verrouillés", "Partiels",
                                    "Sans verrou"};

}  // namespace

void App::rebuild_monster_rows() {
    monster_rows_.clear();
    for (const auto& monster : data_.monsters) {
        MonsterRow row;
        const auto* reference = data_.reference.find(monster.unit_master_id);
        row.unit_id = monster.unit_id;
        row.monster = &monster;
        row.unit_master_id = monster.unit_master_id;
        row.name = monster_name(monster.unit_master_id);
        row.folded_name = fold(row.name);
        row.level = monster.level;
        row.stars = monster.stars;
        if (reference) {
            row.element = reference->element;
            row.archetype = reference->archetype;
        }
        // Même calcul que la fiche du monstre (monster_stats_view).
        const auto view = monster_stats_view(monster);
        row.base = view.base;
        row.current = view.current;
        row.base_derived = {static_cast<long long>(view.base_metrics[0]),
                            static_cast<long long>(view.base_metrics[1])};
        row.current_derived = {static_cast<long long>(view.current_metrics[0]),
                               static_cast<long long>(view.current_metrics[1])};
        row.efficiency = view.efficiency;
        row.runes = view.runes;
        row.artifacts = view.artifacts;
        monster_rows_.push_back(std::move(row));
    }
    if (include_storage_) {
        for (const auto& stack : data_.storage.stacks()) {
            MonsterRow row;
            const auto* reference = data_.reference.find(stack.unit_master_id);
            row.unit_master_id = stack.unit_master_id;
            row.name = monster_name(stack.unit_master_id);
            row.folded_name = fold(row.name);
            row.stars = stack.stars;
            if (reference) {
                row.element = reference->element;
                row.archetype = reference->archetype;
            }
            row.stored = true;
            row.quantity = stack.quantity;
            monster_rows_.push_back(std::move(row));
        }
    }
    monster_order_.resize(monster_rows_.size());
    std::iota(monster_order_.begin(), monster_order_.end(), std::size_t{0});
    monster_generation_ = data_.generation;
    monster_storage_rows_ = include_storage_;
    monster_sort_dirty_ = true;
}

void App::sort_monster_rows(const ImGuiTableSortSpecs* specs) {
    if (!specs || specs->SpecsCount == 0) {
        std::iota(monster_order_.begin(), monster_order_.end(), std::size_t{0});
        return;
    }
    const bool base = base_stats_;
    const auto lock_rank = [&](const MonsterRow& row) {
        return row.monster ? static_cast<int>(swcore::monster_lock_state(data_.locks, *row.monster))
                           : -1;
    };
    const auto compare_column = [&](const MonsterRow& a, const MonsterRow& b,
                                    int column) -> int {
        const auto cmp = [](const auto& x, const auto& y) { return x < y ? -1 : y < x ? 1 : 0; };
        const auto& sa = base ? a.base : a.current;
        const auto& sb = base ? b.base : b.current;
        const auto& da = base ? a.base_derived : a.current_derived;
        const auto& db = base ? b.base_derived : b.current_derived;
        switch (column) {
        case kName: return cmp(a.folded_name, b.folded_name);
        case kLock: return cmp(lock_rank(a), lock_rank(b));
        case kLevel: return cmp(a.level, b.level);
        case kStars: return cmp(a.stars, b.stars);
        case kElement:
            return cmp(std::string(element_label(a.element)),
                       std::string(element_label(b.element)));
        case kArchetype:
            return cmp(std::string(archetype_label(a.archetype)),
                       std::string(archetype_label(b.archetype)));
        case kDamage: return cmp(da[0], db[0]);
        case kEffectiveHp: return cmp(da[1], db[1]);
        case kEfficiency: return cmp(a.efficiency.value_or(-1), b.efficiency.value_or(-1));
        case kRunes: return cmp(a.runes, b.runes);
        case kArtifacts: return cmp(a.artifacts, b.artifacts);
        default:
            if (column >= kHp && column <= kAccuracy)
                return cmp(sa[static_cast<std::size_t>(column - kHp)],
                           sb[static_cast<std::size_t>(column - kHp)]);
            return 0;
        }
    };
    std::iota(monster_order_.begin(), monster_order_.end(), std::size_t{0});
    std::stable_sort(monster_order_.begin(), monster_order_.end(),
                     [&](std::size_t left, std::size_t right) {
                         for (int i = 0; i < specs->SpecsCount; ++i) {
                             const auto& spec = specs->Specs[i];
                             const int result = compare_column(
                                 monster_rows_[left], monster_rows_[right],
                                 spec.ColumnUserID);
                             if (result != 0)
                                 return spec.SortDirection == ImGuiSortDirection_Ascending
                                            ? result < 0
                                            : result > 0;
                         }
                         return false;
                     });
}

void App::render_monsters_list() {
    if (monster_generation_ != data_.generation || monster_storage_rows_ != include_storage_)
        rebuild_monster_rows();


    // Barre de filtres.
    const float field = ImGui::GetFontSize() * 11.0f;
    ImGui::SetNextItemWidth(field * 1.3f);
    ImGui::InputTextWithHint("##search", "Rechercher un nom", monster_search_,
                             sizeof monster_search_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(field);
    ImGui::Combo("##element", &element_filter_, kElementFilters, IM_ARRAYSIZE(kElementFilters));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(field);
    ImGui::Combo("##archetype", &archetype_filter_, kArchetypeFilters,
                 IM_ARRAYSIZE(kArchetypeFilters));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(field * 0.8f);
    ImGui::Combo("##stars", &stars_filter_, kStarFilters, IM_ARRAYSIZE(kStarFilters));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(field);
    ImGui::Combo("##runes", &runes_filter_, kRuneFilters, IM_ARRAYSIZE(kRuneFilters));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(field);
    ImGui::Combo("##locks", &lock_filter_, kLockFilters, IM_ARRAYSIZE(kLockFilters));

    int stats_mode = base_stats_ ? 0 : 1;
    if (ImGui::RadioButton("Stats de base", &stats_mode, 0)) monster_sort_dirty_ = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Stats actuelles", &stats_mode, 1)) monster_sort_dirty_ = true;
    base_stats_ = stats_mode == 0;
    ImGui::SameLine(0, ImGui::GetFontSize() * 2.0f);
    ImGui::BeginDisabled(!data_.storage_file.loaded);
    ImGui::Checkbox("Inclure les monstres stockés (hôtel scellé)", &include_storage_);
    ImGui::EndDisabled();

    const auto pending = swcore::pending_changes(data_.equip_log.actions);

    // Lignes visibles (ordre trié puis filtres).
    const std::string search = fold(monster_search_);
    std::vector<std::size_t> visible;
    visible.reserve(monster_order_.size());
    for (const auto index : monster_order_) {
        const auto& row = monster_rows_[index];
        if (!search.empty() && row.folded_name.find(search) == std::string::npos) continue;
        if (element_filter_ > 0 && row.element != kElementValues[element_filter_]) continue;
        if (archetype_filter_ > 0 && row.archetype != kArchetypeValues[archetype_filter_])
            continue;
        if (stars_filter_ > 0 && row.stars != stars_filter_) continue;
        if (runes_filter_ == 1 && row.runes == 0) continue;
        if (runes_filter_ == 2 && row.runes > 0) continue;
        if (lock_filter_ > 0) {
            const auto state = row.monster
                                   ? swcore::monster_lock_state(data_.locks, *row.monster)
                                   : swcore::LockState::None;
            if (lock_filter_ == 1 && state != swcore::LockState::Full) continue;
            if (lock_filter_ == 2 && state != swcore::LockState::Partial) continue;
            if (lock_filter_ == 3 && state != swcore::LockState::None) continue;
        }
        visible.push_back(index);
    }
    ImGui::TextColored(color::muted, "%s monstres affichés",
                       group_digits(static_cast<long long>(visible.size())).c_str());

    // Sélection multiple (Ctrl / Maj + clic) et verrouillage en lot.
    const auto is_selected = [&](std::uint64_t unit_id) {
        return std::find(selected_monsters_.begin(), selected_monsters_.end(), unit_id) !=
               selected_monsters_.end();
    };
    ImGui::SameLine(0, ImGui::GetFontSize() * 2.0f);
    ImGui::BeginDisabled(selected_monsters_.empty());
    const std::string count = std::to_string(selected_monsters_.size());
    const auto apply_to_selection = [&](bool lock) {
        for (const auto unit_id : selected_monsters_)
            if (const auto* monster = find_monster(unit_id))
                set_monster_locks(*monster, lock, true, true);
    };
    if (ImGui::SmallButton(("Verrouiller (" + count + ")").c_str())) apply_to_selection(true);
    ImGui::SameLine();
    if (ImGui::SmallButton(("Déverrouiller (" + count + ")").c_str())) apply_to_selection(false);
    ImGui::SameLine();
    if (ImGui::SmallButton("Vider la sélection")) selected_monsters_.clear();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(color::muted, "Ctrl / Maj + clic : sélection multiple");

    const ImGuiTableFlags flags =
        ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##monsters", kColumnCount, flags)) return;
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Nom", ImGuiTableColumnFlags_DefaultSort |
                                       ImGuiTableColumnFlags_NoHide,
                            0.0f, kName);
    ImGui::TableSetupColumn("Verrou", 0, 0.0f, kLock);
    ImGui::TableSetupColumn("Niv.", 0, 0.0f, kLevel);
    ImGui::TableSetupColumn("Étoiles", 0, 0.0f, kStars);
    ImGui::TableSetupColumn("Élément", 0, 0.0f, kElement);
    ImGui::TableSetupColumn("Archétype", 0, 0.0f, kArchetype);
    for (int i = 0; i < 8; ++i)
        ImGui::TableSetupColumn(kMetrics[i].label, ImGuiTableColumnFlags_PreferSortDescending,
                                0.0f, static_cast<ImGuiID>(kHp + i));
    ImGui::TableSetupColumn("DMG", ImGuiTableColumnFlags_PreferSortDescending, 0.0f, kDamage);
    ImGui::TableSetupColumn("PV eff.", ImGuiTableColumnFlags_PreferSortDescending, 0.0f,
                            kEffectiveHp);
    ImGui::TableSetupColumn("Efficacité", ImGuiTableColumnFlags_PreferSortDescending, 0.0f,
                            kEfficiency);
    ImGui::TableSetupColumn("Runes", 0, 0.0f, kRunes);
    ImGui::TableSetupColumn("Artefacts", 0, 0.0f, kArtifacts);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoHide,
                            0.0f, kAction);
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
        if (specs->SpecsDirty || monster_sort_dirty_) {
            sort_monster_rows(specs);
            specs->SpecsDirty = false;
            monster_sort_dirty_ = false;
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& row = monster_rows_[visible[static_cast<std::size_t>(i)]];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            const auto& stats = base_stats_ ? row.base : row.current;
            const auto& metrics = base_stats_ ? row.base_derived : row.current_derived;
            ImGui::TableSetColumnIndex(kName);
            // Toute la ligne est cliquable (le bouton Optimiser reste à part).
            // Marqueur : équipement modifié virtuellement (pas encore fait en jeu).
            const auto changes = row.stored ? pending.end() : pending.find(row.unit_id);
            const std::string label =
                row.stored ? row.name + " (stocké x" + std::to_string(row.quantity) + ")"
                : changes != pending.end()
                    ? row.name + "  [virtuel : " + std::to_string(changes->second) + "]"
                    : row.name;
            const bool selected = !row.stored && is_selected(row.unit_id);
            if (ImGui::Selectable(label.c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap)) {
                const ImGuiIO& io = ImGui::GetIO();
                if (row.stored) {
                    open_monster_tab(row);
                } else if (io.KeyCtrl) {
                    if (selected)
                        selected_monsters_.erase(std::remove(selected_monsters_.begin(),
                                                             selected_monsters_.end(),
                                                             row.unit_id),
                                                 selected_monsters_.end());
                    else
                        selected_monsters_.push_back(row.unit_id);
                    last_clicked_row_ = i;
                } else if (io.KeyShift && last_clicked_row_ >= 0) {
                    const int from = std::min(last_clicked_row_, i);
                    const int to = std::max(last_clicked_row_, i);
                    for (int k = from; k <= to && k < static_cast<int>(visible.size()); ++k) {
                        const auto& other = monster_rows_[visible[static_cast<std::size_t>(k)]];
                        if (!other.stored && !is_selected(other.unit_id))
                            selected_monsters_.push_back(other.unit_id);
                    }
                } else {
                    selected_monsters_ = {row.unit_id};
                    last_clicked_row_ = i;
                    open_monster_tab(row);
                }
            }
            // Cadenas : clic = tout verrouiller (sinon tout déverrouiller) ; clic droit : menu.
            ImGui::TableSetColumnIndex(kLock);
            if (row.monster) {
                const auto state = swcore::monster_lock_state(data_.locks, *row.monster);
                const char* tooltip = state == swcore::LockState::Full
                                          ? "Runes et artefacts verrouillés (clic : déverrouiller)"
                                      : state == swcore::LockState::Partial
                                          ? "Verrouillage partiel (clic : tout verrouiller)"
                                          : "Aucun verrou (clic : tout verrouiller)";
                if (lock_button("##lock", state, tooltip))
                    set_monster_locks(*row.monster, state != swcore::LockState::Full, true,
                                      true);
                if (ImGui::BeginPopupContextItem("##lock_menu")) {
                    if (ImGui::MenuItem("Verrouiller les runes"))
                        set_monster_locks(*row.monster, true, true, false);
                    if (ImGui::MenuItem("Verrouiller les artefacts"))
                        set_monster_locks(*row.monster, true, false, true);
                    if (ImGui::MenuItem("Tout verrouiller"))
                        set_monster_locks(*row.monster, true, true, true);
                    if (ImGui::MenuItem("Tout déverrouiller"))
                        set_monster_locks(*row.monster, false, true, true);
                    ImGui::EndPopup();
                }
            }
            ImGui::TableSetColumnIndex(kLevel);
            if (!row.stored) ImGui::Text("%d", row.level);
            ImGui::TableSetColumnIndex(kStars);
            ImGui::Text("%d*", row.stars);
            ImGui::TableSetColumnIndex(kElement);
            ImGui::TextUnformatted(element_label(row.element));
            ImGui::TableSetColumnIndex(kArchetype);
            ImGui::TextUnformatted(archetype_label(row.archetype));
            for (int s = 0; s < 8; ++s) {
                ImGui::TableSetColumnIndex(kHp + s);
                if (!row.stored)
                    ImGui::TextUnformatted(
                        group_digits(stats[static_cast<std::size_t>(s)]).c_str());
            }
            if (!row.stored) {
                ImGui::TableSetColumnIndex(kDamage);
                ImGui::TextUnformatted(group_digits(metrics[0]).c_str());
                ImGui::TableSetColumnIndex(kEffectiveHp);
                ImGui::TextUnformatted(group_digits(metrics[1]).c_str());
                ImGui::TableSetColumnIndex(kEfficiency);
                if (row.efficiency) ImGui::Text("%.2f %%", *row.efficiency);
                else ImGui::TextColored(color::muted, "-");
                ImGui::TableSetColumnIndex(kRunes);
                ImGui::Text("%d/6", row.runes);
                ImGui::TableSetColumnIndex(kArtifacts);
                ImGui::Text("%d/2", row.artifacts);
                ImGui::TableSetColumnIndex(kAction);
                if (ImGui::SmallButton("Optimiser")) {
                    config_.pool.target_unit_id = row.unit_id;
                    page_ = Page::Optimizer;
                    select_fixed_ = FixedTab::Search;
                    set_status("Monstre sélectionné pour l'optimisation : " + row.name);
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

}  // namespace swgui
