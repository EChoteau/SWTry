#pragma once

#include "swcore/account.hpp"

#include <array>
#include <optional>

namespace swgui {

// Stats d'un monstre possédé, calculées une seule fois et partagées par la
// ligne du tableau Monstres et la fiche du monstre (sans ImGui : testable).
struct MonsterStatsView {
    std::array<int, 8> base{};     // stats blanches (sans runes ni artefacts)
    std::array<int, 8> current{};  // fonction de stats de swcore (runes, sets, artefacts)
    std::array<double, 3> base_metrics{};     // DMG, PV eff., PV eff. DEF brisée
    std::array<double, 3> current_metrics{};
    std::optional<double> efficiency;  // moyenne des runes équipées
    std::array<bool, 6> rune_slots{};  // slot 1..6 occupé
    int runes = 0;
    int artifacts = 0;
};

MonsterStatsView monster_stats_view(const swcore::AccountMonster& monster);

}  // namespace swgui
