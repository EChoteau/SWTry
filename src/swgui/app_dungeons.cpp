// Page "Donjons" (étape 1, sans calcul) : liste des donjons par catégorie ->
// étages -> vagues et teams proposées -> écran team (config, possession,
// script, presets, hypothèses non vérifiées de mechanics.json).

#include "app.hpp"
#include "gui_common.hpp"
#include "text_util.hpp"

#include "swcore/build_text.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace swgui {

namespace {

const char* const kUnavailable = "données indisponibles (pas de vagues dans SWARFARM)";

std::size_t team_count(const DataStore::DungeonState& state, int dungeon, int level = 0) {
    return static_cast<std::size_t>(std::count_if(
        state.teams.begin(), state.teams.end(), [&](const swcore::TeamConfig& team) {
            return team.dungeon_id == dungeon && (level == 0 || team.matches(dungeon, level));
        }));
}

std::string level_label(const swcore::DungeonLevel& level) {
    std::string label = "Étage " + std::to_string(level.floor);
    if (level.difficulty) label += " (" + *level.difficulty + ")";
    return label;
}

const char* role_label(const std::string& role) {
    if (role == "speed_anchor") return "ancre de vitesse";
    if (role == "support") return "support";
    if (role == "nuker") return "nuker";
    return role.empty() ? "-" : role.c_str();
}

}  // namespace

void App::render_dungeons_page() {
    data_.start_dungeon_load();
    const auto* state = data_.dungeons();
    if (!state) {
        ImGui::TextColored(color::muted, "Chargement des donjons...");
        return;
    }
    if (state->data.empty()) {
        ImGui::TextColored(color::error,
                           "Aucun donjon chargé : data/swarfarm/dungeons.json est absent ou "
                           "illisible (lancer swsync).");
        return;
    }

    // Fil d'Ariane : Donjons > donjon > étage > team (chaque niveau cliquable).
    const auto* dungeon = dungeon_id_ ? state->data.dungeon(dungeon_id_) : nullptr;
    const auto* level = dungeon && dungeon_level_id_ ? state->data.level(dungeon_level_id_) : nullptr;
    const swcore::TeamConfig* team = nullptr;
    if (level)
        for (const auto& candidate : state->teams)
            if (candidate.id == dungeon_team_id_) team = &candidate;
    if (ImGui::TextLink("Donjons")) {
        dungeon_id_ = 0;
        dungeon_level_id_ = 0;
        dungeon_team_id_.clear();
    }
    if (dungeon) {
        ImGui::SameLine();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        if (ImGui::TextLink(dungeon->name.c_str())) {
            dungeon_level_id_ = 0;
            dungeon_team_id_.clear();
        }
    }
    if (level) {
        ImGui::SameLine();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        if (ImGui::TextLink(level_label(*level).c_str())) dungeon_team_id_.clear();
    }
    if (team) {
        ImGui::SameLine();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        ImGui::TextUnformatted(team->name.c_str());
    }
    ImGui::Separator();

    ImGui::BeginChild("##dungeon_view", ImVec2(0, 0));
    if (team) render_dungeon_team(*state, *team);
    else if (level) render_dungeon_level(*state, *dungeon, *level);
    else if (dungeon) render_dungeon_levels(*state, *dungeon);
    else render_dungeon_list(*state);
    ImGui::EndChild();
}

