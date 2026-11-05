// Équipement virtuel et verrous côté GUI : boutons de l'onglet de build, retour
// d'action (toast), page "Équipement et verrous". Toute la logique est dans
// swcore (equip, locks) ; rien ne verrouille ni n'équipe sans clic explicite.

#include "app.hpp"
#include "widgets/gui_common.hpp"
#include "widgets/text_util.hpp"

#include "swcore/solver/build_text.hpp"
#include "swcore/account/equip.hpp"
#include "swcore/account/locks.hpp"

#include <imgui.h>

#include <algorithm>
#include <ctime>
#include <sstream>

namespace swgui {

namespace {

std::string now_text() {
    const std::time_t time = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M", &local);
    return buffer;
}

const char* scope_label(swcore::EquipScope scope) {
    switch (scope) {
    case swcore::EquipScope::Runes: return "runes";
    case swcore::EquipScope::Artifacts: return "artefacts";
    case swcore::EquipScope::Both: break;
    }
    return "runes et artefacts";
}

// Bouton scindé : bouton principal + petite flèche qui ouvre un menu.
bool split_button(const char* label, const char* arrow_id, const char* popup_id,
                  bool disabled) {
    ImGui::BeginDisabled(disabled);
    const bool clicked = ImGui::Button(label);
    ImGui::EndDisabled();
    ImGui::SameLine(0, 1.0f);
    if (ImGui::ArrowButton(arrow_id, ImGuiDir_Down)) ImGui::OpenPopup(popup_id);
    return clicked;
}

}  // namespace

void App::show_toast(std::string text, std::function<void()> undo) {
    toast_ = Toast{std::move(text), std::move(undo), ImGui::GetTime() + 10.0};
    set_status(toast_->text);
}

void App::render_toast() {
    ImGui::TextColored(color::accent, "%s", toast_->text.c_str());
    if (toast_->undo) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Annuler")) {
            auto undo = std::move(toast_->undo);
            toast_.reset();
            undo();
            set_status("Action annulée.");
        }
    }
}

