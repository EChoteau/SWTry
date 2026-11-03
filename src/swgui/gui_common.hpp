#pragma once

#include "swcore/reference_db.hpp"
#include "swcore/set_effects.hpp"

#include <imgui.h>

#include <string>

namespace swgui {

// Métriques acceptées par le solveur (noms canoniques) et libellés CLI.
// rune_efficiency = efficacité moyenne des 6 runes (projection et craft inclus).
struct MetricInfo {
    const char* key;
    const char* label;
};
inline constexpr MetricInfo kMetrics[] = {
    {"hp", "PV"},
    {"attack", "ATQ"},
    {"defense", "DEF"},
    {"speed", "VIT"},
    {"crit_rate", "Tx crit."},
    {"crit_damage", "Dgts crit."},
    {"resistance", "RES"},
    {"accuracy", "Préc."},
    {"effective_hp", "PV eff."},
    {"effective_hp_broken_def", "PV eff. DEF brisée"},
    {"expected_damage", "DMG"},
    {"rune_efficiency", "Efficacité"},
};

// Nom canonique d'une métrique (les configs CLI acceptent aussi SPD, ATK...).
std::string canonical_metric(const std::string& name);

// Libellés français (affichage uniquement).
const char* element_label(swcore::Element element);
const char* archetype_label(const std::string& archetype);
const char* rarity_label(int rarity);  // 1-5 : Normal ... Légendaire

// Valeur d'une stat par index (0..7 : PV, ATQ, DEF, VIT, TC, DC, RES, PRÉC).
int stat_at(const swcore::StatValues& values, int index);

namespace color {
inline const ImVec4 muted{0.60f, 0.63f, 0.68f, 1.0f};
inline const ImVec4 error{0.95f, 0.45f, 0.42f, 1.0f};
inline const ImVec4 positive{0.45f, 0.82f, 0.58f, 1.0f};
inline const ImVec4 negative{0.95f, 0.45f, 0.42f, 1.0f};
inline const ImVec4 accent{0.40f, 0.78f, 0.88f, 1.0f};
inline const ImVec4 warning{0.95f, 0.78f, 0.38f, 1.0f};
}  // namespace color

}  // namespace swgui
