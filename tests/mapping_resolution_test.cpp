#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <string>

namespace {

using json = nlohmann::json;

json load_json(const std::string& path) {
    std::ifstream input(path);
    REQUIRE(input.good());
    json value;
    input >> value;
    return value;
}

void require_ids(const json& table, const std::set<int>& ids) {
    for (const int id : ids)
        REQUIRE(table.contains(std::to_string(id)));
}

}  // namespace

TEST_CASE("SW Exporter mapping resolves all mapping-relevant fixture IDs") {
    const std::string root = SW_PROJECT_SOURCE_DIR;
    const auto export_data = load_json(
        root + "/tests/data/exports/Qui-Gon-21196058-2026-11-02_001212.json");
    const auto mapping = root + "/data/game/";

    std::set<int> rune_sets;
    std::set<int> rune_stats;
    std::set<int> artifact_types;
    std::set<int> artifact_ranks;
    std::set<int> monster_attributes;
    std::set<int> artifact_main_effects;
    std::set<int> artifact_effects;
    for (const auto& unit : export_data.at("unit_list")) {
        monster_attributes.insert(unit.at("attribute").get<int>());
        for (const auto& rune : unit.at("runes")) {
            rune_sets.insert(rune.at("set_id").get<int>());
            rune_stats.insert(rune.at("pri_eff").at(0).get<int>());
            rune_stats.insert(rune.at("prefix_eff").at(0).get<int>());
            for (const auto& effect : rune.at("sec_eff"))
                rune_stats.insert(effect.at(0).get<int>());
        }
        for (const auto& artifact : unit.at("artifacts")) {
            artifact_types.insert(artifact.at("type").get<int>());
            artifact_ranks.insert(artifact.at("rank").get<int>());
            artifact_main_effects.insert(artifact.at("pri_effect").at(0).get<int>());
            for (const auto& effect : artifact.at("sec_effects"))
                artifact_effects.insert(effect.at(0).get<int>());
        }
    }

    require_ids(load_json(mapping + "runes/rune_sets.json"), rune_sets);
    require_ids(load_json(mapping + "runes/rune_effectTypes.json"), rune_stats);
    require_ids(load_json(mapping + "artifacts/artifact_types.json"), artifact_types);
    require_ids(load_json(mapping + "artifacts/artifact_rank.json"), artifact_ranks);
    require_ids(load_json(mapping + "artifacts/artifact_effectTypes_main.json"),
                artifact_main_effects);
    require_ids(load_json(mapping + "artifacts/artifact_effectTypes_sub.json"),
                artifact_effects);
    require_ids(load_json(mapping + "monsters/monster_attributes.json"),
                monster_attributes);
}
