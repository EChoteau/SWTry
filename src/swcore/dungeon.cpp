#include "swcore/dungeon.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace swcore {

namespace {

using json = nlohmann::json;

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

std::optional<json> read_json(const std::filesystem::path& path, const WarningSink& warnings,
                              bool required = true) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (required) warn(warnings, "Donjons : fichier introuvable : " + path.string());
        return std::nullopt;
    }
    try {
        json root;
        input >> root;
        return root;
    } catch (const json::exception& error) {
        warn(warnings, "Donjons : JSON invalide (" + path.filename().string() + ") : " +
                           error.what());
        return std::nullopt;
    }
}

int int_or(const json& object, const char* key, int fallback = 0) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<int>() : fallback;
}

std::string string_or(const json& object, const char* key, std::string fallback = {}) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

bool bool_or(const json& object, const char* key, bool fallback = false) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

SkillEffect parse_effect(const json& value) {
    SkillEffect effect;
    if (const auto it = value.find("effect"); it != value.end() && it->is_object())
        effect.name = string_or(*it, "name");
    else
        effect.name = string_or(value, "name");  // forme courte des overrides
    if (const auto it = value.find("chance"); it != value.end() && it->is_number())
        effect.chance = it->get<double>();
    effect.quantity = int_or(value, "quantity");
    effect.self_effect = bool_or(value, "self_effect");
    effect.aoe = bool_or(value, "aoe");
    return effect;
}

SkillData parse_skill(const json& value) {
    SkillData skill;
    skill.id = int_or(value, "id");
    skill.com2us_id = int_or(value, "com2us_id");
    skill.name = string_or(value, "name");
    skill.slot = int_or(value, "slot");
    if (const auto it = value.find("cooltime"); it != value.end() && it->is_number())
        skill.cooltime = it->get<int>();
    skill.hits = int_or(value, "hits");
    skill.aoe = bool_or(value, "aoe");
    skill.passive = bool_or(value, "passive");
    skill.max_level = int_or(value, "max_level", 1);
    skill.multiplier_formula = string_or(value, "multiplier_formula");
    if (const auto it = value.find("upgrades"); it != value.end() && it->is_array())
        for (const auto& upgrade : *it) {
            if (!upgrade.is_object()) continue;
            SkillUpgrade parsed;
            parsed.effect = string_or(upgrade, "effect");
            if (const auto amount = upgrade.find("amount");
                amount != upgrade.end() && amount->is_number())
                parsed.amount = amount->get<double>();
            skill.upgrades.push_back(std::move(parsed));
        }
    if (const auto it = value.find("effects"); it != value.end() && it->is_array())
        for (const auto& effect : *it)
            if (effect.is_object()) skill.effects.push_back(parse_effect(effect));
    return skill;
}

DungeonEnemy parse_enemy(const json& value, const ReferenceDB& reference) {
    DungeonEnemy enemy;
    enemy.swarfarm_id = int_or(value, "monster");
    enemy.level = int_or(value, "level");
    enemy.stars = int_or(value, "stars");
    enemy.hp = int_or(value, "hp");
    enemy.attack = int_or(value, "attack");
    enemy.defense = int_or(value, "defense");
    enemy.speed = int_or(value, "speed");
    enemy.resist = int_or(value, "resist");
    enemy.accuracy_bonus = int_or(value, "accuracy_bonus");
    enemy.crit_bonus = int_or(value, "crit_bonus");
    enemy.crit_damage_reduction = int_or(value, "crit_damage_reduction");
    if (const auto* monster = reference.find_swarfarm(enemy.swarfarm_id)) {
        enemy.known = true;
        enemy.com2us_id = monster->com2us_id;
        enemy.element = monster->element;
        enemy.name = monster->name;
        enemy.skill_ids = monster->skills;
    } else {
        enemy.name = "Monstre " + std::to_string(enemy.swarfarm_id);
    }
    return enemy;
}

}  // namespace

std::map<int, SkillOverride> load_skill_overrides(const std::filesystem::path& path,
                                                  WarningSink warnings) {
    std::map<int, SkillOverride> overrides;
    const auto root = read_json(path, warnings, false);  // fichier facultatif
    if (!root) return overrides;
    const auto skills = root->find("skills");
    if (skills == root->end() || !skills->is_object()) {
        warn(warnings, "Overrides : clé \"skills\" absente ou invalide");
        return overrides;
    }
    for (const auto& [key, value] : skills->items()) {
        int id = 0;
        try {
            id = std::stoi(key);
        } catch (const std::exception&) {
            warn(warnings, "Overrides : id de compétence invalide : " + key);
            continue;
        }
        if (!value.is_object()) continue;
        SkillOverride override;
        if (const auto it = value.find("name"); it != value.end() && it->is_string())
            override.name = it->get<std::string>();
        if (const auto it = value.find("aoe"); it != value.end() && it->is_boolean())
            override.aoe = it->get<bool>();
        if (const auto it = value.find("multiplier_formula");
            it != value.end() && it->is_string())
            override.multiplier_formula = it->get<std::string>();
        if (const auto it = value.find("cooltime"); it != value.end()) {
            if (it->is_null()) override.cooltime = std::optional<int>{};
            else if (it->is_number_integer()) override.cooltime = it->get<int>();
        }
        if (const auto it = value.find("hits"); it != value.end() && it->is_number_integer())
            override.hits = it->get<int>();
        if (const auto it = value.find("effects"); it != value.end() && it->is_array()) {
            std::vector<SkillEffect> effects;
            for (const auto& effect : *it)
                if (effect.is_object()) effects.push_back(parse_effect(effect));
            override.effects = std::move(effects);
        }
        override.note = string_or(value, "note");
        overrides[id] = std::move(override);
    }
    return overrides;
}

