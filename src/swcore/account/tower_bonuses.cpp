#include "swcore/account/tower_bonuses.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <optional>

namespace swcore {

namespace {

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

std::optional<Element> element_named(const std::string& name) {
    if (name == "Fire") return Element::Fire;
    if (name == "Water") return Element::Water;
    if (name == "Wind") return Element::Wind;
    if (name == "Light") return Element::Light;
    if (name == "Dark") return Element::Dark;
    return std::nullopt;
}

}  // namespace

TowerBonuses load_tower_bonuses(const std::filesystem::path& path, int level,
                                WarningSink warnings) {
    TowerBonuses bonuses;
    bonuses.level = level;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        warn(warnings, "Tours : fichier introuvable : " + path.string() + " (bonus à 0)");
        return bonuses;
    }
    nlohmann::json root;
    try {
        input >> root;
    } catch (const nlohmann::json::exception& error) {
        warn(warnings, std::string("Tours : JSON invalide : ") + error.what());
        return bonuses;
    }
    const auto tables = root.find("tables");
    const auto skills = root.find("skills");
    if (tables == root.end() || skills == root.end() || !tables->is_object() ||
        !skills->is_object()) {
        warn(warnings, "Tours : clés \"tables\" / \"skills\" absentes");
        return bonuses;
    }
    // Valeur d'une table au niveau demandé.
    const auto value = [&](const std::string& table) -> std::optional<double> {
        const auto it = tables->find(table);
        if (it == tables->end() || !it->is_array() || level < 1 ||
            level > static_cast<int>(it->size()))
            return std::nullopt;
        const auto& entry = (*it)[static_cast<std::size_t>(level - 1)];
        return entry.is_number() ? std::optional<double>{entry.get<double>()} : std::nullopt;
    };
    for (const auto& [id, skill] : skills->items()) {
        if (!skill.is_object()) continue;
        const auto table = skill.find("table");
        if (table == skill.end() || !table->is_string()) continue;  // tour inconnue
        const auto amount = value(table->get<std::string>());
        if (!amount) {
            warn(warnings, "Tours : niveau " + std::to_string(level) + " absent de la table " +
                               table->get<std::string>());
            continue;
        }
        const std::string name = table->get<std::string>();
        if (name == "SPD_PCT") bonuses.speed += *amount;
        else if (name == "CRIT_DMG_PCT") bonuses.crit_damage += *amount;
        else if (name == "ATK_PCT") bonuses.attack += *amount;
        else if (name == "DEF_PCT") bonuses.defense += *amount;
        else if (name == "HP_PCT") bonuses.hp += *amount;
        else if (name == "ELEM_ATK_PCT") {
            const auto element = skill.find("element");
            if (element != skill.end() && element->is_string())
                if (const auto parsed = element_named(element->get<std::string>()))
                    bonuses.element_attack[*parsed] += *amount;
        }
    }
    bonuses.loaded = true;
    return bonuses;
}

}  // namespace swcore
