#include <catch2/catch_test_macros.hpp>

#include "swcore/craft_audit.hpp"

#include <string>
#include <vector>

namespace {

swcore::SecondaryStat line(int stat, int value, bool enchanted, int grind) {
    swcore::SecondaryStat result;
    result.value = {swcore::StatType::Unknown, swcore::StatValueKind::Flat, stat,
                    value};
    result.enchanted = enchanted;
    result.grind = grind;
    return result;
}

swcore::Rune rune(std::uint64_t id, int level, int primary,
                  std::vector<swcore::SecondaryStat> lines, int innate = 0) {
    swcore::Rune result;
    result.rune_id = id;
    result.upgrade_level = level;
    result.primary = swcore::StatValue{swcore::StatType::Unknown,
                                       swcore::StatValueKind::Flat, primary, 10};
    if (innate != 0)
        result.innate = swcore::StatValue{swcore::StatType::Unknown,
                                          swcore::StatValueKind::Flat, innate, 5};
    result.secondary = std::move(lines);
    return result;
}

}  // namespace

TEST_CASE("Craft audit checks grinds, gems and gem stat conflicts") {
    const std::string mapping = std::string{SW_PROJECT_SOURCE_DIR} + "/data/mapping";
    const auto grindstones = swcore::load_craft_ranges(mapping + "/grindstone.json");
    const auto gems = swcore::load_craft_ranges(mapping + "/enchanted_gem.json");
    REQUIRE(grindstones.contains(8));
    REQUIRE(gems.contains(8));
    const int grind_max = grindstones.at(8).rbegin()->second.second;
    const int gem_max = gems.at(8).rbegin()->second.second;

    const std::vector<swcore::Rune> runes{
        // Meule VIT valide (rune +12), gemme VIT valide portant aussi une meule.
        rune(1, 12, 3, {line(8, 10, false, grind_max), line(9, 5, false, 0)}),
        rune(2, 15, 3, {line(8, gem_max, true, grind_max)}, 8),
        // Meule sur Tx crit. (9) : jamais meulable.
        rune(3, 15, 3, {line(9, 5, false, 3)}),
        // Gemme VIT hors plage, sur une rune dont la principale est VIT.
        rune(4, 15, 8, {line(8, gem_max + 50, true, 0)}),
        // Gemme VIT alors qu'une autre substat est déjà VIT ; deux gemmes.
        rune(5, 9, 3, {line(8, gem_max, true, 0), line(8, 5, false, 0),
                       line(4, 5, true, 0)}),
    };
    const auto audit = swcore::audit_craft(runes, grindstones, gems);

    CHECK(audit.runes == 5);
    CHECK(audit.ground_lines == 3);
    REQUIRE(audit.ground_bad_stat.size() == 1);
    CHECK(audit.ground_bad_stat[0].rune_id == 3);
    CHECK(audit.ground_out_of_range.empty());
    CHECK(audit.gem_lines == 4);
    REQUIRE(audit.gem_out_of_range.size() >= 1);
    CHECK(audit.gem_out_of_range[0].rune_id == 4);
    REQUIRE(audit.gem_conflicts.size() == 2);
    CHECK(audit.gem_conflicts[0].rune_id == 4);
    CHECK(audit.gem_conflicts[0].conflict == swcore::GemConflict::MainStat);
    CHECK(audit.gem_conflicts[1].rune_id == 5);
    CHECK(audit.gem_conflicts[1].conflict == swcore::GemConflict::OtherSubstat);
    CHECK(audit.runes_with_several_gems == std::vector<std::uint64_t>{5});
    CHECK(audit.gem_runes_with_innate == 1);
    CHECK(audit.gem_same_as_innate == 1);
    CHECK(audit.gem_with_grind == 1);
    CHECK(audit.min_level_ground == 12);
    CHECK(audit.min_level_gem == 9);
}
