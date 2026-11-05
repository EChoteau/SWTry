// Écran team (page Donjons) : réglages de l'optimisation (sets, verrous,
// meules / gemmes, enregistrés dans data/user/team_settings.json) et
// optimisation de team en arrière-plan (swcore::optimize_team).

#include "app.hpp"
#include "widgets/gui_common.hpp"
#include "widgets/text_util.hpp"

#include "swcore/solver/build_text.hpp"
#include "swcore/account/locks.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace swgui {

namespace {

using json = nlohmann::json;

json rules_to_json(const std::map<int, swcore::GrindstoneRule>& rules) {
    json object = json::object();
    for (const auto& [stat, rule] : rules)
        object[std::to_string(stat)] = {{"mode", static_cast<int>(rule.mode)},
                                        {"rarity", rule.rarity},
                                        {"exact", rule.exact}};
    return object;
}

std::map<int, swcore::GrindstoneRule> rules_from_json(const json& object) {
    std::map<int, swcore::GrindstoneRule> rules;
    if (!object.is_object()) return rules;
    for (const auto& [key, value] : object.items()) {
        try {
            swcore::GrindstoneRule rule;
            rule.mode = static_cast<swcore::GrindstoneMode>(value.value("mode", 0));
            rule.rarity = value.value("rarity", 5);
            rule.exact = value.value("exact", 0);
            if (rule.mode != swcore::GrindstoneMode::Off) rules[std::stoi(key)] = rule;
        } catch (const std::exception&) {
        }
    }
    return rules;
}

std::string sets_text(const std::vector<std::string>& sets) {
    if (sets.empty()) return "tous sets";
    std::string text;
    for (const auto& set : sets) text += (text.empty() ? "" : " + ") + set;
    return text;
}

}  // namespace

App::TeamUiSettings& App::team_settings(const std::string& team_id) {
    if (!team_settings_loaded_) {
        team_settings_loaded_ = true;
        std::ifstream input(data_.user_directory() / "team_settings.json", std::ios::binary);
        try {
            json root;
            if (input) input >> root;
            if (root.is_object() && root.contains("teams") && root["teams"].is_object())
                for (const auto& [id, value] : root["teams"].items()) {
                    TeamUiSettings settings;
                    settings.active_set = value.value("active_set", std::string{"set1"});
                    if (value.contains("set3") && value["set3"].is_object())
                        for (const auto& [monster, sets] : value["set3"].items())
                            if (sets.is_array())
                                for (const auto& set : sets)
                                    if (set.is_string())
                                        settings.set3[monster].push_back(set.get<std::string>());
                    if (value.contains("locked") && value["locked"].is_array())
                        for (const auto& name : value["locked"])
                            if (name.is_string()) settings.locked.insert(name.get<std::string>());
                    settings.grind_on = value.value("grind_on", false);
                    settings.gem_on = value.value("gem_on", false);
                    if (value.contains("grindstones"))
                        settings.craft.grindstones = rules_from_json(value["grindstones"]);
                    if (value.contains("gems")) settings.craft.gems = rules_from_json(value["gems"]);
                    team_settings_[id] = std::move(settings);
                }
        } catch (const std::exception&) {
            data_.warnings.push_back("team_settings.json illisible : réglages par défaut.");
        }
    }
    return team_settings_[team_id];
}

void App::save_team_settings() {
    json teams = json::object();
    for (const auto& [id, settings] : team_settings_) {
        json set3 = json::object();
        for (const auto& [monster, sets] : settings.set3) set3[monster] = sets;
        teams[id] = {{"active_set", settings.active_set},
                     {"set3", set3},
                     {"locked", std::vector<std::string>(settings.locked.begin(),
                                                         settings.locked.end())},
                     {"grind_on", settings.grind_on},
                     {"gem_on", settings.gem_on},
                     {"grindstones", rules_to_json(settings.craft.grindstones)},
                     {"gems", rules_to_json(settings.craft.gems)}};
    }
    const json root{{"version", 1}, {"teams", teams}};
    if (!swcore::write_text_atomic(data_.user_directory() / "team_settings.json",
                                   root.dump(2) + "\n"))
        set_status("Impossible d'écrire data/user/team_settings.json");
}

