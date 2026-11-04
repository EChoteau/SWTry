// Page "Donjons" (étape 1, sans calcul) : liste des donjons par catégorie ->
// étages -> vagues et teams proposées -> écran team (config, possession,
// script, presets, hypothèses non vérifiées de mechanics.json).

#include "app.hpp"
#include "gui_common.hpp"
#include "text_util.hpp"

#include "swcore/build_text.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
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

    // Réglages (sets, verrous, meules / gemmes) et optimisation de team.
    render_team_settings(team);
    render_team_optimization(state, team);

    // Monstres, VIT de combat, fenêtres, taux critique, ordre des tours, verdict.
    render_team_analysis(state, team);

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

void App::render_team_analysis(const DataStore::DungeonState& state,
                               const swcore::TeamConfig& team) {
    const auto* dungeon = state.data.dungeon(dungeon_id_);
    const auto* level = state.data.level(dungeon_level_id_);
    if (!level) return;

    // Recalcul seulement si une entrée change (team, étage, box, VIT saisies).
    std::map<std::string, int> manual;
    const std::string prefix = team.id + "/";
    for (const auto& [key, speed] : dungeon_manual_speeds_)
        if (key.rfind(prefix, 0) == 0) manual[key.substr(prefix.size())] = speed;
    // Box analysée : l'état actuel, ou la box hypothétique de l'optimisation.
    const swcore::TeamOptimizerResult* optimized =
        team_show_optimized_ && team_opt_ && !team_opt_->running && team_opt_->result &&
                team_opt_->team_id == team.id && team_opt_->level_id == level->id
            ? &*team_opt_->result
            : nullptr;
    std::string key = team.id + "#" + std::to_string(level->id) + "#" +
                      std::to_string(data_.generation) + "#" +
                      (data_.export_file.loaded ? "1" : "0") + "#" +
                      (optimized ? "opt" + std::to_string(team_opt_revision_)
                                 : std::string{"actuel"});
    for (const auto& [name, speed] : manual) key += "#" + name + "=" + std::to_string(speed);
    if (!team_analysis_ || key != team_analysis_key_) {
        swcore::TeamAnalysisInput input;
        input.team = &team;
        input.data = &state.data;
        input.dungeon = dungeon;
        input.level = level;
        input.reference = &data_.reference;
        input.box = optimized ? &optimized->box
                              : (data_.export_file.loaded ? &data_.monsters : nullptr);
        if (optimized) input.pinned_units = optimized->units;
        input.mechanics = &state.mechanics;
        input.manual_speeds = manual;
        team_analysis_ = swcore::analyze_team(input);
        team_analysis_key_ = key;
    }
    const auto& analysis = *team_analysis_;
    if (optimized)
        ImGui::TextColored(color::accent, "Analyse de la team après optimisation (builds proposés, "
                                          "pas encore équipés).");
    for (const auto& note : analysis.notes) ImGui::TextColored(color::muted, "%s", note.c_str());
    if (!analysis.valid) return;

    const auto decimal = [](double value) {  // une décimale, virgule : "123,4"
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.1f", value);
        std::string text = buffer;
        std::replace(text.begin(), text.end(), '.', ',');
        return text;
    };

    // --- Monstres : stats actuelles, VIT de combat, fenêtre, taux critique ---
    ImGui::SeparatorText("Monstres");
    if (!data_.export_file.loaded)
        ImGui::TextColored(color::muted,
                           "Aucun export chargé : VIT de base SWARFARM (saisir les VIT à la main).");
    std::vector<std::size_t> ordered(analysis.members.size());
    for (std::size_t i = 0; i < ordered.size(); ++i) ordered[i] = i;
    std::stable_sort(ordered.begin(), ordered.end(), [&](std::size_t a, std::size_t b) {
        return analysis.members[a].config->priority < analysis.members[b].config->priority;
    });
    if (ImGui::BeginTable("##team_members", 9,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingFixedFit)) {
        for (const char* header : {"Monstre", "Rôle", "Box", "PV", "ATQ", "Dgts crit.",
                                   "VIT de combat", "Fenêtre de VIT", "CR des runes"})
            ImGui::TableSetupColumn(header);
        ImGui::TableHeadersRow();
        for (const auto index : ordered) {
            const auto& member = analysis.members[index];
            const auto& name = member.config->name;
            ImGui::PushID(name.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s%s", name.c_str(), name == team.leader ? "  (leader)" : "");
            if (member.reference && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s (%s)", member.reference->name.c_str(),
                                  element_label(member.element));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(role_label(member.config->role));
            ImGui::TableSetColumnIndex(2);
            if (!data_.export_file.loaded) ImGui::TextColored(color::muted, "-");
            else if (member.unit) ImGui::TextColored(color::positive, "oui");
            else ImGui::TextColored(color::warning, "non");
            ImGui::TableSetColumnIndex(3);
            if (member.unit) ImGui::TextUnformatted(group_digits(member.unit->stats.total.hp).c_str());
            ImGui::TableSetColumnIndex(4);
            if (member.unit)
                ImGui::TextUnformatted(group_digits(member.unit->stats.total.attack).c_str());
            ImGui::TableSetColumnIndex(5);
            if (member.unit) ImGui::Text("%d %%", member.unit->stats.total.crit_damage);

            // VIT de combat : calculée, ou saisie à la main (pour tester).
            ImGui::TableSetColumnIndex(6);
            const auto& speed = member.speed;
            int value = speed.combat;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.5f);
            ImGui::InputInt("##speed", &value, 0, 0);
            if (ImGui::IsItemDeactivatedAfterEdit() && value != speed.combat)
                dungeon_manual_speeds_[prefix + name] = std::max(1, value);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Base %d, totale %d (runes et sets), leader %s %%, tour %s %%%s",
                                  speed.base, speed.total,
                                  swcore::text::format_number(speed.leader_percent).c_str(),
                                  swcore::text::format_number(speed.tower_percent).c_str(),
                                  speed.manual ? "\nValeur saisie à la main" : "");
            if (speed.manual) {
                ImGui::SameLine();
                if (ImGui::SmallButton("auto")) dungeon_manual_speeds_.erase(prefix + name);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Revenir à la VIT calculée");
            }

            // Fenêtre de VIT (les autres fixes).
            ImGui::TableSetColumnIndex(7);
            const auto& window = member.window;
            if (!window.found) {
                ImGui::TextColored(color::error, "aucune");
            } else {
                std::string text = window.lower_open && window.upper_open ? "toutes"
                                   : window.upper_open ? ">= " + std::to_string(window.min)
                                   : window.lower_open ? "<= " + std::to_string(window.max)
                                                       : std::to_string(window.min) + " – " +
                                                             std::to_string(window.max);
                ImGui::TextColored(window.current_inside ? color::positive : color::error, "%s",
                                   text.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(window.current_inside
                                          ? "VIT actuelle dans la fenêtre"
                                          : "VIT actuelle hors de la fenêtre");
            }

            // Taux critique : CR des runes actuel / requis.
            ImGui::TableSetColumnIndex(8);
            const auto& crit = member.crit;
            if (!crit.needed) {
                ImGui::TextColored(color::muted, "pas de contrainte");
            } else {
                const bool ok = member.unit && crit.current_runes >= crit.required_runes;
                if (member.unit)
                    ImGui::TextColored(ok ? color::positive : color::error, "%d / %d %%",
                                       crit.current_runes, crit.required_runes);
                else
                    ImGui::Text("requis %d %%", crit.required_runes);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Requis = 100 - base %d - leader %d - avantage élémentaire %d",
                                      crit.base, crit.leader, crit.advantage);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!analysis.relative.empty()) {
        ImGui::TextUnformatted("Contraintes relatives détectées :");
        for (const auto& line : analysis.relative) ImGui::BulletText("%s", line.c_str());
    }

    // --- Ordre des tours ---
    ImGui::SeparatorText("Ordre des tours (jusqu'au premier tour du boss)");
    const auto& atb = analysis.atb;
    const std::size_t steps = analysis.script.size();
    if (atb.script_before_boss)
        ImGui::TextColored(color::positive, "Script complet avant le premier tour du boss (%zu/%zu).",
                           atb.steps_done_before_boss, steps);
    else
        ImGui::TextColored(color::error, "Actions du script avant le premier tour du boss : %zu/%zu.",
                           atb.steps_done_before_boss, steps);
    for (const auto& warning : atb.warnings)
        ImGui::TextColored(color::warning, "%s", warning.c_str());
    for (std::size_t t = 0; t < atb.turns.size(); ++t) {
        const auto& turn = atb.turns[t];
        const auto& unit = analysis.units[turn.actor];
        std::string line = "t" + std::to_string(turn.tick) + "  " + unit.name + " (" +
                           decimal(turn.bar) + ")";
        if (turn.step >= 0) {
            const auto& step = team.boss_script[static_cast<std::size_t>(turn.step)];
            line += "  S" + std::to_string(step.skill) + "  [action " +
                    std::to_string(turn.step + 1) + "]";
        } else if (unit.ally) {
            line += "  tour en plus";
        }
        if (turn.tie) line += "  (égalité)";
        const bool boss_turn = atb.boss_turn && t == *atb.boss_turn;
        if (boss_turn) ImGui::TextColored(color::error, "%s  <- premier tour du boss", line.c_str());
        else if (!unit.ally) ImGui::TextColored(color::muted, "%s", line.c_str());
        else ImGui::TextUnformatted(line.c_str());
    }

    // Tableau tic par tic : barre de chaque unité après l'incrément du tic ;
    // la case de l'unité qui joue est surlignée.
    if (ImGui::TreeNode("Tableau tic par tic")) {
        const int last_tick = atb.turns.empty() ? 0 : atb.turns.back().tick;
        const int columns = static_cast<int>(analysis.units.size()) + 1;
        if (ImGui::BeginTable("##ticks", columns,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 22.0f))) {
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableSetupColumn("Tic");
            for (const auto& unit : analysis.units) ImGui::TableSetupColumn(unit.name.c_str());
            ImGui::TableHeadersRow();
            for (int tick = 1; tick <= last_tick && tick <= static_cast<int>(atb.bars.size());
                 ++tick) {
                std::optional<std::size_t> actor;
                for (const auto& turn : atb.turns)
                    if (turn.tick == tick) actor = turn.actor;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", tick);
                const auto& bars = atb.bars[static_cast<std::size_t>(tick - 1)];
                for (std::size_t u = 0; u < bars.size(); ++u) {
                    ImGui::TableSetColumnIndex(static_cast<int>(u) + 1);
                    if (actor && *actor == u) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                               ImGui::GetColorU32(ImVec4(0.40f, 0.78f, 0.88f, 0.35f)));
                        ImGui::TextUnformatted(decimal(bars[u]).c_str());
                    } else if (bars[u] >= 100.0) {
                        ImGui::TextColored(color::warning, "%s", decimal(bars[u]).c_str());
                    } else {
                        ImGui::TextColored(color::muted, "%s", decimal(bars[u]).c_str());
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    // --- Verdict : pire / meilleur cas ---
    ImGui::SeparatorText("Verdict");
    const auto verdict = [&](const char* title, const swcore::CaseOutcome& outcome) {
        ImGui::PushID(title);
        if (outcome.kills)
            ImGui::TextColored(color::positive, "%s : boss mort avant son tour.", title);
        else
            ImGui::TextColored(color::error,
                               "%s : boss vivant, %s PV restants (%s %%).", title,
                               group_digits(static_cast<long long>(outcome.remaining)).c_str(),
                               decimal(outcome.remaining_percent).c_str());
        ImGui::SameLine();
        ImGui::TextColored(color::muted, "dégâts %s / %s PV",
                           group_digits(static_cast<long long>(outcome.damage)).c_str(),
                           group_digits(static_cast<long long>(outcome.boss_hp)).c_str());
        if (outcome.counter) ImGui::TextColored(color::warning, "%s", outcome.counter->c_str());
        if (ImGui::TreeNode("Détail des actions")) {
            if (ImGui::BeginTable("##actions", 5,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_SizingFixedFit)) {
                for (const char* header : {"Tic", "Action", "Dégâts", "Conditions", "Note"})
                    ImGui::TableSetupColumn(header);
                ImGui::TableHeadersRow();
                for (const auto& action : outcome.actions) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("t%d", action.tick);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s S%d%s", action.actor.c_str(), action.skill,
                                action.fallback ? " (repli)" : action.extra_turn ? " (tour en plus)" : "");
                    ImGui::TableSetColumnIndex(2);
                    if (action.damage > 0)
                        ImGui::TextUnformatted(
                            group_digits(static_cast<long long>(action.damage)).c_str());
                    else
                        ImGui::TextColored(color::muted, "-");
                    ImGui::TableSetColumnIndex(3);
                    std::string conditions;
                    if (action.damage > 0) conditions += action.crit ? "critique" : "non critique";
                    if (action.def_broken) conditions += ", DEF brisée";
                    if (action.atk_buff) conditions += ", ATQ boostée";
                    ImGui::TextUnformatted(conditions.c_str());
                    ImGui::TableSetColumnIndex(4);
                    std::string note = action.note;
                    if (action.kills) note += note.empty() ? "coup fatal" : ", coup fatal";
                    ImGui::TextUnformatted(note.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    verdict("Pire cas", analysis.worst);
    ImGui::TextColored(color::muted,
                       "Pire cas : replis de compétences, effets aléatoires ratés, hypothèses "
                       "inconnues = 0.");
    verdict("Meilleur cas", analysis.best);
    ImGui::TextColored(color::muted, "Meilleur cas : compétences principales, effets aléatoires "
                                     "réussis, tous les coups critiques.");
    ImGui::Text("Score de team (pire cas, coups critiques supposés) : %s",
                group_digits(static_cast<long long>(analysis.score)).c_str());

    // Ce qui manque (pire cas).
    if (analysis.missing.empty()) {
        ImGui::TextColored(color::positive, "Rien ne manque au pire cas.");
    } else {
        ImGui::TextUnformatted("Ce qui manque (pire cas) :");
        for (const auto& line : analysis.missing) ImGui::BulletText("%s", line.c_str());
    }

    // Survie au premier coup du boss.
    if (!analysis.survival.empty()) {
        ImGui::SeparatorText(("Survie au premier coup du boss (" + analysis.boss_skill + ")").c_str());
        if (!analysis.survival_matters)
            ImGui::TextColored(color::warning,
                               "Personne ne survit même sans critique : l'objectif est de tuer "
                               "le boss avant son tour (la survie n'est pas une contrainte).");
        if (analysis.worst.attack_tower_before_boss)
            ImGui::TextColored(color::muted, "La tour d'ATQ joue avant le boss : ATQ du boss boostée.");
        if (analysis.worst.defense_tower_before_boss)
            ImGui::TextColored(color::muted, "La tour de DEF joue avant le boss : DEF de la team "
                                             "réduite.");
        if (ImGui::BeginTable("##survival", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingFixedFit)) {
            for (const char* header : {"Monstre", "PV", "Coup normal", "Coup critique"})
                ImGui::TableSetupColumn(header);
            ImGui::TableHeadersRow();
            for (const auto& line : analysis.survival) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(line.member.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(group_digits(line.hp).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(line.survives ? color::positive : color::error, "%s",
                                   group_digits(static_cast<long long>(line.hit)).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(line.survives_crit ? color::positive : color::error, "%s",
                                   group_digits(static_cast<long long>(line.crit_hit)).c_str());
            }
            ImGui::EndTable();
        }
    }

    // Hypothèses non vérifiées réellement utilisées par le calcul.
    for (const auto& key : analysis.unverified)
        ImGui::TextColored(color::warning, "Hypothèse non vérifiée utilisée (pire cas : 0) : %s",
                           key.c_str());
}

}  // namespace swgui
