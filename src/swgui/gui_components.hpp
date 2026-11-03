#pragma once

#include "swcore/account.hpp"
#include "swcore/locks.hpp"
#include "swcore/runes.hpp"
#include "swcore/set_effects.hpp"
#include "swcore/solver.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

// Composants d'affichage partagés par les onglets de build et les fiches de
// monstre (aucun calcul : les valeurs viennent de swcore).
namespace swgui {

// Tableau de stats à 3 colonnes de valeurs : gauche, droite [+vert], écart.
struct StatComparison {
    std::array<const char*, 3> headers{};
    std::array<int, 8> left{};
    std::array<int, 8> right{};
    std::array<int, 8> green{};
    std::array<double, 3> left_metrics{};   // DMG, PV eff., PV eff. DEF brisée
    std::array<double, 3> right_metrics{};
    std::optional<double> left_efficiency;
    std::optional<double> right_efficiency;
};

void render_stat_comparison(const StatComparison& stats);

// Carte de rune (réalité, lignes simulées en couleur d'accent).
// Cadenas dessiné (ImDrawList) : aucun / partiel / complet. Vrai si cliqué.
bool lock_button(const char* id, swcore::LockState state, const std::string& tooltip);

// Avec une table de verrous, la carte porte un cadenas ; *lock_clicked si cliqué.
// context_unit : monstre du contexte ; une rune verrouillée pour un autre monstre
// a un état distinct ("verrouillé pour <monstre>").
void render_rune_card(const swcore::RuneView& view, const swcore::RuneMapping& mapping,
                      const swcore::LockTable* locks = nullptr,
                      bool* lock_clicked = nullptr, std::uint64_t context_unit = 0);

// 6 cartes 6-1-2 / 5-4-3 ; un slot absent affiche "vide".
// Retourne le slot (1..6) dont le cadenas a été cliqué, 0 sinon.
int render_rune_cards(const std::array<swcore::RuneView, 6>& runes,
                       const std::array<bool, 6>& present,
                       const swcore::RuneMapping& mapping,
                       const swcore::LockTable* locks = nullptr,
                       std::uint64_t context_unit = 0);

// Artefacts équipés (attribut / archétype, effets) ; "vide" si show_empty.
// Retourne l'id (rid) de l'artefact dont le cadenas a été cliqué, 0 sinon.
std::uint64_t render_artifacts(const swcore::AccountMonster& monster,
                               const std::filesystem::path& root, bool show_empty,
                               const swcore::LockTable* locks = nullptr);

// Bonus d'un set : "2 p. : ATQ +15%" (activations = 0) ou le bonus cumulé.
std::string set_bonus_text(const swcore::SetEffects& effects, int set_id,
                           int activations = 0);

}  // namespace swgui
