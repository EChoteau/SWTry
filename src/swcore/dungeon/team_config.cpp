#include "swcore/dungeon/team_config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace swcore {

namespace {

using json = nlohmann::json;

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

std::string string_or(const json& object, const char* key, std::string fallback = {}) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

int int_or(const json& object, const char* key, int fallback = 0) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

std::vector<std::string> strings(const json& object, const char* key) {
    std::vector<std::string> result;
    if (const auto it = object.find(key); it != object.end() && it->is_array())
        for (const auto& value : *it)
            if (value.is_string()) result.push_back(value.get<std::string>());
    return result;
}

std::vector<int> ints(const json& object, const char* key) {
    std::vector<int> result;
    if (const auto it = object.find(key); it != object.end() && it->is_array())
        for (const auto& value : *it)
            if (value.is_number_integer()) result.push_back(value.get<int>());
    return result;
}

std::optional<TeamConfig> parse_team(const json& root, const std::filesystem::path& file,
                                     const WarningSink& warnings) {
    const std::string where = file.filename().string();
    if (!root.is_object()) {
        warn(warnings, "Team " + where + " : l'objet racine est attendu");
        return std::nullopt;
    }
    TeamConfig team;
    team.file = file;
    team.id = string_or(root, "id", file.stem().string());
    team.name = string_or(root, "name", team.id);
    team.dungeon_id = int_or(root, "dungeon_id");
    team.level_ids = ints(root, "level_ids");
    team.leader = string_or(root, "leader");
    team.boss = string_or(root, "boss");
    team.notes = string_or(root, "notes");
    if (const auto it = root.find("tempest_ready_on_boss"); it != root.end() && it->is_boolean())
        team.tempest_ready_on_boss = it->get<bool>();
    team.tempest_ready_note = string_or(root, "tempest_ready_note");
    if (const auto it = root.find("monsters"); it != root.end() && it->is_array())
        for (const auto& value : *it) {
            if (!value.is_object()) continue;
            TeamMonster monster;
            monster.name = string_or(value, "name");
            monster.swarfarm_id = int_or(value, "swarfarm_id");
            monster.role = string_or(value, "role");
            monster.priority = int_or(value, "priority");
            monster.skill_overrides = ints(value, "skills");
            team.monsters.push_back(std::move(monster));
        }
    if (const auto it = root.find("boss_script"); it != root.end() && it->is_array())
        for (const auto& value : *it) {
            if (!value.is_object()) continue;
            ScriptStep step;
            step.actor = string_or(value, "actor");
            step.skill = int_or(value, "skill");
            if (const auto fallback = value.find("fallback_skill");
                fallback != value.end() && fallback->is_number_integer())
                step.fallback_skill = fallback->get<int>();
            step.target = string_or(value, "target");
            step.effects = strings(value, "effects");
            team.boss_script.push_back(std::move(step));
        }
    if (const auto it = root.find("rune_presets"); it != root.end() && it->is_object())
        for (const auto& [preset, by_monster] : it->items()) {
            auto& sets = team.rune_presets[preset];
            if (!by_monster.is_object()) continue;
            for (const auto& [monster, names] : by_monster.items())
                if (names.is_array())
                    for (const auto& name : names)
                        if (name.is_string()) sets[monster].push_back(name.get<std::string>());
        }
    if (team.monsters.empty()) {
        warn(warnings, "Team " + where + " : aucun monstre");
        return std::nullopt;
    }
    return team;
}

}  // namespace

const TeamMonster* TeamConfig::monster(const std::string& name) const {
    for (const auto& entry : monsters)
        if (entry.name == name) return &entry;
    return nullptr;
}

bool TeamConfig::matches(int dungeon, int level) const {
    return dungeon_id == dungeon &&
           std::find(level_ids.begin(), level_ids.end(), level) != level_ids.end();
}

std::optional<TeamConfig> load_team_config(const std::filesystem::path& file,
                                           WarningSink warnings) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        warn(warnings, "Team : fichier illisible : " + file.string());
        return std::nullopt;
    }
    try {
        json root;
        input >> root;
        return parse_team(root, file, warnings);
    } catch (const json::exception& error) {
        warn(warnings, "Team " + file.filename().string() + " : JSON invalide : " + error.what());
        return std::nullopt;
    }
}

std::vector<TeamConfig> load_team_configs(const std::filesystem::path& directory,
                                          WarningSink warnings) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (std::filesystem::is_directory(directory, error))
        // Récursif : une team par fichier, rangées par donjon (teams/<donjon>/).
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(directory, error))
            if (entry.path().extension() == ".json") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    std::vector<TeamConfig> teams;
    for (const auto& file : files)
        if (auto team = load_team_config(file, warnings)) teams.push_back(std::move(*team));
    return teams;
}

