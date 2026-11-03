#pragma once

#include "swcore/account.hpp"
#include "swcore/runes.hpp"

#include <nlohmann/json.hpp>

// Instantané autonome des builds sauvegardés : runes, monstre et artefacts
// complets, relus sans dépendre de l'export courant.
namespace swgui {

nlohmann::json rune_to_json(const swcore::Rune& rune);
swcore::Rune rune_from_json(const nlohmann::json& value);

// Monstre : stats blanches, niveau, étoiles, runes équipées, artefacts.
nlohmann::json monster_to_json(const swcore::AccountMonster& monster);
swcore::AccountMonster monster_from_json(const nlohmann::json& value);

// Même rune en jeu (set, slot, niveau, étoiles, principale, innée, substats,
// meules, gemmes) : sert à signaler un export qui a changé depuis la sauvegarde.
bool same_rune(const swcore::Rune& left, const swcore::Rune& right);

}  // namespace swgui
