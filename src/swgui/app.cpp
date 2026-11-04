#include "app.hpp"

#include "text_util.hpp"

#include "swcore/build_text.hpp"

#include <imgui.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include <portable-file-dialogs.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <system_error>
#include <utility>

namespace swgui {
namespace fs = std::filesystem;

namespace {

const ImVec4 kErrorColor{0.95f, 0.45f, 0.42f, 1.0f};
const ImVec4 kMutedColor{0.60f, 0.63f, 0.68f, 1.0f};
const ImVec4 kOkColor{0.45f, 0.82f, 0.58f, 1.0f};

// Messages regroupés par type (chiffres remplacés par #), avec leur nombre.
std::vector<std::string> grouped_warnings(const std::vector<std::string>& warnings) {
    std::vector<std::string> order;
    std::map<std::string, std::pair<std::size_t, std::string>> groups;
    for (const auto& message : warnings) {
        std::string kind;
        bool digits = false;
        for (const char c : message) {
            if (c >= '0' && c <= '9') {
                if (!digits) kind += '#';
                digits = true;
            } else {
                kind += c;
                digits = false;
            }
        }
        auto [it, inserted] = groups.try_emplace(kind, 0, message);
        if (inserted) order.push_back(kind);
        ++it->second.first;
    }
    std::vector<std::string> lines;
    for (const auto& kind : order) {
        const auto& [count, example] = groups[kind];
        lines.push_back(count == 1 ? example : std::to_string(count) + " x " + kind);
    }
    return lines;
}

bool looks_like_storage(const fs::path& path) {
    return fold(to_utf8(path.filename())).rfind("storage", 0) == 0;
}

void labeled(const char* label, const std::string& value) {
    ImGui::TextColored(kMutedColor, "%s", label);
    ImGui::SameLine(ImGui::GetFontSize() * 11.0f);
    ImGui::TextUnformatted(value.c_str());
}

}  // namespace

App::App(DataStore& data, Settings& settings) : data_(data), settings_(settings) {
    swcore::text::set_mapping_directory(data_.mapping_directory());
    load_craft_tables();
    // La dernière config est restaurée à la réouverture.
    const fs::path last = data_.user_directory() / "configs" / "last.json";
    std::error_code error;
    if (fs::exists(last, error)) {
        config_ = swcore::SolverConfig::load(last, data_.mapping_directory());
        normalize_config();
    } else {
        reset_config();
    }
}

App::~App() { stop_sort(); }

void App::save_state() {
    const fs::path directory = data_.user_directory() / "configs";
    std::error_code error;
    fs::create_directories(directory, error);
    std::ofstream output(directory / "last.json", std::ios::binary);
    if (output) output << config_.to_json() << '\n';
}

void App::set_status(std::string message) { status_ = std::move(message); }

void App::load_last_files() {
    std::error_code error;
    if (!settings_.last_export.empty() &&
        fs::exists(from_utf8(settings_.last_export), error))
        load_export(from_utf8(settings_.last_export));
    if (!settings_.last_storage.empty() &&
        fs::exists(from_utf8(settings_.last_storage), error))
        load_storage(from_utf8(settings_.last_storage));
}

void App::load_export(const fs::path& path) {
    if (data_.load_export(path)) {
        settings_.last_export = to_utf8(path);
        save_settings(data_.user_directory(), settings_);
        set_status("Export chargé : " + to_utf8(path.filename()));
    } else {
        set_status("Échec du chargement de l'export (voir la page Données).");
    }
}

void App::load_storage(const fs::path& path) {
    if (data_.load_storage(path)) {
        settings_.last_storage = to_utf8(path);
        save_settings(data_.user_directory(), settings_);
        set_status("Stockage chargé : " + to_utf8(path.filename()));
    } else {
        set_status("Échec du chargement du stockage (voir la page Données).");
    }
}

void App::choose_export() {
    const auto selection =
        pfd::open_file("Choisir un export SW Exporter", to_utf8(data_.root()),
                       {"Export JSON", "*.json", "Tous les fichiers", "*"})
            .result();
    if (!selection.empty()) load_export(from_utf8(selection.front()));
}

void App::choose_storage() {
    const auto selection =
        pfd::open_file("Choisir un dump de stockage", to_utf8(data_.root()),
                       {"Stockage JSON", "storage-*.json", "Tous les fichiers", "*"})
            .result();
    if (!selection.empty()) load_storage(from_utf8(selection.front()));
}

const swcore::AccountMonster* App::find_monster(std::uint64_t unit_id) const {
    const auto it = std::find_if(data_.monsters.begin(), data_.monsters.end(),
                                 [&](const swcore::AccountMonster& monster) {
                                     return monster.unit_id == unit_id;
                                 });
    return it == data_.monsters.end() ? nullptr : &*it;
}

std::string App::monster_name(int unit_master_id) const {
    const auto* reference = data_.reference.find(unit_master_id);
    return reference ? reference->name : swcore::text::unknown(unit_master_id);
}

void App::on_drop(const std::vector<std::string>& utf8_paths) {
    for (const auto& text : utf8_paths) {
        const fs::path path = from_utf8(text);
        if (path.extension() != ".json") {
            set_status("Fichier ignoré (pas un .json) : " + to_utf8(path.filename()));
            continue;
        }
        if (looks_like_storage(path)) load_storage(path);
        else load_export(path);
    }
}

void App::render() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float line = ImGui::GetFrameHeightWithSpacing();
    const float status_height = show_warnings_ ? line * 9.0f : line * 1.4f;
    const float menu_width = ImGui::GetFontSize() * 12.0f;