std::map<std::string, std::vector<std::string>> App::team_sets(
    const swcore::TeamConfig& team, const TeamUiSettings& settings) const {
    if (settings.active_set == "set3") return settings.set3;
    const auto preset = team.rune_presets.find(settings.active_set);
    return preset == team.rune_presets.end()
               ? std::map<std::string, std::vector<std::string>>{}
               : preset->second;
}

void App::render_team_settings(const swcore::TeamConfig& team) {
    auto& settings = team_settings(team.id);
    const std::string before = json{{"s", settings.active_set},
                                    {"3", settings.set3},
                                    {"l", std::vector<std::string>(settings.locked.begin(),
                                                                   settings.locked.end())},
                                    {"g", settings.grind_on},
                                    {"e", settings.gem_on},
                                    {"gr", rules_to_json(settings.craft.grindstones)},
                                    {"ge", rules_to_json(settings.craft.gems)}}
                                   .dump();

    ImGui::SeparatorText("Réglages de l'optimisation");
    // Sets : Set 1 / Set 2 (conseillés, config) ou Set 3 (personnalisé).
    ImGui::TextUnformatted("Sets :");
    for (const auto& [key, label] : {std::pair{"set1", "Set 1 (conseillé)"},
                                     std::pair{"set2", "Set 2 (conseillé)"},
                                     std::pair{"set3", "Set 3 (personnalisé)"}}) {
        ImGui::SameLine();
        if (ImGui::RadioButton(label, settings.active_set == key)) settings.active_set = key;
    }
    std::vector<std::string> set_names;
    for (const auto& [id, name] : data_.mapping.set_names) set_names.push_back(name);
    std::sort(set_names.begin(), set_names.end());
    const auto& set_name_map = data_.mapping.set_names;

    if (ImGui::BeginTable("##team_sets", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Monstre");
        ImGui::TableSetupColumn("Sets");
        ImGui::TableSetupColumn("Verrou");
        ImGui::TableHeadersRow();
        const auto chosen = team_sets(team, settings);
        for (const auto& monster : team.monsters) {
            const auto& name = monster.name;
            ImGui::PushID(name.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableSetColumnIndex(1);
            if (settings.active_set == "set3") {
                // Jusqu'à 3 sets ; "—" retire le set.
                auto& sets = settings.set3[name];
                for (int slot = 0; slot < 3; ++slot) {
                    ImGui::PushID(slot);
                    if (slot) ImGui::SameLine();
                    const std::string current =
                        slot < static_cast<int>(sets.size()) ? sets[static_cast<std::size_t>(slot)]
                                                             : std::string{"—"};
                    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.5f);
                    if (ImGui::BeginCombo("##set", current.c_str())) {
                        if (ImGui::Selectable("—") && slot < static_cast<int>(sets.size()))
                            sets.erase(sets.begin() + slot);
                        for (const auto& set : set_names)
                            if (ImGui::Selectable(set.c_str(), set == current)) {
                                if (slot < static_cast<int>(sets.size()))
                                    sets[static_cast<std::size_t>(slot)] = set;
                                else
                                    sets.push_back(set);
                            }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
            } else {
                const auto it = chosen.find(name);
                ImGui::TextUnformatted(
                    sets_text(it == chosen.end() ? std::vector<std::string>{} : it->second).c_str());
            }
            // Validité (6 pièces, sets connus).
            const auto it = chosen.find(name);
            std::string problem;
            if (!swcore::scenario_from_sets(it == chosen.end() ? std::vector<std::string>{}
                                                               : it->second,
                                            set_name_map, data_.effects, &problem)) {
                ImGui::SameLine();
                ImGui::TextColored(color::error, "(%s)", problem.c_str());
            }
            ImGui::TableSetColumnIndex(2);
            bool locked = settings.locked.contains(name);
            if (ImGui::Checkbox("##lock", &locked)) {
                if (locked) settings.locked.insert(name);
                else settings.locked.erase(name);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Verrouillé : garde son build et ses runes, jamais modifié "
                                  "par l'optimisation de team.");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Meules / gemmes : interrupteur, puis mêmes réglages que l'Optimiseur
    // (menus communs appliqués tout de suite, réglage par ligne ensuite).
    for (const bool gems : {false, true}) {
        ImGui::PushID(gems ? "team_gems" : "team_grinds");
        bool& on = gems ? settings.gem_on : settings.grind_on;
        auto& rules = gems ? settings.craft.gems : settings.craft.grindstones;
        if (ImGui::Checkbox(gems ? "Gemmes" : "Meules", &on) && on && rules.empty()) {
            // Première activation : règles des menus communs sur toutes les stats.
            const auto& global = settings.global[gems ? 1 : 0];
            const auto& ranges = gems ? gem_ranges_ : grind_ranges_;
            for (const auto& [stat, by_rarity] : ranges)
                if (gems || swcore::is_grindable_stat(stat))
                    rules[stat] = {static_cast<swcore::GrindstoneMode>(global.mode),
                                   global.rarity, 0};
        }
        if (on) {
            ImGui::SameLine();
            if (ImGui::TreeNode(gems ? "Réglages des gemmes" : "Réglages des meules")) {
                render_craft_rules(settings.craft, gems, settings.global[gems ? 1 : 0]);
                ImGui::TreePop();
            }
        }
        ImGui::PopID();
    }

    const std::string after = json{{"s", settings.active_set},
                                   {"3", settings.set3},
                                   {"l", std::vector<std::string>(settings.locked.begin(),
                                                                  settings.locked.end())},
                                   {"g", settings.grind_on},
                                   {"e", settings.gem_on},
                                   {"gr", rules_to_json(settings.craft.grindstones)},
                                   {"ge", rules_to_json(settings.craft.gems)}}
                                  .dump();
    if (after != before) save_team_settings();
}

void App::start_team_optimization(const DataStore::DungeonState& state,
                                  const swcore::TeamConfig& team) {
    const auto* level = state.data.level(dungeon_level_id_);
    if (!level || !data_.export_file.loaded) return;
    const auto& settings = team_settings(team.id);

    // Copies : l'optimisation ne lit rien de l'interface pendant qu'elle tourne.
    struct Inputs {
        std::vector<swcore::AccountMonster> box;
        std::vector<swcore::Rune> runes;
        swcore::SetEffects effects;
        swcore::TowerBonuses towers;
        swcore::LockTable locks;
        std::unordered_map<int, std::string> set_names;
        std::filesystem::path mapping;
        swcore::TeamOptimizerSettings settings;
        swcore::TeamAnalysisInput analysis;
    };
    auto inputs = std::make_shared<Inputs>();
    inputs->box = data_.monsters;
    inputs->runes = data_.runes;
    inputs->effects = data_.effects;
    inputs->towers = state.towers;
    inputs->locks = data_.locks;
    inputs->set_names = data_.mapping.set_names;
    inputs->mapping = data_.game_directory();
    inputs->settings.sets = team_sets(team, settings);
    inputs->settings.locked = settings.locked;
    if (settings.grind_on) inputs->settings.grindstones = settings.craft.grindstones;
    if (settings.gem_on) inputs->settings.gems = settings.craft.gems;
    inputs->analysis.team = &team;  // données de la page Donjons : stables
    inputs->analysis.data = &state.data;
    inputs->analysis.dungeon = state.data.dungeon(dungeon_id_);
    inputs->analysis.level = level;
    inputs->analysis.reference = &data_.reference;
    inputs->analysis.mechanics = &state.mechanics;

    team_opt_.reset();  // arrête et joint une optimisation précédente
    team_opt_ = std::make_unique<TeamOptimization>();
    ++team_opt_revision_;
    team_opt_->team_id = team.id;
    team_opt_->level_id = level->id;
    team_opt_->running = true;
    team_opt_->progress = "Démarrage...";
    TeamOptimization* job = team_opt_.get();
    job->thread = std::jthread([job, inputs](std::stop_token stop) {
        swcore::TeamOptimizerInput input;
        input.analysis = inputs->analysis;
        input.analysis.effects = &inputs->effects;
        input.analysis.towers = &inputs->towers;
        input.box = &inputs->box;
        input.runes = &inputs->runes;
        input.mapping_directory = inputs->mapping;
        input.locks = &inputs->locks;
        input.set_names = &inputs->set_names;
        auto result = swcore::optimize_team(input, inputs->settings, stop,
                                            [job](const std::string& message) {
                                                std::lock_guard lock(job->mutex);
                                                job->progress = message;
                                            });
        {
            std::lock_guard lock(job->mutex);
            job->result = std::move(result);
        }
        job->running = false;
    });
    set_status("Optimisation de la team lancée.");
}

void App::render_team_optimization(const DataStore::DungeonState& state,
                                   const swcore::TeamConfig& team) {
    ImGui::SeparatorText("Optimisation de la team");
    const bool mine = team_opt_ && team_opt_->team_id == team.id &&
                      team_opt_->level_id == dungeon_level_id_;
    const bool running = mine && team_opt_->running;
    if (!running) {
        ImGui::BeginDisabled(!data_.export_file.loaded);
        if (ImGui::Button("Optimiser la team")) start_team_optimization(state, team);
        ImGui::EndDisabled();
        if (!data_.export_file.loaded) {
            ImGui::SameLine();
            ImGui::TextColored(color::muted, "Chargez un export (page Données).");
        } else {
            ImGui::SameLine();
            ImGui::TextColored(color::muted,
                               "Glouton puis descente coordonnée (3 passes au plus) ; "
                               "8 s au plus par monstre et par passe.");
        }
    } else {
        if (ImGui::Button("Annuler")) team_opt_->thread.request_stop();
        ImGui::SameLine();
        std::lock_guard lock(team_opt_->mutex);
        ImGui::TextColored(color::accent, "%s", team_opt_->progress.c_str());
        return;
    }
    if (!mine || !team_opt_->result) return;
    const auto& result = *team_opt_->result;

    // Résumé de la simulation finale (seule source de "contraintes tenues").
    ImGui::TextColored(result.constraints_ok ? color::positive : color::error, "%s",
                       result.constraints_ok ? "Contraintes tenues (plan de vitesse strict, CR)."
                                             : "Contraintes non tenues : voir « Ce qui manque ».");
    ImGui::SameLine();
    ImGui::TextColored(result.kills ? color::positive : color::error, "%s",
                       result.kills ? "Boss mort au pire cas." : "Boss vivant au pire cas.");
    ImGui::TextColored(color::muted, "Dégâts au pire cas %s, au meilleur cas %s.",
                       group_digits(static_cast<long long>(result.worst_damage)).c_str(),
                       group_digits(static_cast<long long>(result.best_damage)).c_str());
    if (result.cancelled) ImGui::TextColored(color::warning, "Optimisation annulée : résultat partiel.");

    if (ImGui::BeginTable("##team_builds", 7,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingFixedFit)) {
        for (const char* header : {"Monstre", "Statut", "Sets", "VIT runes", "ATQ runes",
                                   "DC / CR runes", ""})
            ImGui::TableSetupColumn(header);
        ImGui::TableHeadersRow();
        for (const auto& member : result.members) {
            ImGui::PushID(member.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(member.name.c_str());
            ImGui::TableSetColumnIndex(1);
            if (member.locked) ImGui::TextColored(color::muted, "verrouillé");
            else if (!member.owned) ImGui::TextColored(color::warning, "absent de la box");
            else if (!member.build) ImGui::TextColored(color::error, "%s", member.note.c_str());
            else if (member.changed) ImGui::TextColored(color::positive, "nouveau build");
            else ImGui::TextColored(color::muted, "inchangé");
            if (member.build && !member.note.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(color::warning, "(%s)", member.note.c_str());
            }
            if (member.build) {
                const auto& stats = member.build->stats.total;
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(member.scenario.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%d", stats.speed);
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(group_digits(stats.attack).c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%d %% / %d %%", stats.crit_damage, stats.crit_rate);
                ImGui::TableSetColumnIndex(6);
                const auto unit = result.units.find(member.name);
                const auto* target =
                    unit == result.units.end() ? nullptr : find_monster(unit->second);
                ImGui::BeginDisabled(!target);
                if (ImGui::SmallButton("Ouvrir le build") && target)
                    open_build_tab(*target, *member.build, member.config, member.scenario, true);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Onglet de build : Équiper / Verrouiller comme d'habitude.");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::TreeNode("Déroulement")) {
        for (const auto& line : result.log) ImGui::TextUnformatted(line.c_str());
        ImGui::TreePop();
    }
}

const swcore::TeamOptimizerResult* App::team_result(const swcore::TeamConfig& team) const {
    if (!team_opt_ || team_opt_->running || !team_opt_->result) return nullptr;
    if (team_opt_->team_id != team.id || team_opt_->level_id != dungeon_level_id_) return nullptr;
    return &*team_opt_->result;
}

}  // namespace swgui
