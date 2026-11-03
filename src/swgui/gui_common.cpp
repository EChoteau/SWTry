#include "gui_common.hpp"

#include <map>

namespace swgui {

std::string canonical_metric(const std::string& name) {
    static const std::map<std::string, std::string> aliases = {
        {"HP", "hp"},       {"PV", "hp"},          {"ATK", "attack"},
        {"ATQ", "attack"},  {"DEF", "defense"},     {"SPD", "speed"},
        {"VIT", "speed"},   {"CRate", "crit_rate"}, {"CDmg", "crit_damage"},
        {"RES", "resistance"}, {"ACC", "accuracy"}, {"efficiency", "rune_efficiency"}};
    const auto it = aliases.find(name);
    return it == aliases.end() ? name : it->second;
}

const char* element_label(swcore::Element element) {
    switch (element) {
    case swcore::Element::Fire: return "Feu";
    case swcore::Element::Water: return "Eau";
    case swcore::Element::Wind: return "Vent";
    case swcore::Element::Light: return "Lumière";
    case swcore::Element::Dark: return "Ténèbres";
    case swcore::Element::Pure: return "Pur";
    case swcore::Element::Unknown: break;
    }
    return "-";
}

const char* archetype_label(const std::string& archetype) {
    if (archetype == "Attack") return "Attaque";
    if (archetype == "Defense") return "Défense";
    if (archetype == "HP") return "PV";
    if (archetype == "Support") return "Support";
    if (archetype == "Material") return "Matériau";
    return "-";
}

const char* rarity_label(int rarity) {
    switch (rarity) {
    case 1: return "Normal";
    case 2: return "Magique";
    case 3: return "Rare";
    case 4: return "Héroïque";
    case 5: return "Légendaire";
    default: return "?";
    }
}

int stat_at(const swcore::StatValues& values, int index) {
    switch (index) {
    case 0: return values.hp;
    case 1: return values.attack;
    case 2: return values.defense;
    case 3: return values.speed;
    case 4: return values.crit_rate;
    case 5: return values.crit_damage;
    case 6: return values.resistance;
    case 7: return values.accuracy;
    default: return 0;
    }
}

}  // namespace swgui
