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

// Cadenas dessiné (ImDrawList) : aucun / partiel / complet. Vrai si cliqué.
bool lock_button(const char* id, swcore::LockState state, const std::string& tooltip);
// Gros cadenas non cliquable (fiche monstre) : ouvert gris, partiel à moitié
// rempli, fermé plein couleur d'accent.
void big_padlock(swcore::LockState state, float size);

// Origine d'une rune pour l'affichage : nom du monstre ou "Inventaire", sans id.
std::string origin_label(const swcore::Rune& rune);

// Carte de rune (réalité, lignes simulées en couleur d'accent). Aucun id
// affiché, aucun verrouillage d'une rune isolée.
void render_rune_card(const swcore::RuneView& view, const swcore::RuneMapping& mapping);

// 6 cartes 6-1-2 / 5-4-3 ; un slot absent affiche "vide".
void render_rune_cards(const std::array<swcore::RuneView, 6>& runes,
                       const std::array<bool, 6>& present,
                       const swcore::RuneMapping& mapping);

// Artefacts équipés (attribut / archétype, effets) ; "vide" si show_empty.
void render_artifacts(const swcore::AccountMonster& monster,
                      const std::filesystem::path& root, bool show_empty);

// Bonus d'un set : "2 p. : ATQ +15%" (activations = 0) ou le bonus cumulé.
std::string set_bonus_text(const swcore::SetEffects& effects, int set_id,
                           int activations = 0);

}  // namespace swgui
