#pragma once

#include "data_store.hpp"
#include "gui_logic.hpp"
#include "settings.hpp"

#include "swcore/craft_audit.hpp"
#include "swcore/solve_async.hpp"
#include "swcore/solver.hpp"
#include "swcore/solver_config.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct ImGuiTableSortSpecs;

namespace swgui {

// Ligne du tableau Monstres (possédé ou stocké).
struct MonsterRow {
    std::uint64_t unit_id = 0;  // 0 pour un monstre stocké
    const swcore::AccountMonster* monster = nullptr;  // possédé (données chargées)
    int unit_master_id = 0;
    std::string name;
    std::string folded_name;   // minuscules sans accents (recherche)
    int level = 0;
    int stars = 0;
    swcore::Element element = swcore::Element::Unknown;
    std::string archetype;
    std::array<int, 8> base{};
    std::array<int, 8> current{};
    std::array<long long, 2> base_derived{};     // DMG, PV eff.
    std::array<long long, 2> current_derived{};
    std::optional<double> efficiency;            // moyenne des runes équipées
    int runes = 0;
    int artifacts = 0;
    bool stored = false;
    int quantity = 0;
};

// Onglet fermable de la barre d'onglets du haut : un build (résultat ou build
// sauvegardé) ou la fiche d'un monstre "en l'état".
struct WorkspaceTab {
    enum class Kind { Build, Monster };
    Kind kind = Kind::Build;
    std::string title;
    int uid = 0;
    bool open = true;
    // Monstre affiché : copie de l'export (ou de l'instantané d'un build sauvegardé).
    swcore::AccountMonster target;
    // Build
    swcore::SolverBuild build;
    swcore::SolverConfig config;
    std::string scenario;
    swcore::BuildReport report;
    std::string text;     // même texte que la CLI
    std::string warning;  // bandeau : instantané différent de l'export courant
    // Fiche monstre
    bool stored = false;  // monstre stocké (hôtel scellé)
    int unit_master_id = 0;
    int quantity = 0;
};

// Build sauvegardé dans data/user/saved_builds/.
struct SavedBuild {
    std::filesystem::path file;
    std::string monster;
    std::string scenario;
    std::string date;
    std::string sets;
    double score = 0;
    std::uint64_t unit_id = 0;
    std::array<std::uint64_t, 6> rune_ids{};
    std::array<int, 6> variants{};
};

class App {
public:
    App(DataStore& data, Settings& settings);
    ~App();

    void render();  // une image, sur toute la fenêtre
    void on_drop(const std::vector<std::string>& utf8_paths);
    void load_last_files();
    void save_state();  // config courante (data/user/configs/last.json)

    // --selftest : rend sans fenêtre la fiche de chaque monstre possédé et vérifie
    // qu'elle affiche les mêmes stats que la ligne du tableau. Retourne les erreurs.
    std::vector<std::string> selftest_monster_sheets();
    // --selftest : rend sans fenêtre chaque page avec ces résultats chargés, un
    // onglet de build ouvert puis fermé. N'écrit rien dans data/user.
    std::vector<std::string> selftest_pages(const swcore::SolverReport& report,
                                            const swcore::SolverConfig& config);

private:
    enum class Page { Data, Monsters, Optimizer, SavedBuilds, EquipLocks, Dungeons };

    struct Toast {
        std::string text;
        std::function<void()> undo;
        double until = 0;
    };

    // Coque et page Données (app.cpp)
    void render_menu();
    void render_status_bar();
    void render_data_page();
    void choose_export();
    void choose_storage();
    void load_export(const std::filesystem::path& path);
    void load_storage(const std::filesystem::path& path);
    void set_status(std::string message);
    const swcore::AccountMonster* find_monster(std::uint64_t unit_id) const;
    std::string monster_name(int unit_master_id) const;

    // Verrous (app_locks.cpp) : chaque changement est écrit dans locks.json.
    std::string owner_name(std::uint64_t unit_id) const;
    void set_monster_locks(const swcore::AccountMonster& monster, bool lock, bool runes,
                           bool artifacts);
    void locks_changed();

    // Retour d'action (toast) avec "Annuler" : un clic = une action, sans popup.
    void show_toast(std::string text, std::function<void()> undo = {});
    void render_toast();

    // Page "Donjons" (app_dungeons.cpp) : donjons -> étages -> teams -> team.
    void render_dungeons_page();
    void render_dungeon_list(const DataStore::DungeonState& state);
    void render_dungeon_levels(const DataStore::DungeonState& state,
                               const swcore::Dungeon& dungeon);
    void render_dungeon_level(const DataStore::DungeonState& state,
                              const swcore::Dungeon& dungeon, const swcore::DungeonLevel& level);
    void render_dungeon_team(const DataStore::DungeonState& state,
                             const swcore::TeamConfig& team);

    // Page "Équipement et verrous" (app_equip.cpp).
    void render_equip_locks_page();
    std::string equip_todo_text() const;
    void render_build_actions(WorkspaceTab& tab);  // boutons Équiper / Verrouiller

    // Barre d'onglets du haut et fiches de monstre (app_workspace.cpp)
    void render_tab_strip();
    WorkspaceTab* active_tab();
    void activate_tab(int uid);
    void open_monster_tab(const MonsterRow& row);
    void render_monster_tab(WorkspaceTab& tab);
    void render_monster_lock_panel(const swcore::AccountMonster& monster);