void validate_team_config(TeamConfig& team, const ReferenceDB& reference,
                          const std::unordered_map<int, std::string>& set_names) {
    team.problems.clear();
    std::set<std::string> names;
    for (const auto& monster : team.monsters) {
        if (!names.insert(monster.name).second)
            team.problems.push_back("monstre en double : " + monster.name);
        const auto* known = reference.find_swarfarm(monster.swarfarm_id);
        if (!known) {
            team.problems.push_back(monster.name + " : id SWARFARM " +
                                    std::to_string(monster.swarfarm_id) +
                                    " absent de monsters.json");
        } else if (known->name.rfind(monster.name, 0) != 0) {
            // "Homunculus" accepte "Homunculus(Attack)" ; sinon le nom diffère.
            team.problems.push_back(monster.name + " : l'id " +
                                    std::to_string(monster.swarfarm_id) +
                                    " correspond à " + known->name);
        }
    }
    if (team.monsters.size() > 5)
        team.problems.push_back("plus de 5 monstres (" + std::to_string(team.monsters.size()) +
                                ")");
    if (!team.leader.empty() && !team.monster(team.leader))
        team.problems.push_back("leader absent de la team : " + team.leader);
    for (std::size_t i = 0; i < team.boss_script.size(); ++i) {
        const auto& step = team.boss_script[i];
        if (!team.monster(step.actor))
            team.problems.push_back("script, action " + std::to_string(i + 1) +
                                    " : acteur absent de la team : " + step.actor);
        if (step.skill < 1)
            team.problems.push_back("script, action " + std::to_string(i + 1) +
                                    " : compétence invalide");
    }
    for (const auto& [preset, by_monster] : team.rune_presets)
        for (const auto& [monster, sets] : by_monster) {
            if (!team.monster(monster))
                team.problems.push_back(preset + " : monstre absent de la team : " + monster);
            for (const auto& set : sets) {
                const bool known = std::any_of(set_names.begin(), set_names.end(),
                                               [&](const auto& entry) { return entry.second == set; });
                if (!known)
                    team.problems.push_back(preset + " / " + monster + " : set inconnu : " + set);
            }
        }
}

TeamOwnership team_ownership(const TeamConfig& team, const ReferenceDB& reference,
                             const std::vector<AccountMonster>& box) {
    TeamOwnership ownership;
    for (const auto& monster : team.monsters) {
        const auto* known = reference.find_swarfarm(monster.swarfarm_id);
        const bool owned =
            known && std::any_of(box.begin(), box.end(), [&](const AccountMonster& unit) {
                return unit.unit_master_id == known->com2us_id;
            });
        if (!owned) ownership.missing.push_back(monster.name);
    }
    return ownership;
}

namespace {

void flatten(const json& value, const std::string& prefix,
             std::map<std::string, MechanicValue>& out) {
    if (value.is_object()) {
        for (const auto& [key, child] : value.items())
            flatten(child, prefix.empty() ? key : prefix + "." + key, out);
        return;
    }
    MechanicValue parsed;
    if (value.is_number()) {
        parsed.kind = MechanicValue::Kind::Number;
        parsed.number = value.get<double>();
    } else if (value.is_boolean()) {
        parsed.kind = MechanicValue::Kind::Bool;
        parsed.flag = value.get<bool>();
    } else if (value.is_string()) {
        parsed.kind = MechanicValue::Kind::Text;
        parsed.text = value.get<std::string>();
    }  // null (ou tableau) : non vérifié
    out[prefix] = std::move(parsed);
}

}  // namespace

Mechanics Mechanics::parse(const std::string& json_text, WarningSink warnings) {
    Mechanics mechanics;
    try {
        const json root = json::parse(json_text);
        if (!root.is_object()) {
            warn(warnings, "mechanics.json : l'objet racine est attendu");
            return mechanics;
        }
        flatten(root, "", mechanics.values_);
        mechanics.loaded_ = true;
    } catch (const json::exception& error) {
        warn(warnings, std::string("mechanics.json : JSON invalide : ") + error.what());
    }
    return mechanics;
}

Mechanics Mechanics::load(const std::filesystem::path& file, WarningSink warnings) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        warn(warnings, "mechanics.json introuvable : " + file.string());
        return {};
    }
    std::ostringstream text;
    text << input.rdbuf();
    return parse(text.str(), std::move(warnings));
}

std::optional<double> Mechanics::number(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end() || it->second.kind != MechanicValue::Kind::Number) return std::nullopt;
    return it->second.number;
}

std::optional<bool> Mechanics::flag(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end() || it->second.kind != MechanicValue::Kind::Bool) return std::nullopt;
    return it->second.flag;
}

std::optional<std::string> Mechanics::text(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end() || it->second.kind != MechanicValue::Kind::Text) return std::nullopt;
    return it->second.text;
}

std::vector<std::string> Mechanics::unverified() const {
    std::vector<std::string> keys;
    for (const auto& [key, value] : values_)
        if (value.kind == MechanicValue::Kind::Null) keys.push_back(key);
    return keys;
}

}  // namespace swcore