void App::render_dungeon_list(const DataStore::DungeonState& state) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    ImGui::InputTextWithHint("##dungeon_filter", "Rechercher un donjon", dungeon_filter_,
                             sizeof dungeon_filter_);
    ImGui::SameLine();
    ImGui::TextColored(color::muted, "%zu team(s) configurée(s) dans data/teams",
                       state.teams.size());
    const std::string filter = fold(dungeon_filter_);
    for (const auto& category : state.data.categories()) {
        std::vector<const swcore::Dungeon*> entries;
        for (const auto& dungeon : state.data.dungeons())
            if (dungeon.category == category &&
                (filter.empty() || fold(dungeon.name).find(filter) != std::string::npos))
                entries.push_back(&dungeon);
        if (entries.empty()) continue;
        const std::string header = category + " (" + std::to_string(entries.size()) + ")###" +
                                   category;
        // Ouvert par défaut : Cairos ; ouvert aussi pendant une recherche.
        ImGui::SetNextItemOpen(!filter.empty() || category == "Cairos Dungeon",
                               filter.empty() ? ImGuiCond_Once : ImGuiCond_Always);
        if (!ImGui::CollapsingHeader(header.c_str())) continue;
        ImGui::Indent();
        for (const auto* dungeon : entries) {
            const bool available = state.data.has_wave_data(*dungeon);
            const std::size_t teams = team_count(state, dungeon->id);
            std::string label = dungeon->name;
            if (teams > 0) label += "   [" + std::to_string(teams) + " team(s)]";
            label += "##dungeon" + std::to_string(dungeon->id);
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(label.c_str())) {
                dungeon_id_ = dungeon->id;
                dungeon_level_id_ = 0;
                dungeon_team_id_.clear();
            }
            ImGui::EndDisabled();
            if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", kUnavailable);
        }
        ImGui::Unindent();
    }
}

