#include "swcore/reference_db.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <utility>

namespace swcore {
namespace {

using json = nlohmann::json;

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

template <typename T>
T read(const json& object, const char* field, T fallback,
       const WarningSink& warnings) {
    const auto it = object.find(field);
    if (it == object.end()) {
        warn(warnings, std::string("ReferenceDB: missing field '") + field + "'");
        return fallback;
    }
    try {
        return it->get<T>();
    } catch (const json::exception& error) {
        warn(warnings, std::string("ReferenceDB: invalid field '") + field +
                          "': " + error.what());
        return fallback;
    }
}

// Stat optionnelle : null => absente sans avertissement (recensée par la base) ;
// champ manquant ou invalide => absente avec avertissement.
std::optional<int> read_optional_stat(const json& object, const char* field,
                                      const WarningSink& warnings) {
    const auto it = object.find(field);
    if (it == object.end()) {
        warn(warnings, std::string("ReferenceDB: missing field '") + field + "'");
        return std::nullopt;
    }
    if (it->is_null()) return std::nullopt;
    try {
        return it->get<int>();
    } catch (const json::exception& error) {
        warn(warnings, std::string("ReferenceDB: invalid field '") + field +
                          "': " + error.what());
        return std::nullopt;
    }
}

Element parse_element(const json& object, const WarningSink& warnings) {
    const auto value = read<std::string>(object, "element", "", warnings);
    if (value == "Fire") return Element::Fire;
    if (value == "Water") return Element::Water;
    if (value == "Wind") return Element::Wind;
    if (value == "Light") return Element::Light;
    if (value == "Dark") return Element::Dark;
    if (value == "Pure") return Element::Pure;
    if (!value.empty())
        warn(warnings, "ReferenceDB: unknown element '" + value + "'");
    return Element::Unknown;
}

std::vector<int> parse_skills(const json& object, const WarningSink& warnings) {
    const auto it = object.find("skills");
    if (it == object.end()) {
        warn(warnings, "ReferenceDB: missing field 'skills'");
        return {};
    }
    if (!it->is_array()) {
        warn(warnings, "ReferenceDB: invalid field 'skills': expected array");
        return {};
    }
    std::vector<int> skills;
    for (const auto& value : *it) {
        try {
            skills.push_back(value.get<int>());
        } catch (const json::exception& error) {
            warn(warnings, std::string("ReferenceDB: invalid skill ID: ") +
                              error.what());
        }
    }
    return skills;
}

std::optional<LeaderSkill> parse_leader_skill(const json& object,
                                              const WarningSink& warnings) {
    const auto it = object.find("leader_skill");
    if (it == object.end()) {
        warn(warnings, "ReferenceDB: missing field 'leader_skill'");
        return std::nullopt;
    }
    if (it->is_null()) return std::nullopt;
    if (!it->is_object()) {
        warn(warnings, "ReferenceDB: invalid field 'leader_skill': expected object");
        return std::nullopt;
    }

    LeaderSkill skill;
    skill.id = read<int>(*it, "id", 0, warnings);
    skill.amount = read<int>(*it, "amount", 0, warnings);
    skill.area = read<std::string>(*it, "area", "", warnings);
    skill.attribute = read<std::string>(*it, "attribute", "", warnings);
    const auto element_it = it->find("element");
    if (element_it == it->end()) {
        warn(warnings, "ReferenceDB: missing field 'element'");
    } else if (!element_it->is_null()) {
        try {
            const auto element = element_it->get<std::string>();
            json element_object = {{"element", element}};
            skill.element = parse_element(element_object, warnings);
        } catch (const json::exception& error) {
            warn(warnings, std::string("ReferenceDB: invalid field 'element': ") +
                              error.what());
        }
    }
    return skill;
}

ReferenceMonster parse_monster(const json& object, const WarningSink& warnings) {
    ReferenceMonster monster;
    monster.com2us_id = read<int>(object, "com2us_id", 0, warnings);
    monster.name = read<std::string>(object, "name", "", warnings);
    monster.element = parse_element(object, warnings);
    monster.natural_stars = read<int>(object, "natural_stars", 0, warnings);
    monster.base_stars = read<int>(object, "base_stars", 0, warnings);
    monster.awaken_level = read<int>(object, "awaken_level", 0, warnings);
    monster.can_awaken = read<bool>(object, "can_awaken", false, warnings);
    monster.family_id = read<int>(object, "family_id", 0, warnings);
    monster.base_hp = read_optional_stat(object, "base_hp", warnings);
    monster.base_attack = read_optional_stat(object, "base_attack", warnings);
    monster.base_defense = read_optional_stat(object, "base_defense", warnings);
    monster.speed = read<int>(object, "speed", 0, warnings);
    monster.crit_rate = read<int>(object, "crit_rate", 0, warnings);
    monster.crit_damage = read<int>(object, "crit_damage", 0, warnings);
    monster.resistance = read<int>(object, "resistance", 0, warnings);
    monster.accuracy = read<int>(object, "accuracy", 0, warnings);
    monster.max_lvl_hp = read_optional_stat(object, "max_lvl_hp", warnings);
    monster.max_lvl_attack = read_optional_stat(object, "max_lvl_attack", warnings);
    monster.max_lvl_defense =
        read_optional_stat(object, "max_lvl_defense", warnings);
    monster.skills = parse_skills(object, warnings);
    monster.leader_skill = parse_leader_skill(object, warnings);
    return monster;
}

}  // namespace

ReferenceDB ReferenceDB::load(const std::filesystem::path& path,
                              WarningSink warnings) {
    ReferenceDB database;
    std::ifstream input(path);
    if (!input) {
        warn(warnings, "ReferenceDB: unable to open '" + path.string() + "'");
        return database;
    }

    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        warn(warnings, std::string("ReferenceDB: invalid JSON: ") + error.what());
        return database;
    }
    if (!root.is_array()) {
        warn(warnings, "ReferenceDB: root must be an array");
        return database;
    }

    for (const auto& value : root) {
        if (!value.is_object()) {
            warn(warnings, "ReferenceDB: ignored non-object entry");
            continue;
        }
        auto monster = parse_monster(value, warnings);
        if (monster.com2us_id == 0) {
            warn(warnings, "ReferenceDB: ignored entry without valid com2us_id");
            continue;
        }
        const int id = monster.com2us_id;
        const bool complete = monster.has_stats();
        if (!database.monsters_.emplace(id, std::move(monster)).second)
            warn(warnings, "ReferenceDB: duplicate com2us_id");
        else if (!complete)
            database.without_stats_.push_back(id);
    }
    std::sort(database.without_stats_.begin(), database.without_stats_.end());
    return database;
}

const std::vector<int>& ReferenceDB::monsters_without_stats() const noexcept {
    return without_stats_;
}

const ReferenceMonster* ReferenceDB::find(int com2us_id) const {
    const auto it = monsters_.find(com2us_id);
    return it == monsters_.end() ? nullptr : &it->second;
}

std::size_t ReferenceDB::size() const noexcept {
    return monsters_.size();
}

}  // namespace swcore
