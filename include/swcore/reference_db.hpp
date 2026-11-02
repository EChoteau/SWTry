#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace swcore {

enum class Element {
    Unknown,
    Fire,
    Water,
    Wind,
    Light,
    Dark,
    Pure,
};

struct LeaderSkill {
    int id = 0;
    int amount = 0;
    std::string area;
    std::string attribute;
    std::optional<Element> element;
};

struct ReferenceMonster {
    int com2us_id = 0;
    std::string name;
    Element element = Element::Unknown;
    int natural_stars = 0;
    int base_stars = 0;
    int awaken_level = 0;
    bool can_awaken = false;
    int family_id = 0;
    int base_hp = 0;
    int base_attack = 0;
    int base_defense = 0;
    int speed = 0;
    int crit_rate = 0;
    int crit_damage = 0;
    int resistance = 0;
    int accuracy = 0;
    int max_lvl_hp = 0;
    int max_lvl_attack = 0;
    int max_lvl_defense = 0;
    std::vector<int> skills;
    std::optional<LeaderSkill> leader_skill;
};

using WarningSink = std::function<void(const std::string&)>;

class ReferenceDB {
public:
    static ReferenceDB load(const std::filesystem::path& path,
                            WarningSink warnings = {});

    const ReferenceMonster* find(int com2us_id) const;
    std::size_t size() const noexcept;

private:
    std::unordered_map<int, ReferenceMonster> monsters_;
};

}  // namespace swcore