void apply_skill_override(SkillData& skill, const SkillOverride& override) {
    if (override.name) skill.name = *override.name;
    if (override.aoe) skill.aoe = *override.aoe;
    if (override.multiplier_formula) skill.multiplier_formula = *override.multiplier_formula;
    if (override.cooltime) skill.cooltime = *override.cooltime;
    if (override.hits) skill.hits = *override.hits;
    if (override.effects) skill.effects = *override.effects;
    skill.overridden = true;
    skill.override_note = override.note;
}

DungeonData DungeonData::load(const std::filesystem::path& swarfarm_directory,
                              const std::filesystem::path& overrides,
                              const ReferenceDB& reference, WarningSink warnings) {
    DungeonData data;

    if (const auto root = read_json(swarfarm_directory / "dungeons.json", warnings);
        root && root->is_array()) {
        for (const auto& value : *root) {
            if (!value.is_object()) continue;
            Dungeon dungeon;
            dungeon.id = int_or(value, "id");
            dungeon.name = string_or(value, "name");
            dungeon.category = string_or(value, "category", "Autre");
            dungeon.enabled = bool_or(value, "enabled", true);
            if (const auto it = value.find("levels"); it != value.end() && it->is_array())
                for (const auto& level : *it)
                    if (level.is_number_integer()) dungeon.level_ids.push_back(level.get<int>());
            if (dungeon.id == 0) continue;
            if (std::find(data.categories_.begin(), data.categories_.end(), dungeon.category) ==
                data.categories_.end())
                data.categories_.push_back(dungeon.category);
            data.dungeon_index_[dungeon.id] = data.dungeons_.size();
            data.dungeons_.push_back(std::move(dungeon));
        }
    }

    if (const auto root = read_json(swarfarm_directory / "levels.json", warnings);
        root && root->is_array()) {
        for (const auto& value : *root) {
            if (!value.is_object()) continue;
            DungeonLevel level;
            level.id = int_or(value, "id");
            level.dungeon_id = int_or(value, "dungeon");
            level.floor = int_or(value, "floor");
            if (const auto it = value.find("difficulty"); it != value.end() && it->is_string())
                level.difficulty = it->get<std::string>();
            if (const auto waves = value.find("waves"); waves != value.end() && waves->is_array())
                for (const auto& wave : *waves) {
                    DungeonWave parsed;
                    if (const auto enemies = wave.find("enemies");
                        enemies != wave.end() && enemies->is_array())
                        for (const auto& enemy : *enemies)
                            if (enemy.is_object())
                                parsed.enemies.push_back(parse_enemy(enemy, reference));
                    if (!parsed.enemies.empty()) level.waves.push_back(std::move(parsed));
                }
            if (level.id != 0) data.levels_[level.id] = std::move(level);
        }
    }

    if (const auto root = read_json(swarfarm_directory / "skills.json", warnings);
        root && root->is_array()) {
        for (const auto& value : *root) {
            if (!value.is_object()) continue;
            auto skill = parse_skill(value);
            if (skill.id != 0) data.skills_[skill.id] = std::move(skill);
        }
    }

    // Corrections manuelles, appliquées après le chargement.
    for (const auto& [id, override] : load_skill_overrides(overrides, warnings)) {
        const auto it = data.skills_.find(id);
        if (it == data.skills_.end()) {
            warn(warnings, "Overrides : compétence " + std::to_string(id) +
                               " absente de skills.json, ignorée");
            continue;
        }
        apply_skill_override(it->second, override);
        ++data.overrides_applied_;
    }
    return data;
}

const Dungeon* DungeonData::dungeon(int id) const {
    const auto it = dungeon_index_.find(id);
    return it == dungeon_index_.end() ? nullptr : &dungeons_[it->second];
}

const DungeonLevel* DungeonData::level(int id) const {
    const auto it = levels_.find(id);
    return it == levels_.end() ? nullptr : &it->second;
}

const SkillData* DungeonData::skill(int id) const {
    const auto it = skills_.find(id);
    return it == skills_.end() ? nullptr : &it->second;
}

bool DungeonData::has_wave_data(const Dungeon& dungeon) const {
    for (const int id : dungeon.level_ids)
        if (const auto* found = level(id); found && found->has_waves()) return true;
    return false;
}

}  // namespace swcore