void App::render_build_actions(WorkspaceTab& tab) {
    // Monstre cible dans l'état effectif courant (sinon la copie de l'onglet).
    const auto* current = find_monster(tab.target.unit_id);
    const swcore::AccountMonster& target = current ? *current : tab.target;
    const std::string name = monster_name(target.unit_master_id);

    // --- Équiper (vue du logiciel ; ne touche pas aux verrous) ---
    const bool runes_done = swcore::build_equipped(data_.state, target.unit_id, tab.build,
                                                   swcore::EquipScope::Runes);
    const bool artifacts_change =
        !swcore::make_equip_action(data_.state, target.unit_id, tab.build,
                                   swcore::EquipScope::Artifacts)
             .moves.empty();
    const auto equip = [&](swcore::EquipScope scope) {
        auto action = swcore::make_equip_action(data_.state, target.unit_id, tab.build, scope);
        if (action.moves.empty()) {
            show_toast("Rien à équiper : le build est déjà en place.");
            return;
        }
        action.date = now_text();
        int locked_elsewhere = 0;
        for (const auto& move : action.moves) {
            if (move.to != target.unit_id) continue;
            const auto* lock = data_.locks.find(move.kind, move.id);
            if (lock && lock->reserved_for != target.unit_id) ++locked_elsewhere;
        }
        const auto moves = action.moves.size();
        data_.apply_equip(std::move(action));
        std::string text = "Équipé sur " + name + " (" + scope_label(scope) + ") : " +
                           std::to_string(moves) + " déplacement(s) virtuel(s)";
        if (locked_elsewhere > 0)
            text += " ; " + std::to_string(locked_elsewhere) +
                    " objet(s) verrouillé(s) pour un autre monstre déplacé(s), verrous "
                    "inchangés";
        show_toast(text, [this] { data_.undo_equip(); });
    };
    const bool equipped = runes_done && !artifacts_change;
    if (split_button(equipped ? "Équipé" : "Équiper", "##equip_more", "##equip_menu",
                     equipped))
        equip(swcore::EquipScope::Both);
    if (ImGui::BeginPopup("##equip_menu")) {
        if (ImGui::MenuItem("Équiper runes et artefacts", nullptr, false, !equipped))
            equip(swcore::EquipScope::Both);
        if (ImGui::MenuItem("Équiper uniquement les runes", nullptr, false, !runes_done))
            equip(swcore::EquipScope::Runes);
        if (ImGui::MenuItem("Équiper uniquement les artefacts", nullptr, false,
                            artifacts_change))
            equip(swcore::EquipScope::Artifacts);
        if (!artifacts_change && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Le build ne change aucun artefact (le solveur ne les "
                              "optimise pas encore).");
        ImGui::EndPopup();
    }

    // --- Verrouiller / Déverrouiller pour le monstre CIBLE ---
    ImGui::SameLine();
    const auto lock_scope = [&](bool runes, bool artifacts) {
        const swcore::LockTable before = data_.locks;
        std::string text;
        if (swcore::build_locked_for(data_.locks, target, tab.build, runes, artifacts)) {
            swcore::unlock_build(data_.locks, target, tab.build, runes, artifacts);
            text = "Déverrouillé pour " + name;
        } else {
            const auto result =
                swcore::lock_build(data_.locks, target, tab.build, name, runes, artifacts);
            text = "Verrouillé pour " + name + " : " + std::to_string(result.locked) +
                   " objet(s)";
            if (!result.released.empty()) {
                text += " ; ancien(s) verrou(s) libéré(s) :";
                for (const auto& lock : result.released)
                    text += lock.kind == swcore::LockKind::Rune
                                ? " " + swcore::text::set_name(data_.mapping, lock.set_id) +
                                      " slot " + std::to_string(lock.slot)
                                : " artefact slot " + std::to_string(lock.slot);
            }
        }
        locks_changed();
        show_toast(text, [this, before] {
            data_.locks = before;
            locks_changed();
        });
    };
    const auto lock_label = [&](bool runes, bool artifacts, const char* what) {
        return std::string(swcore::build_locked_for(data_.locks, target, tab.build, runes,
                                                    artifacts)
                               ? "Déverrouiller "
                               : "Verrouiller ") +
               what;
    };
    const bool all_locked =
        swcore::build_locked_for(data_.locks, target, tab.build, true, true);
    if (split_button(all_locked ? "Déverrouiller" : "Verrouiller", "##lock_more",
                     "##lock_menu", false))
        lock_scope(true, true);
    if (ImGui::BeginPopup("##lock_menu")) {
        if (ImGui::MenuItem(lock_label(true, true, "runes et artefacts").c_str()))
            lock_scope(true, true);
        if (ImGui::MenuItem(lock_label(true, false, "uniquement les runes").c_str()))
            lock_scope(true, false);
        if (ImGui::MenuItem(lock_label(false, true, "uniquement les artefacts").c_str()))
            lock_scope(false, true);
        ImGui::EndPopup();
    }
}

std::string App::equip_todo_text() const {
    using namespace swcore::text;
    std::ostringstream out;
    const auto describe = [&](std::uint64_t id) {
        for (const auto& rune : data_.exported.runes)
            if (rune.rune_id == id)
                return "Rune " + std::to_string(id) + " (" + set_name(data_.mapping, rune.set_id) +
                       " slot " + std::to_string(rune.slot) + " +" +
                       std::to_string(rune.upgrade_level) + ")";
        return "Rune " + std::to_string(id);
    };
    for (const auto& action : data_.equip_log.actions) {
        const std::string target = owner_name(action.target);
        out << "Action " << action.id << " (" << action.date << ") : équiper " << target
            << " (" << scope_label(action.scope) << ")\n";
        for (const auto& move : action.moves) {
            if (move.kind == swcore::LockKind::Artifact) {
                out << "  Artefact " << move.id << " (slot " << move.slot
                    << ") : " << owner_name(move.from) << " -> " << owner_name(move.to)
                    << '\n';
            } else if (move.to == 0 && move.from == action.target) {
                out << "  Ancienne rune de " << target << " : " << describe(move.id)
                    << " -> inventaire\n";
            } else {
                out << "  " << describe(move.id) << " : " << owner_name(move.from) << " -> "
                    << owner_name(move.to) << '\n';
            }
        }
        for (const auto& task : action.craft) {
            for (const auto& grind : task.grinds)
                out << "  Meule (" << rarity_label(grind.rarity) << ") : rune " << task.rune_id
                    << " slot " << task.slot << ", " << stat_name(data_.mapping, grind.stat_id)
                    << " +" << grind.previous << " -> +" << grind.value << '\n';
            for (const auto& gem : task.gems)
                out << "  Gemme (" << rarity_label(gem.rarity) << ") : rune " << task.rune_id
                    << " slot " << task.slot << ", " << stat_name(data_.mapping, gem.old_stat_id)
                    << ' ' << gem.old_value << " remplacée par "
                    << stat_name(data_.mapping, gem.stat_id) << ' ' << gem.value << '\n';
        }
    }
    return out.str();
}

void App::render_equip_locks_page() {
    ImGui::TextUnformatted("Équipement et verrous");
    ImGui::Separator();

    // --- À faire en jeu ---
    ImGui::SeparatorText("À faire en jeu");
    const auto& actions = data_.equip_log.actions;
    if (actions.empty()) {
        ImGui::TextColored(color::muted, "Aucun équipement virtuel en attente.");
    } else {
        if (ImGui::Button("Copier la liste")) ImGui::SetClipboardText(equip_todo_text().c_str());
        ImGui::SameLine();
        if (ImGui::Button("Annuler la dernière action")) {
            data_.undo_equip();
            set_status("Dernière action d'équipement annulée.");
        }
        ImGui::SameLine();
        if (!confirm_reset_equip_) {
            if (ImGui::Button("Réinitialiser")) confirm_reset_equip_ = true;
        } else {
            ImGui::TextColored(color::warning, "Abandonner tout l'équipement virtuel ?");
            ImGui::SameLine();
            if (ImGui::Button("Confirmer")) {
                data_.reset_equip();
                confirm_reset_equip_ = false;
                set_status("Équipement virtuel réinitialisé.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Non")) confirm_reset_equip_ = false;
        }
        ImGui::BeginChild("##todo", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.45f),
                          ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(equip_todo_text().c_str());
        ImGui::EndChild();
    }

    // --- Verrous ---
    ImGui::SeparatorText("Verrous");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    ImGui::InputTextWithHint("##lock_search", "Filtrer (id, monstre)", lock_filter_text_,
                             sizeof lock_filter_text_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    ImGui::Combo("##lock_kind", &lock_kind_filter_, "Runes et artefacts\0Runes\0Artefacts\0");
    ImGui::SameLine();
    ImGui::BeginDisabled(selected_locks_.empty());
    if (ImGui::Button(("Déverrouiller la sélection (" + std::to_string(selected_locks_.size()) +
                       ")")
                          .c_str())) {
        for (const auto id : selected_locks_) {
            data_.locks.remove(swcore::LockKind::Rune, id);
            data_.locks.remove(swcore::LockKind::Artifact, id);
        }
        selected_locks_.clear();
        locks_changed();
    }
    ImGui::EndDisabled();

    // Emplacement actuel (état effectif).
    const auto location = [&](const swcore::Lock& lock) -> std::string {
        if (lock.kind == swcore::LockKind::Rune) {
            for (const auto& rune : data_.runes)
                if (rune.rune_id == lock.id) return owner_name(swcore::rune_owner(rune));
            return "absente";
        }
        for (const auto& monster : data_.monsters)
            for (const auto& artifact : monster.artifacts)
                if (artifact.rid == lock.id) return owner_name(monster.unit_id);
        return "inventaire";
    };
    const std::string filter = fold(lock_filter_text_);
    std::vector<const swcore::Lock*> rows;
    for (const auto& lock : data_.locks.locks()) {
        if (lock_kind_filter_ == 1 && lock.kind != swcore::LockKind::Rune) continue;
        if (lock_kind_filter_ == 2 && lock.kind != swcore::LockKind::Artifact) continue;
        if (!filter.empty() && std::to_string(lock.id).find(filter) == std::string::npos &&
            fold(lock.monster_name).find(filter) == std::string::npos)
            continue;
        rows.push_back(&lock);
    }
    enum { kSelect, kKind, kId, kSet, kSlot, kReserved, kWhere, kAction };
    if (!ImGui::BeginTable("##locks", 8,
                           ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                               ImGuiTableFlags_BordersInnerV))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort, 0.0f, kSelect);
    ImGui::TableSetupColumn("Type", 0, 0.0f, kKind);
    ImGui::TableSetupColumn("Id", 0, 0.0f, kId);
    ImGui::TableSetupColumn("Set", 0, 0.0f, kSet);
    ImGui::TableSetupColumn("Slot", 0, 0.0f, kSlot);
    ImGui::TableSetupColumn("Réservé pour", ImGuiTableColumnFlags_DefaultSort, 0.0f, kReserved);
    ImGui::TableSetupColumn("Emplacement actuel", 0, 0.0f, kWhere);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort, 0.0f, kAction);
    ImGui::TableHeadersRow();
    if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
        specs && specs->SpecsCount > 0) {
        const auto& spec = specs->Specs[0];
        const auto key = [&](const swcore::Lock* lock) -> std::string {
            switch (spec.ColumnUserID) {
            case kKind: return lock->kind == swcore::LockKind::Rune ? "rune" : "artefact";
            case kId: return std::string(20 - std::to_string(lock->id).size(), '0') +
                             std::to_string(lock->id);
            case kSet: return swcore::text::set_name(data_.mapping, lock->set_id);
            case kSlot: return std::to_string(lock->slot);
            case kWhere: return fold(location(*lock));
            default: return fold(lock->reserved_for ? lock->monster_name : std::string{});
            }
        };
        std::stable_sort(rows.begin(), rows.end(), [&](const auto* a, const auto* b) {
            return spec.SortDirection == ImGuiSortDirection_Ascending ? key(a) < key(b)
                                                                      : key(b) < key(a);
        });
    }
    std::optional<swcore::Lock> unlock;
    for (const auto* lock : rows) {
        ImGui::PushID(static_cast<int>(lock->id % 2147483647));
        ImGui::PushID(static_cast<int>(lock->kind));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(kSelect);
        bool selected = std::find(selected_locks_.begin(), selected_locks_.end(), lock->id) !=
                        selected_locks_.end();
        if (ImGui::Checkbox("##sel", &selected)) {
            if (selected) selected_locks_.push_back(lock->id);
            else
                selected_locks_.erase(
                    std::remove(selected_locks_.begin(), selected_locks_.end(), lock->id),
                    selected_locks_.end());
        }
        ImGui::TableSetColumnIndex(kKind);
        ImGui::TextUnformatted(lock->kind == swcore::LockKind::Rune ? "Rune" : "Artefact");
        ImGui::TableSetColumnIndex(kId);
        ImGui::TextUnformatted(std::to_string(lock->id).c_str());
        ImGui::TableSetColumnIndex(kSet);
        if (lock->kind == swcore::LockKind::Rune)
            ImGui::TextUnformatted(swcore::text::set_name(data_.mapping, lock->set_id).c_str());
        ImGui::TableSetColumnIndex(kSlot);
        ImGui::Text("%d", lock->slot);
        ImGui::TableSetColumnIndex(kReserved);
        ImGui::TextUnformatted(lock->reserved_for ? lock->monster_name.c_str() : "personne");
        ImGui::TableSetColumnIndex(kWhere);
        ImGui::TextUnformatted(location(*lock).c_str());
        ImGui::TableSetColumnIndex(kAction);
        if (ImGui::SmallButton("Déverrouiller")) unlock = *lock;
        ImGui::PopID();
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (unlock) {
        data_.locks.remove(unlock->kind, unlock->id);
        locks_changed();
    }
}

}  // namespace swgui
