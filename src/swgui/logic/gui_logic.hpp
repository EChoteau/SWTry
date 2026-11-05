#pragma once

// Logique de l'interface sans ImGui (testable) : pagination et tri du tableau
// de résultats, vue active (page ou onglet), état des boutons de verrou.

#include "swcore/account/account.hpp"
#include "swcore/account/locks.hpp"
#include "swcore/runes/runes.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace swgui {

// Ligne du tableau de résultats (compacte, ~120 octets).
struct ResultRow {
    std::array<std::uint64_t, 6> rune_ids{};
    swcore::StatValues total;
    std::int64_t effective_hp = 0;
    std::int64_t effective_hp_broken_def = 0;
    std::int64_t expected_damage = 0;
    double efficiency = 0;
    double score = 0;
    std::uint16_t scenario = 0;
    std::uint16_t sets_label = 0;  // index dans la liste des libellés de sets
    std::uint8_t grinds = 0;
    std::uint8_t gems = 0;
    bool compact = false;          // vrai : ScenarioReport::all_builds
    std::uint32_t index = 0;       // index dans builds ou all_builds
};

// Colonnes du tableau de résultats (identifiants de tri stables).
enum ResultColumn {
    kColRank, kColScenario, kColSets, kColScore,
    kColHp, kColAttack, kColDefense, kColSpeed, kColCritRate, kColCritDamage,
    kColResistance, kColAccuracy,
    kColEffectiveHp, kColEffectiveHpBroken, kColDamage, kColEfficiency, kColCraft, kColIds,
    kResultColumnCount
};

using SortKeys = std::vector<std::pair<int, bool>>;  // colonne, croissant

// Ordre de TOUTES les lignes selon les clés ; à clés égales, l'ordre du
// solveur (tri stable sur l'ordre d'origine).
std::vector<std::uint32_t> sorted_result_order(const std::vector<ResultRow>& rows,
                                               const SortKeys& keys,
                                               const std::vector<std::string>& set_labels);

// Ordre filtré sur un scénario (scenario < 0 : tous).
std::vector<std::uint32_t> filter_scenario(const std::vector<std::uint32_t>& order,
                                           const std::vector<ResultRow>& rows, int scenario);

// Pagination classique. Il y a toujours au moins une page (vide si 0 ligne) ;
// la page courante est toujours ramenée dans [1, nombre de pages].
struct Pager {
    std::size_t page_size = 25;
    std::size_t page = 1;  // à partir de 1

    static std::size_t page_count(std::size_t rows, std::size_t page_size);
    void clamp(std::size_t rows);
    void set_page_size(std::size_t size);  // revient à la page 1
    void reset() { page = 1; }             // après un tri ou un filtre
    // Lignes [first, last) de la page courante.
    std::pair<std::size_t, std::size_t> range(std::size_t rows) const;
};

// Numéros de pages à afficher : 1 2 3 4 5 ... N ; 0 = points de suspension.
std::vector<std::size_t> page_buttons(std::size_t current, std::size_t count);

// Vue active : une page du menu de gauche OU un onglet ouvert (uid).
struct ViewState {
    int page = 0;  // dernière page affichée
    int tab = 0;   // onglet actif (0 : aucun, la page est affichée)

    void show_page(int p) {
        page = p;
        tab = 0;
    }
    void show_tab(int uid) { tab = uid; }
    // Fermer l'onglet actif revient à la dernière page affichée.
    void tab_closed(int uid) {
        if (tab == uid) tab = 0;
    }
    bool page_active(int p) const { return tab == 0 && page == p; }
    bool tab_active(int uid) const { return tab != 0 && tab == uid; }
};

// Boutons "Verrouiller les runes / les artefacts" de la fiche monstre :
// objets équipés réservés à CE monstre sur le total équipé.
struct LockScopeState {
    int locked = 0;
    int total = 0;
    bool disabled() const { return total == 0; }
    bool all_locked() const { return total > 0 && locked == total; }
};
LockScopeState lock_scope_state(const swcore::LockTable& locks,
                                const swcore::AccountMonster& monster, swcore::LockKind kind);
// État global (gros cadenas) : runes ET artefacts.
swcore::LockState combined_lock_state(const LockScopeState& runes,
                                      const LockScopeState& artifacts);

}  // namespace swgui
