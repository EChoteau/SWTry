#include <catch2/catch_test_macros.hpp>

#include "swcore/set_effects.hpp"
#include "swcore/runes.hpp"

#include <string>

TEST_CASE("Set effects load confirmed personal and team bonuses") {
    std::vector<std::string> warnings;
    const auto effects = swcore::SetEffects::load(
        std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping/set_effects.json",
        [&](const std::string& warning) { warnings.push_back(warning); });

    REQUIRE(effects.effects.contains(8));
    CHECK(effects.effects.at(8).pieces_required == 4);
    CHECK(effects.effects.at(8).stat == "attack");
    CHECK(effects.effects.at(8).value == 35);
    CHECK(effects.effects.at(8).type == "personnel");
    REQUIRE(effects.effects.contains(19));
    CHECK(effects.effects.at(19).type == "équipe");
    CHECK(warnings.empty());
}

TEST_CASE("Stat percentage bonuses use ceiling on white stats") {
    const swcore::StatValues white{101, 201, 301, 100, 15, 50, 15, 0};
    const swcore::StatValues percent{1, 1, 1, 0, 0, 0, 0, 0};
    const auto result = swcore::calculate_stat_block(
        white, {}, percent, {}, {}, {});

    CHECK(result.total.hp == 103);
    CHECK(result.total.attack == 204);
    CHECK(result.total.defense == 305);
}

TEST_CASE("Rune mapping classifies HP ATK and DEF percentage IDs") {
    const auto mapping = swcore::RuneMapping::load(
        std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping");
    CHECK(mapping.kind_for_stat(1) == swcore::StatValueKind::Flat);
    CHECK(mapping.kind_for_stat(2) == swcore::StatValueKind::Percent);
    CHECK(mapping.kind_for_stat(3) == swcore::StatValueKind::Flat);
    CHECK(mapping.kind_for_stat(4) == swcore::StatValueKind::Percent);
    CHECK(mapping.kind_for_stat(5) == swcore::StatValueKind::Flat);
    CHECK(mapping.kind_for_stat(6) == swcore::StatValueKind::Percent);
    CHECK(mapping.kind_for_stat(8) == swcore::StatValueKind::Flat);
    CHECK(mapping.kind_for_stat(9) == swcore::StatValueKind::Flat);
    CHECK(mapping.kind_for_stat(10) == swcore::StatValueKind::Flat);
    CHECK(mapping.kind_for_stat(11) == swcore::StatValueKind::Flat);
    CHECK(mapping.kind_for_stat(12) == swcore::StatValueKind::Flat);
}

TEST_CASE("Percentage rune bonuses are calculated from white stats") {
    const swcore::StatValues white{9720, 801, 582, 108, 15, 50, 15, 0};
    const swcore::StatValues fixed{0, 0, 0, 66, 55, 41, 0, 35};
    const swcore::StatValues percent{23, 47, 25, 0, 0, 0, 0, 0};
    const auto result = swcore::calculate_stat_block(
        white, fixed, percent, {}, {}, {});

    CHECK(result.total.hp == 11956);
    CHECK(result.total.attack == 1178);
    CHECK(result.total.defense == 728);
    CHECK(result.total.speed == 174);
    CHECK(result.total.crit_rate == 70);
    CHECK(result.total.crit_damage == 91);
    CHECK(result.total.resistance == 15);
    CHECK(result.total.accuracy == 35);
}

TEST_CASE("Percentage speed bonuses are calculated from white speed") {
    const swcore::StatValues white{0, 0, 0, 108, 0, 0, 0, 0};
    const swcore::StatValues set_percent{0, 0, 0, 25, 0, 0, 0, 0};
    const auto result = swcore::calculate_stat_block(
        white, {}, {}, {}, {}, set_percent);

    CHECK(result.total.speed == 135);
}

TEST_CASE("Set activation counts complete pieces and ignores incomplete sets") {
    const auto effects = swcore::SetEffects::load(
        std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping/set_effects.json");
    const auto activations = swcore::calculate_set_activations(
        {8, 8, 8, 8, 8, 19, 19, 25}, effects);

    REQUIRE(activations.size() == 2);
    for (const auto& activation : activations) {
        if (activation.set_id == 8) CHECK(activation.activations == 1);
        if (activation.set_id == 19) CHECK(activation.activations == 1);
    }
}