    ImGui::BeginChild("##menu", ImVec2(menu_width, -status_height),
                      ImGuiChildFlags_Borders);
    render_menu();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##page", ImVec2(0, -status_height), ImGuiChildFlags_Borders);
    poll_search();  // la recherche avance quelle que soit la page affichée
    // Barre du haut : uniquement les onglets fermables ouverts (masquée sinon).
    render_tab_strip();
    ImGui::BeginChild("##view", ImVec2(0, 0));
    if (WorkspaceTab* tab = active_tab()) {
        if (tab->kind == WorkspaceTab::Kind::Monster) render_monster_tab(*tab);
        else render_build_tab(*tab);
    } else {
        const bool needs_export = page() == Page::Monsters || page() == Page::Optimizer;
        if (needs_export && !data_.export_file.loaded) {
            ImGui::TextColored(kMutedColor, "Chargez un export dans la page Données.");
        } else {
            switch (page()) {
            case Page::Data: render_data_page(); break;
            case Page::Monsters: render_monsters_list(); break;
            case Page::Optimizer: render_search_tab(); break;
            case Page::SavedBuilds: render_saved_builds_page(); break;
            case Page::EquipLocks: render_equip_locks_page(); break;
            case Page::Dungeons: render_dungeons_page(); break;
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
    render_status_bar();
    ImGui::End();
}

void App::render_menu() {
    const float height = ImGui::GetFrameHeight() * 1.4f;
    const std::pair<Page, const char*> entries[] = {
        {Page::Data, "Données"},
        {Page::Monsters, "Monstres"},
        {Page::Optimizer, "Optimiseur"},
        {Page::Dungeons, "Donjons"},
        {Page::SavedBuilds, "Builds sauvegardés"},
        {Page::EquipLocks, "Équipement et verrous"}};
    ImGui::Spacing();
    // Seule navigation entre pages ; mise en évidence seulement si la page est
    // affichée (pas quand un onglet est actif). Les onglets restent ouverts.
    for (const auto& [entry, label] : entries)
        if (ImGui::Selectable(label, view_.page_active(static_cast<int>(entry)), 0,
                              ImVec2(0, height)))
            show_page(entry);
}

void App::render_status_bar() {
    const auto lines = grouped_warnings(data_.warnings);
    // Retour de la dernière action (avec "Annuler"), sinon le message d'état.
    if (toast_ && ImGui::GetTime() < toast_->until) {
        render_toast();
    } else {
        toast_.reset();
        ImGui::TextUnformatted(status_.empty() ? "Prêt." : status_.c_str());
    }
    if (!lines.empty()) {
        const std::string label = (show_warnings_ ? "Masquer les avertissements ("
                                                  : "Avertissements (") +
                                  std::to_string(lines.size()) + ")";
        const float width = ImGui::CalcTextSize(label.c_str()).x +
                            ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
        if (ImGui::SmallButton(label.c_str())) show_warnings_ = !show_warnings_;
    }
    if (show_warnings_ && !lines.empty()) {
        ImGui::BeginChild("##warnings", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (const auto& message : lines) ImGui::TextWrapped("%s", message.c_str());
        ImGui::EndChild();
    }
}

void App::render_data_page() {
    ImGui::TextUnformatted("Données");
    ImGui::Separator();
    ImGui::Spacing();

    const auto file_block = [&](const LoadedFile& file) {
        if (file.path.empty()) {
            ImGui::TextColored(kMutedColor, "Aucun fichier chargé.");
            return false;
        }
        labeled("Fichier", to_utf8(file.path.filename()));
        labeled("Dossier", to_utf8(file.path.parent_path()));
        labeled("Modifié le", file.modified.empty() ? "-" : file.modified);
        if (!file.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
            ImGui::TextWrapped("Erreur de lecture : %s", file.error.c_str());
            ImGui::PopStyleColor();
            return false;
        }
        return file.loaded;
    };

    ImGui::SeparatorText("Export SW Exporter (.json)");
    if (ImGui::Button("Charger un export...")) choose_export();
    ImGui::SameLine();
    ImGui::TextColored(kMutedColor, "ou glissez-déposez le fichier dans la fenêtre");
    if (file_block(data_.export_file)) {
        labeled("Monstres", group_digits(static_cast<long long>(data_.monsters.size())));
        labeled("Runes", group_digits(static_cast<long long>(data_.runes.size())));
        labeled("Artefacts", group_digits(static_cast<long long>(data_.artifact_count)));
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Stockage (storage-*.json)");
    if (ImGui::Button("Charger un stockage...")) choose_storage();
    ImGui::SameLine();
    ImGui::TextColored(kMutedColor,
                       "un fichier dont le nom commence par \"storage\" est reconnu au dépôt");
    if (file_block(data_.storage_file)) {
        labeled("Monstres stockés",
                group_digits(static_cast<long long>(data_.storage.totalQuantity())));
        labeled("Entrées distinctes",
                group_digits(static_cast<long long>(data_.storage.distinctEntryCount())));
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Données SWARFARM");
    if (data_.swarfarm.files == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextWrapped("Aucune donnée dans data/swarfarm : lancez swsync pour les "
                           "télécharger.");
        ImGui::PopStyleColor();
    } else {
        labeled("Fichiers", std::to_string(data_.swarfarm.files));
        labeled("Mis à jour le", data_.swarfarm.newest);
        labeled("monsters.json", data_.swarfarm.monsters ? "présent" : "absent");
        if (data_.swarfarm.monsters)
            labeled("Monstres connus",
                    group_digits(static_cast<long long>(data_.reference.size())));
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Recharger les fichiers")) {
        if (!data_.export_file.path.empty()) load_export(data_.export_file.path);
        if (!data_.storage_file.path.empty()) load_storage(data_.storage_file.path);
        data_.refresh_swarfarm();
    }
    if (data_.export_file.loaded) {
        ImGui::SameLine();
        ImGui::TextColored(kOkColor, "Données prêtes.");
    }
}

}  // namespace swgui