    // Liste des monstres (app_monsters.cpp)
    void render_monsters_list();
    void rebuild_monster_rows();
    void sort_monster_rows(const ImGuiTableSortSpecs* specs);

    // Recherche : paramètres (app_optimizer.cpp)
    void render_search_tab();
    bool section(const char* label);  // en-tête repliable, état mémorisé
    void render_config_files();
    void render_target_and_pool();
    void render_lock_settings();
    void render_scenarios();
    void render_main_stats();
    void render_constraints();
    void render_objective();
    void render_craft(bool gems);
    void render_limits();
    void render_run_controls();
    void start_search();
    void poll_search();
    void reset_config();
    void normalize_config();
    void load_craft_tables();

    // Résultats (app_results.cpp)
    void build_result_rows();
    void render_results();
    void render_pagination();
    void request_result_sort(const ImGuiTableSortSpecs* specs);
    void refresh_result_view();  // filtre de scénario sur l'ordre trié
    void apply_result_sort();    // trie tous les builds selon sort_keys_
    void stop_sort();
    std::string set_label(const std::array<std::uint64_t, 6>& rune_ids);
    void export_results_csv();

    // Onglets de build et builds sauvegardés (app_builds.cpp)
    void open_result(std::size_t row);
    void open_build_tab(const swcore::AccountMonster& target, swcore::SolverBuild build,
                        const swcore::SolverConfig& config, const std::string& scenario,
                        bool save, std::string warning = {});
    void render_build_tab(WorkspaceTab& tab);
    void save_build(const WorkspaceTab& tab);
    void render_saved_builds_page();
    void refresh_saved_builds();
    void open_saved_build(const SavedBuild& saved);

    DataStore& data_;
    Settings& settings_;
    ViewState view_{static_cast<int>(Page::Data), 0};
    Page page() const { return static_cast<Page>(view_.page); }
    void show_page(Page page) { view_.show_page(static_cast<int>(page)); }
    std::string status_;
    bool show_warnings_ = false;

    // Monstres
    std::vector<MonsterRow> monster_rows_;
    std::vector<std::size_t> monster_order_;
    std::size_t monster_generation_ = static_cast<std::size_t>(-1);
    bool monster_storage_rows_ = false;
    bool monster_sort_dirty_ = true;
    bool base_stats_ = false;
    bool include_storage_ = false;
    char monster_search_[64] = {};
    int element_filter_ = 0;
    int archetype_filter_ = 0;
    int stars_filter_ = 0;
    int runes_filter_ = 0;
    int lock_filter_ = 0;  // tous / complets / partiels / aucun
    std::vector<std::uint64_t> selected_monsters_;  // sélection multiple (Ctrl / Maj)
    int last_clicked_row_ = -1;                     // ancre de Maj+clic (ligne visible)

    // Optimiseur
    swcore::SolverConfig config_;
    std::vector<int> main_stat_ids_;     // rune_mainstat.json
    swcore::CraftRanges grind_ranges_;   // grindstone.json
    swcore::CraftRanges gem_ranges_;     // enchanted_gem.json
    std::string config_signature_;
    std::array<std::size_t, 6> candidate_counts_{};
    std::size_t lock_excluded_ = 0;            // runes exclues par verrou (monstre cible)
    std::size_t lock_excluded_artifacts_ = 0;  // artefacts exclus par verrou
    float parameters_height_ = 0;              // zone des paramètres (séparateur)
    std::int64_t last_elapsed_ms_ = 0;         // durée de la dernière recherche
    char config_name_[64] = {};
    char target_filter_[64] = {};
    char locked_filter_[64] = {};
    swcore::AsyncSolver solver_;
    bool solving_ = false;
    std::optional<swcore::SolverReport> report_;
    swcore::SolverConfig report_config_;
    std::uint64_t report_target_ = 0;
    bool report_cancelled_ = false;

    // Résultats
    std::vector<ResultRow> rows_;
    std::vector<std::uint32_t> row_order_;   // tous les builds, tri en cours
    std::vector<std::uint32_t> view_order_;  // après le filtre de scénario
    int scenario_filter_ = -1;               // -1 : tous
    Pager pager_;
    SortKeys sort_keys_{{kColScore, false}};  // tri initial : Score décroissant
    float parameters_content_ = 0;           // hauteur utile des paramètres (image précédente)
    std::vector<std::string> set_labels_;
    std::size_t selected_row_ = static_cast<std::size_t>(-1);
    std::jthread sort_thread_;
    std::atomic<bool> sorting_{false};
    std::mutex sort_mutex_;
    std::optional<std::vector<std::uint32_t>> sorted_order_;

    // Onglets et builds sauvegardés
    std::vector<WorkspaceTab> tabs_;
    int next_tab_uid_ = 1;
    std::vector<SavedBuild> saved_builds_;
    bool saved_loaded_ = false;
    std::size_t delete_armed_ = static_cast<std::size_t>(-1);

    // Page Donjons : sélection courante (0 / vide : niveau du dessus).
    int dungeon_id_ = 0;
    int dungeon_level_id_ = 0;
    std::string dungeon_team_id_;
    char dungeon_filter_[64] = {};

    std::optional<Toast> toast_;
    // Page Équipement et verrous.
    char lock_filter_text_[64] = {};
    int lock_kind_filter_ = 0;  // tous / runes / artefacts
    std::vector<std::uint64_t> selected_locks_;
    bool confirm_reset_equip_ = false;
};

}  // namespace swgui
