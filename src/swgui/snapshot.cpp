#include "snapshot.hpp"

namespace swgui {

using json = nlohmann::json;

namespace {

json stat_to_json(const swcore::StatValue& value) {
    return {{"none", value.stat == swcore::StatType::None},
            {"kind", static_cast<int>(value.kind)},
            {"stat_id", value.stat_id},
            {"value", value.value}};
}

swcore::StatValue stat_from_json(const json& value) {
    swcore::StatValue stat;
    stat.stat = value.value("none", false) ? swcore::StatType::None : swcore::StatType::Unknown;
    stat.kind = static_cast<swcore::StatValueKind>(value.value("kind", 0));
    stat.stat_id = value.value("stat_id", 0);
    stat.value = value.value("value", 0);
    return stat;
}

json optional_stat_to_json(const std::optional<swcore::StatValue>& value) {
    return value ? stat_to_json(*value) : json(nullptr);
}

std::optional<swcore::StatValue> optional_stat_from_json(const json& value) {
    if (value.is_null()) return std::nullopt;
    return stat_from_json(value);
}

bool same_stat(const std::optional<swcore::StatValue>& a,
               const std::optional<swcore::StatValue>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    return (a->stat == swcore::StatType::None) == (b->stat == swcore::StatType::None) &&
           a->stat_id == b->stat_id && a->value == b->value;
}

}  // namespace

json rune_to_json(const swcore::Rune& rune) {
    json secondary = json::array();
    for (const auto& line : rune.secondary)
        secondary.push_back({{"stat", stat_to_json(line.value)},
                             {"enchanted", line.enchanted},
                             {"grind", line.grind}});
    json grinds = json::array();
    for (const auto& grind : rune.applied_grindstones)
        grinds.push_back({grind.stat_id, grind.value, grind.previous, grind.rarity, grind.line});
    json gems = json::array();
    for (const auto& gem : rune.applied_gems)
        gems.push_back({gem.line, gem.old_stat_id, gem.old_value, gem.old_grind,
                        gem.old_enchanted, gem.stat_id, gem.value, gem.rarity});
    return {{"rune_id", rune.rune_id},
            {"slot", rune.slot},
            {"set_id", rune.set_id},
            {"rank", rune.rank},
            {"stars", rune.stars},
            {"upgrade_level", rune.upgrade_level},
            {"primary", optional_stat_to_json(rune.primary)},
            {"innate", optional_stat_to_json(rune.innate)},
            {"secondary", secondary},
            {"occupancy", static_cast<int>(rune.occupancy)},
            {"occupied_id", rune.occupied_id},
            {"extra", rune.extra},
            {"source_unit_id", rune.source_unit_id},
            {"source_name", rune.source_name},
            {"applied_grindstones", grinds},
            {"applied_gems", gems}};
}

swcore::Rune rune_from_json(const json& value) {
    swcore::Rune rune;
    rune.rune_id = value.value("rune_id", std::uint64_t{0});
    rune.slot = value.value("slot", 0);
    rune.set_id = value.value("set_id", 0);
    rune.rank = value.value("rank", 0);
    rune.stars = value.value("stars", 0);
    rune.upgrade_level = value.value("upgrade_level", 0);
    if (value.contains("primary")) rune.primary = optional_stat_from_json(value.at("primary"));
    if (value.contains("innate")) rune.innate = optional_stat_from_json(value.at("innate"));
    for (const auto& line : value.value("secondary", json::array())) {
        swcore::SecondaryStat secondary;
        secondary.value = stat_from_json(line.at("stat"));
        secondary.enchanted = line.value("enchanted", false);
        secondary.grind = line.value("grind", 0);
        rune.secondary.push_back(secondary);
    }
    rune.occupancy = static_cast<swcore::RuneOccupancy>(value.value("occupancy", 0));
    rune.occupied_id = value.value("occupied_id", std::uint64_t{0});
    rune.extra = value.value("extra", 0);
    rune.source_unit_id = value.value("source_unit_id", std::uint64_t{0});
    rune.source_name = value.value("source_name", std::string{});
    for (const auto& grind : value.value("applied_grindstones", json::array()))
        rune.applied_grindstones.push_back({grind.at(0).get<int>(), grind.at(1).get<int>(),
                                            grind.at(2).get<int>(), grind.at(3).get<int>(),
                                            grind.at(4).get<int>()});
    for (const auto& gem : value.value("applied_gems", json::array()))
        rune.applied_gems.push_back({gem.at(0).get<int>(), gem.at(1).get<int>(),
                                     gem.at(2).get<int>(), gem.at(3).get<int>(),
                                     gem.at(4).get<bool>(), gem.at(5).get<int>(),
                                     gem.at(6).get<int>(), gem.at(7).get<int>()});
    return rune;
}

json monster_to_json(const swcore::AccountMonster& monster) {
    json runes = json::array();
    for (const auto& rune : monster.runes) runes.push_back(rune_to_json(rune));
    json artifacts = json::array();
    for (const auto& artifact : monster.artifacts)
        artifacts.push_back({{"rid", artifact.rid},
                             {"slot", artifact.slot},
                             {"type", artifact.type},
                             {"attribute", artifact.attribute},
                             {"unit_style", artifact.unit_style},
                             {"rank", artifact.rank},
                             {"level", artifact.level},
                             {"primary", artifact.primary},
                             {"secondary", artifact.secondary},
                             {"occupied_id", artifact.occupied_id}});
    return {{"unit_id", monster.unit_id},
            {"unit_master_id", monster.unit_master_id},
            {"level", monster.level},
            {"stars", monster.stars},
            {"attribute", monster.attribute},
            {"con", monster.con},
            {"attack", monster.attack},
            {"defense", monster.defense},
            {"speed", monster.speed},
            {"resistance", monster.resistance},
            {"accuracy", monster.accuracy},
            {"crit_rate", monster.crit_rate},
            {"crit_damage", monster.crit_damage},
            {"runes", runes},
            {"artifacts", artifacts}};
}

swcore::AccountMonster monster_from_json(const json& value) {
    swcore::AccountMonster monster;
    monster.unit_id = value.value("unit_id", std::uint64_t{0});
    monster.unit_master_id = value.value("unit_master_id", 0);
    monster.level = value.value("level", 0);
    monster.stars = value.value("stars", 0);
    monster.attribute = value.value("attribute", 0);
    monster.con = value.value("con", 0);
    monster.attack = value.value("attack", 0);
    monster.defense = value.value("defense", 0);
    monster.speed = value.value("speed", 0);
    monster.resistance = value.value("resistance", 0);
    monster.accuracy = value.value("accuracy", 0);
    monster.crit_rate = value.value("crit_rate", 0);
    monster.crit_damage = value.value("crit_damage", 0);
    for (const auto& rune : value.value("runes", json::array()))
        monster.runes.push_back(rune_from_json(rune));
    for (const auto& item : value.value("artifacts", json::array())) {
        swcore::Artifact artifact;
        artifact.rid = item.value("rid", std::uint64_t{0});
        artifact.slot = item.value("slot", 0);
        artifact.type = item.value("type", 0);
        artifact.attribute = item.value("attribute", 0);
        artifact.unit_style = item.value("unit_style", 0);
        artifact.rank = item.value("rank", 0);
        artifact.level = item.value("level", 0);
        artifact.primary = item.at("primary").get<std::array<double, 5>>();
        artifact.secondary =
            item.at("secondary").get<std::vector<std::array<double, 5>>>();
        artifact.occupied_id = item.value("occupied_id", std::uint64_t{0});
        monster.artifacts.push_back(artifact);
    }
    return monster;
}

bool same_rune(const swcore::Rune& left, const swcore::Rune& right) {
    if (left.rune_id != right.rune_id || left.slot != right.slot ||
        left.set_id != right.set_id || left.stars != right.stars ||
        left.upgrade_level != right.upgrade_level ||
        !same_stat(left.primary, right.primary) || !same_stat(left.innate, right.innate) ||
        left.secondary.size() != right.secondary.size())
        return false;
    for (std::size_t i = 0; i < left.secondary.size(); ++i) {
        const auto& a = left.secondary[i];
        const auto& b = right.secondary[i];
        if (a.value.stat_id != b.value.stat_id || a.value.value != b.value.value ||
            a.grind != b.grind || a.enchanted != b.enchanted)
            return false;
    }
    return true;
}

}  // namespace swgui
