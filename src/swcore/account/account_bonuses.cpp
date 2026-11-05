#include "swcore/account/account_bonuses.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <map>
#include <string>
#include <utility>

namespace swcore {
namespace {

using json = nlohmann::json;
using Definitions = std::map<int, std::map<int, AccountBonusDefinition>>;

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

bool read_int(const json& object, const char* field, int& result,
              const WarningSink& warnings) {
    const auto it = object.find(field);
    if (it == object.end()) {
        warn(warnings, std::string("AccountBonuses: missing field '") + field + "'");
        return false;
    }
    try {
        result = it->get<int>();
        return true;
    } catch (const json::exception& error) {
        warn(warnings, std::string("AccountBonuses: invalid field '") + field +
                          "': " + error.what());
        return false;
    }
}

Definitions load_definitions(const json& root, const char* category,
                             const WarningSink& warnings) {
    Definitions result;
    const auto category_it = root.find(category);
    if (category_it == root.end()) return result;
    if (!category_it->is_object()) {
        warn(warnings, std::string("AccountBonuses: category '") + category +
                          "' must be an object");
        return result;
    }
    for (const auto& [id_text, levels] : category_it->items()) {
        if (!levels.is_object()) continue;
        try {
            const int id = std::stoi(id_text);
            for (const auto& [level_text, value] : levels.items()) {
                if (!value.is_object()) continue;
                AccountBonusDefinition definition;
                definition.stat = value.value("stat", "");
                definition.flat = value.value("flat", 0.0);
                definition.percent = value.value("percent", 0.0);
                result[id][std::stoi(level_text)] = definition;
            }
        } catch (const std::exception& error) {
            warn(warnings, std::string("AccountBonuses: invalid mapping ID: ") +
                              error.what());
        }
    }
    return result;
}

void add_bonus(std::vector<AccountBonus>& values, BonusSource source, int id,
               int level, int raw_value, const Definitions& definitions) {
    AccountBonus bonus{source, id, level, raw_value, std::nullopt};
    const auto id_it = definitions.find(id);
    if (id_it != definitions.end()) {
        const auto level_it = id_it->second.find(level);
        if (level_it != id_it->second.end()) bonus.definition = level_it->second;
    }
    values.push_back(std::move(bonus));
}

}  // namespace

AccountBonuses AccountBonuses::load(const std::filesystem::path& export_path,
                                    const std::filesystem::path& mapping_path,
                                    WarningSink warnings) {
    AccountBonuses result;
    std::ifstream export_input(export_path);
    if (!export_input) {
        warn(warnings, "AccountBonuses: unable to open '" + export_path.string() + "'");
        return result;
    }
    json export_data;
    try {
        export_input >> export_data;
    } catch (const json::exception& error) {
        warn(warnings, std::string("AccountBonuses: invalid export JSON: ") +
                          error.what());
        return result;
    }
    std::ifstream mapping_input(mapping_path);
    if (!mapping_input) {
        warn(warnings, "AccountBonuses: unable to open '" + mapping_path.string() + "'");
        return result;
    }
    json mapping;
    try {
        mapping_input >> mapping;
    } catch (const json::exception& error) {
        warn(warnings, std::string("AccountBonuses: invalid mapping JSON: ") +
                          error.what());
        return result;
    }

    const auto decorations = load_definitions(mapping, "decorations", warnings);
    const auto wizard_skills = load_definitions(mapping, "wizard_skills", warnings);
    const auto guild_skills = load_definitions(mapping, "guild_skills", warnings);

    if (const auto list = export_data.find("deco_list");
        list != export_data.end() && list->is_array()) {
        for (const auto& value : *list) {
            int id = 0;
            int level = 0;
            if (read_int(value, "master_id", id, warnings) &&
                read_int(value, "level", level, warnings))
                add_bonus(result.values_, BonusSource::Decoration, id, level, 0,
                          decorations);
        }
    }
    if (const auto list = export_data.find("wizard_skill_list");
        list != export_data.end() && list->is_object()) {
        for (const auto& [unused_key, value] : list->items()) {
            (void)unused_key;
            int id = 0;
            int level = 0;
            if (read_int(value, "skill_id", id, warnings) &&
                read_int(value, "level", level, warnings))
                add_bonus(result.values_, BonusSource::WizardSkill, id, level, 0,
                          wizard_skills);
        }
    }
    const auto guild = export_data.find("guild");
    if (guild != export_data.end() && guild->is_object()) {
        const auto info = guild->find("guild_info");
        if (info != guild->end() && info->is_object()) {
            const auto skills = info->find("skill_info");
            if (skills != info->end() && skills->is_object()) {
                for (const auto& [id_text, levels] : skills->items()) {
                    try {
                        const int id = std::stoi(id_text);
                        if (levels.is_object()) {
                            for (const auto& [level_text, raw] : levels.items())
                                add_bonus(result.values_, BonusSource::GuildSkill, id,
                                          std::stoi(level_text), raw.get<int>(),
                                          guild_skills);
                        } else if (levels.is_array()) {
                            for (std::size_t i = 0; i < levels.size(); ++i)
                                add_bonus(result.values_, BonusSource::GuildSkill, id,
                                          static_cast<int>(i + 1), levels[i].get<int>(),
                                          guild_skills);
                        }
                    } catch (const json::exception& error) {
                        warn(warnings,
                             std::string("AccountBonuses: invalid guild skill: ") +
                                 error.what());
                    } catch (const std::exception& error) {
                        warn(warnings,
                             std::string("AccountBonuses: invalid guild skill ID: ") +
                                 error.what());
                    }
                }
            }
        }
    }
    return result;
}

const std::vector<AccountBonus>& AccountBonuses::values() const noexcept {
    return values_;
}

std::string bonus_source_name(BonusSource source) {
    switch (source) {
    case BonusSource::Decoration: return "decoration";
    case BonusSource::WizardSkill: return "wizard_skill";
    case BonusSource::GuildSkill: return "guild_skill";
    }
    return "unknown";
}

}  // namespace swcore
