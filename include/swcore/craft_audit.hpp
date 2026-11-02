#pragma once

#include "swcore/runes.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace swcore {

// Plages {min, max} par rareté, pour chaque id de stat (grindstone.json,
// enchanted_gem.json : {"<stat>": {"range": {"<rareté>": {"min", "max"}}}}).
using CraftRanges = std::map<int, std::map<int, std::pair<int, int>>>;

CraftRanges load_craft_ranges(const std::filesystem::path& path,
                              WarningSink warnings = {});

enum class GemConflict {
    MainStat,       // la stat gemmée est la stat principale de la rune
    OtherSubstat,   // la stat gemmée est aussi une autre substat de la rune
};

struct CraftLineIssue {
    std::uint64_t rune_id = 0;
    int stat_id = 0;
    int value = 0;
};

struct GemConflictIssue {
    std::uint64_t rune_id = 0;
    int stat_id = 0;
    GemConflict conflict = GemConflict::MainStat;
};

// Constat sur les meules et gemmes réelles d'un ensemble de runes.
struct CraftAudit {
    std::size_t runes = 0;

    std::size_t ground_lines = 0;                      // lignes avec meule > 0
    std::vector<CraftLineIssue> ground_bad_stat;       // stat hors {1-6, 8}
    std::vector<CraftLineIssue> ground_out_of_range;   // meule hors de toute rareté

    std::size_t gem_lines = 0;                         // lignes enchantées
    std::vector<CraftLineIssue> gem_out_of_range;      // valeur hors de toute rareté
    std::vector<GemConflictIssue> gem_conflicts;       // stat déjà présente
    std::vector<std::uint64_t> runes_with_several_gems;
    std::size_t gem_runes_with_innate = 0;             // gemmes sur rune à innée
    std::size_t gem_same_as_innate = 0;                // gemme = stat de l'innée
    std::size_t gem_with_grind = 0;                    // gemme portant une meule

    std::optional<int> min_level_ground;  // niveau minimum, runes avec meule
    std::optional<int> min_level_gem;     // niveau minimum, runes avec gemme
};

bool is_grindable_stat(int stat_id);

CraftAudit audit_craft(const std::vector<Rune>& runes,
                       const CraftRanges& grindstones, const CraftRanges& gems);

}  // namespace swcore