void App::render_dungeon_levels(const DataStore::DungeonState& state,
                                const swcore::Dungeon& dungeon) {
    ImGui::Text("%s   |   %s", dungeon.name.c_str(), dungeon.category.c_str());
    if (!ImGui::BeginTable("##levels", 4,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                               ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Étage");
    ImGui::TableSetupColumn("Vagues");
    ImGui::TableSetupColumn("Ennemis");
    ImGui::TableSetupColumn("Teams");
    ImGui::TableHeadersRow();
    for (const int id : dungeon.level_ids) {
        const auto* level = state.data.level(id);
        ImGui::PushID(id);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (!level) {
            ImGui::TextColored(color::muted, "Étage %d : absent de levels.json", id);
            ImGui::PopID();
            continue;
        }
        const bool available = level->has_waves();
        ImGui::BeginDisabled(!available);
        if (ImGui::Selectable(level_label(*level).c_str(), false,
                              ImGuiSelectableFlags_SpanAllColumns)) {
            dungeon_level_id_ = id;
            dungeon_team_id_.clear();
        }
        ImGui::EndDisabled();
        if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", kUnavailable);
        ImGui::TableSetColumnIndex(1);
        if (available) ImGui::Text("%zu", level->waves.size());
        else ImGui::TextColored(color::muted, "indisponible");
        ImGui::TableSetColumnIndex(2);
        std::size_t enemies = 0;
        for (const auto& wave : level->waves) enemies += wave.enemies.size();
        if (available) ImGui::Text("%zu", enemies);
        ImGui::TableSetColumnIndex(3);
        if (const auto teams = team_count(state, dungeon.id, id); teams > 0)
            ImGui::Text("%zu", teams);
        else
            ImGui::TextColored(color::muted, "-");
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void App::render_dungeon_level(const DataStore::DungeonState& state,
                               const swcore::Dungeon& dungeon,
                               const swcore::DungeonLevel& level) {
    ImGui::Text("%s - %s", dungeon.name.c_str(), level_label(level).c_str());

    // Teams proposées pour cet étage, avec la possession dans la box.
    ImGui::SeparatorText("Teams proposées");
    bool any = false;
    for (const auto& team : state.teams) {
        if (!team.matches(dungeon.id, level.id)) continue;
        any = true;
        ImGui::PushID(team.id.c_str());
        if (ImGui::Selectable(team.name.c_str(), false, 0,
                              ImVec2(ImGui::GetFontSize() * 18.0f, 0)))
            dungeon_team_id_ = team.id;
        ImGui::SameLine();
        if (!data_.export_file.loaded) {
            ImGui::TextColored(color::muted, "box inconnue (aucun export chargé)");
        } else {
            const auto ownership = swcore::team_ownership(team, data_.reference, data_.monsters);
            if (ownership.complete()) {
                ImGui::TextColored(color::positive, "possédée complètement");
            } else {
                std::string missing;
                for (const auto& name : ownership.missing)
                    missing += (missing.empty() ? "" : ", ") + name;
                ImGui::TextColored(color::warning, "manque : %s", missing.c_str());
            }
        }
        if (!team.problems.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(color::error, "(%zu problème(s) dans la config)",
                               team.problems.size());
        }
        ImGui::PopID();
    }
    if (!any)
        ImGui::TextColored(color::muted,
                           "Aucune team pour cet étage (ajouter un fichier dans data/teams).");

    // Vagues et ennemis (données SWARFARM, compétences corrigées par les overrides).
    for (std::size_t w = 0; w < level.waves.size(); ++w) {
        const auto& wave = level.waves[w];
        const std::string title = "Vague " + std::to_string(w + 1) +
                                  (w + 1 == level.waves.size() ? " (boss)" : "");
        ImGui::SeparatorText(title.c_str());
        ImGui::PushID(static_cast<int>(w));
        if (ImGui::BeginTable("##enemies", 10,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingFixedFit)) {
            for (const char* header : {"Ennemi", "Élément", "Niv.", "PV", "ATQ", "DEF", "VIT",
                                       "RES", "Préc. bonus", "Compétences"})
                ImGui::TableSetupColumn(header);
            ImGui::TableHeadersRow();
            for (std::size_t e = 0; e < wave.enemies.size(); ++e) {
                const auto& enemy = wave.enemies[e];
                ImGui::PushID(static_cast<int>(e));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(enemy.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(enemy.known ? element_label(enemy.element) : "-");
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", enemy.level);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(group_digits(enemy.hp).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(group_digits(enemy.attack).c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(group_digits(enemy.defense).c_str());
                ImGui::TableSetColumnIndex(6);
                ImGui::Text("%d", enemy.speed);
                ImGui::TableSetColumnIndex(7);
                ImGui::Text("%d", enemy.resist);
                ImGui::TableSetColumnIndex(8);
                ImGui::Text("%d", enemy.accuracy_bonus);
                ImGui::TableSetColumnIndex(9);
                for (std::size_t s = 0; s < enemy.skill_ids.size(); ++s) {
                    const auto* skill = state.data.skill(enemy.skill_ids[s]);
                    if (s) ImGui::SameLine();
                    if (!skill) {
                        ImGui::TextColored(color::muted, "#%d", enemy.skill_ids[s]);
                        continue;
                    }
                    const std::string label = "S" + std::to_string(skill->slot) + " " +
                                              skill->name + (skill->overridden ? " *" : "");
                    if (skill->overridden) ImGui::TextColored(color::accent, "%s", label.c_str());
                    else ImGui::TextUnformatted(label.c_str());
                    if (ImGui::IsItemHovered()) {
                        std::string tip = skill->name;
                        tip += "\nmultiplicateur : " + (skill->multiplier_formula.empty()
                                                            ? std::string{"aucun"}
                                                            : skill->multiplier_formula);
                        tip += "\nrecharge : " + (skill->cooltime
                                                      ? std::to_string(*skill->cooltime)
                                                      : std::string{"-"});
                        tip += std::string("\nzone : ") + (skill->aoe ? "oui" : "non");
                        if (skill->passive) tip += "\npassive";
                        for (const auto& effect : skill->effects)
                            tip += "\neffet : " + effect.name + " (" +
                                   swcore::text::format_number(effect.chance) + " %)";
                        if (skill->overridden)
                            tip += "\ncorrigée par skills_overrides.json" +
                                   (skill->override_note.empty() ? std::string{}
                                                                 : " : " + skill->override_note);
                        ImGui::SetTooltip("%s", tip.c_str());
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
}

void App::render_dungeon_team(const DataStore::DungeonState& state,
                              const swcore::TeamConfig& team) {
    ImGui::Text("%s", team.name.c_str());
    ImGui::TextColored(color::muted, "Fichier : %s", to_utf8(team.file.filename()).c_str());
    if (!team.notes.empty()) ImGui::TextWrapped("%s", team.notes.c_str());
    for (const auto& problem : team.problems)
        ImGui::TextColored(color::error, "Config : %s", problem.c_str());

    // Monstres (ordre de runage = priorité) et possession dans la box.
    ImGui::SeparatorText("Monstres");
    std::vector<const swcore::TeamMonster*> ordered;
    for (const auto& monster : team.monsters) ordered.push_back(&monster);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto* a, const auto* b) { return a->priority < b->priority; });
    if (ImGui::BeginTable("##team_monsters", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingFixedFit)) {
        for (const char* header :
             {"Priorité", "Monstre", "SWARFARM", "Rôle", "Dans la box", "Compétences"})
            ImGui::TableSetupColumn(header);
        ImGui::TableHeadersRow();
        for (const auto* monster : ordered) {
            const auto* known = data_.reference.find_swarfarm(monster->swarfarm_id);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", monster->priority);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s%s", monster->name.c_str(),
                        monster->name == team.leader ? "  (leader)" : "");
            ImGui::TableSetColumnIndex(2);
            if (known) ImGui::Text("%s (%s)", known->name.c_str(), element_label(known->element));
            else ImGui::TextColored(color::error, "id %d inconnu", monster->swarfarm_id);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(role_label(monster->role));
            ImGui::TableSetColumnIndex(4);
            if (!data_.export_file.loaded) {
                ImGui::TextColored(color::muted, "export non chargé");
            } else {
                const int owned = known ? static_cast<int>(std::count_if(
                                              data_.monsters.begin(), data_.monsters.end(),
                                              [&](const swcore::AccountMonster& unit) {
                                                  return unit.unit_master_id == known->com2us_id;
                                              }))
                                        : 0;
                if (owned > 0) ImGui::TextColored(color::positive, "oui (%d)", owned);
                else ImGui::TextColored(color::warning, "non");
            }
            ImGui::TableSetColumnIndex(5);
            const std::vector<int>& skills =
                !monster->skill_overrides.empty()
                    ? monster->skill_overrides
                    : (known ? known->skills : monster->skill_overrides);
            std::string text;
            for (const int id : skills) {
                const auto* skill = state.data.skill(id);
                text += (text.empty() ? "" : " | ") +
                        (skill ? "S" + std::to_string(skill->slot) + " " + skill->name
                               : "#" + std::to_string(id));
            }
            if (!monster->skill_overrides.empty()) text += "  (imposées par la config)";
            ImGui::TextUnformatted(text.empty() ? "-" : text.c_str());
        }
        ImGui::EndTable();
    }

    // Script contre le boss (tel qu'écrit dans la config).
    ImGui::SeparatorText("Script contre le boss");
    for (std::size_t i = 0; i < team.boss_script.size(); ++i) {
        const auto& step = team.boss_script[i];
        std::string line = std::to_string(i + 1) + ". " + step.actor + " S" +
                           std::to_string(step.skill);
        if (step.fallback_skill) line += " (sinon S" + std::to_string(*step.fallback_skill) + ")";
        if (step.target == "previous_actor") line += " -> allié qui vient de jouer";
        else if (!step.target.empty()) line += " -> " + step.target;
        for (const auto& effect : step.effects) line += "  [" + effect + "]";
        ImGui::TextUnformatted(line.c_str());
    }
    if (team.boss_script.empty()) ImGui::TextColored(color::muted, "Aucun script.");

    // Sets conseillés (set 1 / set 2 de la config).
    ImGui::SeparatorText("Sets conseillés");
    for (const auto& [preset, by_monster] : team.rune_presets) {
        if (by_monster.empty()) {
            ImGui::TextColored(color::muted, "%s : vide", preset.c_str());
            continue;
        }
        ImGui::Text("%s :", preset.c_str());
        for (const auto& [monster, sets] : by_monster) {
            std::string text;
            for (const auto& set : sets) text += (text.empty() ? "" : " + ") + set;
            ImGui::BulletText("%s : %s", monster.c_str(), text.c_str());
        }
    }

    // Hypothèses de mechanics.json encore inconnues (null).
    ImGui::SeparatorText("Mécaniques (data/mechanics.json)");
    if (!state.mechanics.loaded()) {
        ImGui::TextColored(color::error, "mechanics.json absent ou illisible.");
    } else {
        const auto unknown = state.mechanics.unverified();
        if (unknown.empty())
            ImGui::TextColored(color::muted, "Aucune hypothèse inconnue.");
        for (const auto& key : unknown)
            ImGui::TextColored(color::warning, "Hypothèse non vérifiée : %s", key.c_str());
    }
}

}  // namespace swgui
