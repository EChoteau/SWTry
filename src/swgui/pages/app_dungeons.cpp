// Page "Donjons" (étape 1, sans calcul) : liste des donjons par catégorie ->
// étages -> vagues et teams proposées -> écran team (config, possession,
// script, presets, hypothèses non vérifiées de mechanics.json).

#include "app.hpp"
#include "widgets/gui_common.hpp"
#include "widgets/text_util.hpp"

#include "swcore/solver/build_text.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <set>
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

// Ligne d'un trou : "#1234 Nom (passive) - Monstre A, Monstre B".
std::string gap_line(const swcore::SkillData& skill, const swcore::ReferenceDB& reference) {
    std::string line = "#" + std::to_string(skill.id) + " " + skill.name +
                       (skill.passive ? " (passive)" : "");
    std::string monsters;
    for (const int id : skill.used_on)
        if (const auto* monster = reference.find_swarfarm(id))
            if (monsters.find(monster->name) == std::string::npos)
                monsters += (monsters.empty() ? "" : ", ") + monster->name;
    if (!monsters.empty()) line += " - " + monsters;
    return line;
}

// Compétences qui frappent mais restent sans multiplicateur après fusion
// other_skill et overrides : d'abord celles de l'étage et des teams proposées,
// puis la liste complète (repliable).
void render_damage_gaps(const swcore::DungeonData& data, const swcore::ReferenceDB& reference,
                        const std::set<int>& relevant) {
    const auto& gaps = data.damage_gaps();
    ImGui::SeparatorText("Compétences qui frappent sans multiplicateur");
    ImGui::TextColored(color::muted,
                       "Après fusion other_skill (%zu compétence(s) complétée(s)) et overrides. "
                       "À compléter à la main dans data/game/monsters/skills_overrides.json "
                       "(candidats détectés par la description, à vérifier).",
                       data.skills_merged());
    bool any = false;
    for (const int id : gaps) {
        if (!relevant.contains(id)) continue;
        const auto* skill = data.skill(id);
        if (!skill) continue;
        any = true;
        ImGui::TextColored(color::warning, "%s", gap_line(*skill, reference).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", skill->description.c_str());
    }
    if (!any)
        ImGui::TextColored(color::positive,
                           "Aucune pour cet étage ni pour ses teams (ennemis et membres).");
    const std::string all = "Toutes les compétences de monstres concernées (" +
                            std::to_string(gaps.size()) + ")###all_gaps";
    if (ImGui::TreeNode(all.c_str())) {
        for (const int id : gaps)
            if (const auto* skill = data.skill(id)) {
                ImGui::BulletText("%s", gap_line(*skill, reference).c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", skill->description.c_str());
            }
        ImGui::TreePop();
    }
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
                           "Aucune team pour cet étage (ajouter un fichier dans data/teams/<donjon>/).");

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
                        for (const int part : skill->merged_from)
                            tip += "\nmultiplicateur fusionné depuis #" + std::to_string(part) +
                                   " (other_skill)";
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

    // Trous de données : ennemis de l'étage et membres des teams proposées.
    std::set<int> relevant;
    for (const auto& wave : level.waves)
        for (const auto& enemy : wave.enemies)
            relevant.insert(enemy.skill_ids.begin(), enemy.skill_ids.end());
    for (const auto& team : state.teams) {
        if (!team.matches(dungeon.id, level.id)) continue;
        for (const auto& monster : team.monsters) {
            relevant.insert(monster.skill_overrides.begin(), monster.skill_overrides.end());
            if (const auto* reference = data_.reference.find_swarfarm(monster.swarfarm_id))
                relevant.insert(reference->skills.begin(), reference->skills.end());
        }
    }
    render_damage_gaps(state.data, data_.reference, relevant);
}

void App::render_dungeon_team(const DataStore::DungeonState& state,
                              const swcore::TeamConfig& team) {
    ImGui::Text("%s", team.name.c_str());
    ImGui::TextColored(color::muted, "Fichier : %s", to_utf8(team.file.parent_path().filename() / team.file.filename()).c_str());
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
    ImGui::SeparatorText("Mécaniques (data/game/combat/mechanics.json)");
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

namespace {

std::string one_decimal(double value) {  // "123,4"
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", value);
    std::string text = buffer;
    std::replace(text.begin(), text.end(), '.', ',');
    return text;
}

}  // namespace

// Ordre des tours : "t9 Teshar S2 [action 2 du script]" ; hors script, la
// compétence de l'IA auto ; ennemis en gris, premier tour du boss en rouge.
void App::render_turn_order(const swcore::TeamConfig& team, const swcore::TeamAnalysis& analysis,
                            const swcore::AtbResult& atb,
                            const std::vector<swcore::ActionDamage>* actions) {
    for (std::size_t t = 0; t < atb.turns.size(); ++t) {
        const auto& turn = atb.turns[t];
        const auto& unit = analysis.units[turn.actor];
        std::string line = "t" + std::to_string(turn.tick) + "  " + unit.name;
        const swcore::ActionDamage* action = nullptr;
        if (actions)
            for (const auto& candidate : *actions)
                if (!candidate.extra && candidate.tick == turn.tick && candidate.actor == unit.name)
                    action = &candidate;
        if (turn.stunned) {
            line += "  étourdi(e) : tour perdu (" + one_decimal(turn.bar) + ")";
            ImGui::TextColored(color::warning, "%s", line.c_str());
            continue;
        }
        if (unit.ally) {
            if (action) {
                line += " S" + std::to_string(action->skill);
                // "+ Tempest" juste après la compétence ; recharge entre parenthèses.
                if (action->passive.rfind("+ ", 0) == 0) line += " " + action->passive;
                if (action->step >= 0)
                    line += "  [action " + std::to_string(action->step + 1) + " du script]";
                else
                    line += "  [hors script : IA auto]";
                if (action->fallback) line += " (repli)";
                if (!action->passive.empty() && action->passive.rfind("+ ", 0) != 0)
                    line += "  (" + action->passive + ")";
            } else if (turn.step >= 0) {
                line += " S" + std::to_string(team.boss_script[static_cast<std::size_t>(turn.step)].skill) +
                        "  [action " + std::to_string(turn.step + 1) + " du script]";
            } else {
                line += "  [hors script]";
            }
        }
        line += "  (" + one_decimal(turn.bar) + ")";
        const bool boss_turn = atb.boss_turn && t == *atb.boss_turn;
        if (boss_turn) ImGui::TextColored(color::error, "%s  <- premier tour du boss", line.c_str());
        else if (!unit.ally) ImGui::TextColored(color::muted, "%s", line.c_str());
        else ImGui::TextUnformatted(line.c_str());
    }
    // Tableau tic par tic (repliable) : barre de chaque unité après l'incrément.
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
                        ImGui::TextUnformatted(one_decimal(bars[u]).c_str());
                    } else if (bars[u] >= 100.0) {
                        ImGui::TextColored(color::warning, "%s", one_decimal(bars[u]).c_str());
                    } else {
                        ImGui::TextColored(color::muted, "%s", one_decimal(bars[u]).c_str());
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
}

void App::render_team_analysis(const DataStore::DungeonState& state,
                               const swcore::TeamConfig& team) {
    const auto* dungeon = state.data.dungeon(dungeon_id_);
    const auto* level = state.data.level(dungeon_level_id_);
    if (!level) return;
    const auto* result = team_result(team);

    // Entrées communes ; builds = ceux de l'optimisation (simulation pure).
    const auto base_input = [&] {
        swcore::TeamAnalysisInput input;
        input.team = &team;
        input.data = &state.data;
        input.dungeon = dungeon;
        input.level = level;
        input.reference = &data_.reference;
        input.box = data_.export_file.loaded ? &data_.monsters : nullptr;
        input.mechanics = &state.mechanics;
        input.effects = &data_.effects;
        input.towers = &state.towers;
        return input;
    };
    const std::string common = team.id + "#" + std::to_string(level->id) + "#" +
                               std::to_string(data_.generation) + "#" +
                               (data_.export_file.loaded ? "1" : "0");
    const std::string key =
        common + "#" + (result ? "opt" + std::to_string(team_opt_revision_) : std::string{"plan"});
    if (!team_analysis_ || key != team_analysis_key_) {
        auto input = base_input();
        if (result) {
            input.builds = result->builds;
            input.pinned_units = result->units;
        }
        team_analysis_ = swcore::analyze_team(input);
        team_analysis_key_ = key;
    }
    const auto& analysis = *team_analysis_;
    if (!analysis.valid) {
        for (const auto& note : analysis.notes) ImGui::TextColored(color::error, "%s", note.c_str());
        return;
    }
    const bool simulated = result && analysis.all_runes;

    // --- Monstres : stats de combat du build analysé (lecture seule) ---
    ImGui::SeparatorText(simulated ? "Monstres (builds optimisés, non équipés)"
                                   : "Monstres (plan et critères, avant optimisation)");
    if (!data_.export_file.loaded)
        ImGui::TextColored(color::muted, "Aucun export chargé : stats de base SWARFARM.");
    std::vector<std::size_t> ordered(analysis.members.size());
    for (std::size_t i = 0; i < ordered.size(); ++i) ordered[i] = i;
    std::stable_sort(ordered.begin(), ordered.end(), [&](std::size_t a, std::size_t b) {
        return analysis.members[a].config->priority < analysis.members[b].config->priority;
    });
    if (ImGui::BeginTable("##team_members", 12,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX)) {
        for (const char* header : {"Monstre", "Rôle", "Box", "PV", "ATQ", "DEF", "VIT", "CR", "DC",
                                   "VIT min requise", "VIT max", "CR des runes"})
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
            // Stats de combat (runes du build + sets + artefacts + tours + leader).
            const auto& stats = member.combat;
            const int values[] = {stats.hp, stats.attack, stats.defense, member.speed.combat,
                                  stats.crit_rate, stats.crit_damage};
            for (int c = 0; c < 6; ++c) {
                ImGui::TableSetColumnIndex(3 + c);
                if (!member.has_runes) {
                    ImGui::TextColored(color::muted, "-");
                    continue;
                }
                const std::string text = c >= 4 ? std::to_string(values[c]) + " %"
                                                : group_digits(values[c]);
                // VIT colorée selon le plan / la fenêtre.
                if (c == 3) {
                    const int lower = member.window.found ? member.window.min : member.plan_min;
                    const bool ok = member.speed.combat >= lower &&
                                    (!member.window.found || member.window.upper_open ||
                                     member.speed.combat <= member.window.max);
                    ImGui::TextColored(ok ? color::positive : color::error, "%s", text.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Base %d + runes et sets %d + tours / leader %d",
                                          member.speed.base, member.speed.runes - member.speed.base,
                                          member.speed.bonus);
                } else {
                    ImGui::TextUnformatted(text.c_str());
                }
            }
            ImGui::TableSetColumnIndex(9);
            if (analysis.plan.found) ImGui::Text("%d", member.plan_min);
            else ImGui::TextColored(color::error, "?");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Plan joint : VIT de combat minimale (sans runes : %d)",
                                  member.speed.minimum);
            ImGui::TableSetColumnIndex(10);
            if (member.plan_max) ImGui::Text("%d", *member.plan_max);
            else ImGui::TextColored(color::muted, "-");
            ImGui::TableSetColumnIndex(11);
            const auto& crit = member.crit;
            if (!crit.needed) {
                ImGui::TextColored(color::muted, "pas de contrainte");
            } else if (member.has_runes) {
                ImGui::TextColored(crit.satisfied() ? color::positive : color::error, "%d / %d %%",
                                   crit.current_runes, crit.required_runes);
            } else {
                ImGui::Text("requis %d %%", crit.required_runes);
            }
            if (crit.needed && ImGui::IsItemHovered())
                ImGui::SetTooltip("Requis = 100 - base %d - leader %d - avantage élémentaire %d",
                                  crit.base, crit.leader, crit.advantage);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // --- Plan de vitesse (rempli automatiquement) ---
    ImGui::SeparatorText("Plan de vitesse");
    if (analysis.plan.found) {
        ImGui::TextColored(color::muted,
                           "VIT minimales requises (au moins 1 point d'écart quand l'ordre compte), "
                           "calculées en partant des VIT sans runes.");
        if (!analysis.relative.empty()) {
            std::string text;
            for (const auto& line : analysis.relative) text += (text.empty() ? "" : "  |  ") + line;
            ImGui::Text("Contraintes relatives : %s", text.c_str());
        }
    } else {
        ImGui::TextColored(color::error, "Plan introuvable : %s", analysis.plan.reason.c_str());
    }

    if (!simulated) {
        // Avant optimisation : critères seulement, pas de verdict sur les runes actuelles.
        ImGui::Text("Dégâts nécessaires : %s PV du boss (%s, DEF %s) avant son premier tour.",
                    group_digits(analysis.boss->hp).c_str(), analysis.boss->name.c_str(),
                    group_digits(analysis.boss->defense).c_str());
        if (ImGui::TreeNode("Ordre des tours du plan")) {
            render_turn_order(team, analysis, analysis.plan_atb, nullptr);
            ImGui::TreePop();
        }
        ImGui::TextColored(color::muted,
                           "Lancez « Optimiser la team » : le verdict sera calculé sur les builds "
                           "proposés.");
    } else {
        // --- Ordre des tours avec les builds ---
        ImGui::SeparatorText("Ordre des tours (builds optimisés, pire cas)");
        if (analysis.speed_check.ok)
            ImGui::TextColored(color::positive, "Plan de vitesse respecté (ordre strict).");
        else
            ImGui::TextColored(color::error, "Plan de vitesse non respecté : %s.",
                               analysis.speed_check.reason.c_str());
        render_turn_order(team, analysis, analysis.worst.atb, &analysis.worst.actions);
        // Meilleur cas : stuns réussis (tours ennemis perdus), critiques, S2.
        if (ImGui::TreeNode("Ordre des tours (meilleur cas)")) {
            ImGui::PushID("best_order");
            render_turn_order(team, analysis, analysis.best.atb, &analysis.best.actions);
            ImGui::PopID();
            ImGui::TreePop();
        }
    }

    // --- Verdict (sur les builds) ---
    const auto verdict = [&](const char* title, const swcore::CaseOutcome& outcome) {
        ImGui::PushID(title);
        if (outcome.kills)
            ImGui::TextColored(color::positive, "%s : boss mort avant son tour.", title);
        else
            ImGui::TextColored(color::error, "%s : boss vivant, %s PV restants (%s %%).", title,
                               group_digits(static_cast<long long>(outcome.remaining)).c_str(),
                               one_decimal(outcome.remaining_percent).c_str());
        ImGui::SameLine();
        ImGui::TextColored(color::muted, "dégâts %s / %s PV",
                           group_digits(static_cast<long long>(outcome.damage)).c_str(),
                           group_digits(static_cast<long long>(outcome.boss_hp)).c_str());
        if (outcome.counter) ImGui::TextColored(color::warning, "%s", outcome.counter->c_str());
        for (const auto& note : outcome.stun_notes) ImGui::TextColored(color::accent, "%s", note.c_str());
        if (ImGui::TreeNode("Détail des actions")) {
            const auto digits = [](double value) {
                return group_digits(static_cast<long long>(std::max(0.0, value)));
            };
            if (ImGui::BeginTable("##actions", 6,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_SizingFixedFit)) {
                for (const char* header :
                     {"Tic", "Action", "Dégâts (boss)", "PV du boss", "Conditions", "Note"})
                    ImGui::TableSetupColumn(header);
                ImGui::TableHeadersRow();
                for (const auto& action : outcome.actions) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("t%d", action.tick);
                    ImGui::TableSetColumnIndex(1);
                    if (action.extra)
                        ImGui::TextColored(color::accent, "  %s %s (passive)", action.actor.c_str(),
                                           action.skill_name.c_str());
                    else
                        ImGui::Text("%s S%d%s", action.actor.c_str(), action.skill,
                                    action.fallback ? " (repli)" : "");
                    ImGui::TableSetColumnIndex(2);
                    if (action.damage > 0) ImGui::TextUnformatted(digits(action.damage).c_str());
                    else ImGui::TextColored(color::muted, "-");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(digits(action.boss_hp_after).c_str());
                    ImGui::TableSetColumnIndex(4);
                    std::string conditions;
                    if (action.damage > 0) conditions += action.crit ? "critique" : "non critique";
                    if (action.def_broken) conditions += ", DEF brisée";
                    if (action.atk_buff) conditions += ", ATQ boostée";
                    ImGui::TextUnformatted(conditions.c_str());
                    ImGui::TableSetColumnIndex(5);
                    std::string note = action.note;
                    if (!action.extra && !action.passive.empty() &&
                        action.passive.rfind("+ ", 0) != 0)
                        note += (note.empty() ? "" : ", ") + action.passive;
                    if (action.kills) note += note.empty() ? "coup fatal" : ", coup fatal";
                    // Attaque de zone : une ligne par ennemi touché.
                    for (const auto& hit : action.targets) {
                        if (!note.empty()) note += "\n";
                        note += hit.name + " : " + digits(hit.damage) + " (reste " +
                                digits(hit.hp_after) + ")";
                        if (hit.kills) note += ", morte";
                        if (hit.stun_chance)
                            note += ", stun " + one_decimal(*hit.stun_chance) + " %";
                        if (!hit.stun_text.empty()) note += " : " + hit.stun_text;
                    }
                    ImGui::TextUnformatted(note.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    if (simulated) {
        ImGui::SeparatorText("Verdict (builds optimisés)");
        for (const auto& hypothesis : analysis.hypotheses)
            ImGui::TextColored(color::warning, "Hypothèse : %s", hypothesis.c_str());
        verdict("Pire cas", analysis.worst);
        verdict("Meilleur cas", analysis.best);
        ImGui::TextColored(color::muted,
                           "Pire cas : replis, effets aléatoires ratés, hypothèses inconnues = 0. "
                           "Meilleur cas : l'inverse, coups critiques.");
        if (analysis.missing.empty()) {
            ImGui::TextColored(color::positive, "Rien ne manque au pire cas.");
        } else {
            ImGui::TextUnformatted("Ce qui manque (pire cas) :");
            for (const auto& line : analysis.missing) ImGui::BulletText("%s", line.c_str());
        }

        // Survie au premier coup du boss (stats de combat, tours de cristal réelles).
        if (!analysis.survival.empty()) {
            ImGui::SeparatorText(
                ("Survie au premier coup du boss (" + analysis.boss_skill + ")").c_str());
            if (!analysis.survival_matters)
                ImGui::TextColored(color::warning,
                                   "Personne ne survit même sans critique : l'objectif est de "
                                   "tuer le boss avant son tour.");
            ImGui::TextColored(color::muted, "Tour d'ATQ avant le boss : %s ; tour de DEF : %s.",
                               analysis.worst.attack_tower_before_boss ? "oui" : "non",
                               analysis.worst.defense_tower_before_boss ? "oui" : "non");
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
    }

    // --- Comparaison facultative avec les runes actuelles ---
    ImGui::Checkbox("Comparer avec mes runes actuelles", &team_compare_current_);
    if (team_compare_current_) {
        if (!data_.export_file.loaded) {
            ImGui::TextColored(color::muted, "Aucun export chargé.");
        } else {
            const std::string compare_key =
                common + "#actuel#" + (result ? std::to_string(team_opt_revision_) : "-");
            if (!team_compare_ || compare_key != team_compare_key_) {
                auto input = base_input();
                input.use_current_runes = true;
                if (result) input.pinned_units = result->units;
                team_compare_ = swcore::analyze_team(input);
                team_compare_key_ = compare_key;
            }
            const auto& current = *team_compare_;
            if (current.valid) {
                ImGui::PushID("compare");
                std::string speeds;
                for (const auto& member : current.members)
                    speeds += (speeds.empty() ? "" : ", ") + member.config->name + " " +
                              std::to_string(member.speed.combat);
                ImGui::Text("Runes actuelles - VIT de combat : %s", speeds.c_str());
                ImGui::TextColored(current.speed_check.ok ? color::positive : color::error,
                                   "Runes actuelles - plan de vitesse : %s",
                                   current.speed_check.ok ? "respecté"
                                                          : current.speed_check.reason.c_str());
                verdict("Runes actuelles, pire cas", current.worst);
                verdict("Runes actuelles, meilleur cas", current.best);
                ImGui::PopID();
            }
        }
    }

    // Hypothèses non vérifiées réellement utilisées, et limites du calcul.
    for (const auto& key_name : analysis.unverified)
        ImGui::TextColored(color::warning, "Hypothèse non vérifiée utilisée (pire cas : 0) : %s",
                           key_name.c_str());
    for (const auto& note : analysis.notes) ImGui::TextColored(color::muted, "%s", note.c_str());
}

}  // namespace swgui

