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
    int swarfarm_id = 0;  // champ "id" de SWARFARM (référencé par levels.json)
    std::string name;
    Element element = Element::Unknown;
    std::string archetype;  // SWARFARM "archetype" (Attack, HP...), vide si absent
    int natural_stars = 0;
    int base_stars = 0;
    int awaken_level = 0;
    bool can_awaken = false;
    int family_id = 0;
    // Absentes (nullopt) quand SWARFARM fournit null ou omet le champ.
    std::optional<int> base_hp;
    std::optional<int> base_attack;
    std::optional<int> base_defense;
    int speed = 0;
    int crit_rate = 0;
    int crit_damage = 0;
    int resistance = 0;
    int accuracy = 0;
    std::optional<int> max_lvl_hp;
    std::optional<int> max_lvl_attack;
    std::optional<int> max_lvl_defense;

    bool has_stats() const noexcept {
        return base_hp && base_attack && base_defense && max_lvl_hp &&
               max_lvl_attack && max_lvl_defense;
    }
    std::vector<int> skills;
    std::optional<LeaderSkill> leader_skill;
};

using WarningSink = std::function<void(const std::string&)>;

class ReferenceDB {
public:
    static ReferenceDB load(const std::filesystem::path& path,
                            WarningSink warnings = {});

    const ReferenceMonster* find(int com2us_id) const;
    // Par id SWARFARM (levels.json, configs de teams).
    const ReferenceMonster* find_swarfarm(int swarfarm_id) const;
    std::size_t size() const noexcept;
    // com2us_id des monstres dont au moins une stat de base/niv. max est absente,
    // triés par ordre croissant.
    const std::vector<int>& monsters_without_stats() const noexcept;

private:
    std::unordered_map<int, ReferenceMonster> monsters_;
    std::unordered_map<int, int> by_swarfarm_;  // id SWARFARM -> com2us_id
    std::vector<int> without_stats_;
};

}  // namespace swcore
