// Panneau de résultats de l'Optimiseur : lignes compactes, tri de TOUS les
// builds (hors du thread UI au-delà de 50 000 lignes), filtre de scénario,
// pagination (seule la page affichée est dessinée), export CSV dans l'ordre
// du tri en cours (même format que swcli --export).

#include "app.hpp"
#include "gui_common.hpp"
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
#include <map>
#include <numeric>
#include <unordered_map>
#include <utility>

namespace swgui {

namespace {

constexpr std::size_t kBackgroundSortRows = 50'000;
constexpr std::size_t kPageSizes[] = {10, 25, 50, 100};

std::string ids_text(const std::array<std::uint64_t, 6>& ids) {
    std::string text;
    for (std::size_t i = 0; i < ids.size(); ++i)
        text += (i ? " " : "") + std::to_string(ids[i]);
    return text;
}

}  // namespace

std::string App::set_label(const std::array<std::uint64_t, 6>& rune_ids) {
    static std::unordered_map<std::uint64_t, int> set_of;
    static std::size_t generation = static_cast<std::size_t>(-1);
    if (generation != data_.generation) {
        set_of.clear();
        for (const auto& rune : data_.runes) set_of[rune.rune_id] = rune.set_id;
        generation = data_.generation;
    }
    std::map<int, int> counts;
    for (const auto id : rune_ids)
        if (const auto it = set_of.find(id); it != set_of.end()) ++counts[it->second];
    std::vector<std::pair<int, int>> ordered(counts.begin(), counts.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string text;
    for (const auto& [set_id, count] : ordered)
        text += (text.empty() ? "" : " + ") +
                swcore::text::set_name(data_.mapping, set_id) + " " + std::to_string(count);
    return text;
}

void App::build_result_rows() {
    stop_sort();
    rows_.clear();
    set_labels_.clear();
    selected_row_ = static_cast<std::size_t>(-1);
    scenario_filter_ = -1;
    pager_.reset();
    if (!report_) {
        row_order_.clear();
        view_order_.clear();
        return;
    }
    std::map<std::string, std::uint16_t> label_index;
    const auto intern = [&](const std::string& label) {
        const auto [it, inserted] =
            label_index.try_emplace(label, static_cast<std::uint16_t>(set_labels_.size()));
        if (inserted) set_labels_.push_back(label);
        return it->second;
    };
    for (std::size_t s = 0; s < report_->scenarios.size(); ++s) {
        const auto& scenario = report_->scenarios[s];
        if (scenario.all_builds_mode) {
            rows_.reserve(rows_.size() + scenario.all_builds.size());
            for (std::size_t i = 0; i < scenario.all_builds.size(); ++i) {
                const auto& build = scenario.all_builds[i];
                ResultRow row;
                row.rune_ids = build.rune_ids;
                row.total = build.total;
                const auto metrics = swcore::calculate_derived_metrics(build.total);
                row.effective_hp = static_cast<std::int64_t>(build.effective_hp);
                row.effective_hp_broken_def =
                    static_cast<std::int64_t>(metrics.effective_hp_broken_def);
                row.expected_damage = static_cast<std::int64_t>(metrics.expected_damage);
                row.efficiency = build.efficiency;
                row.score = build.score;
                row.grinds = static_cast<std::uint8_t>(build.grind_cost);
                row.gems = static_cast<std::uint8_t>(build.gem_cost);
                row.scenario = static_cast<std::uint16_t>(s);
                row.sets_label = intern(set_label(build.rune_ids));
                row.compact = true;
                row.index = static_cast<std::uint32_t>(i);
                rows_.push_back(row);
            }
            continue;
        }
        for (std::size_t i = 0; i < scenario.builds.size(); ++i) {
            const auto& build = scenario.builds[i];
            ResultRow row;
            for (std::size_t slot = 0; slot < 6; ++slot)
                row.rune_ids[slot] = build.runes[slot].rune_id;
            row.total = build.stats.total;
            row.effective_hp = static_cast<std::int64_t>(build.metrics.effective_hp);
            row.effective_hp_broken_def =
                static_cast<std::int64_t>(build.metrics.effective_hp_broken_def);
            row.expected_damage = static_cast<std::int64_t>(build.metrics.expected_damage);
            row.efficiency = build.efficiency;
            row.score = build.score;
            row.grinds = static_cast<std::uint8_t>(build.grind_cost);
            row.gems = static_cast<std::uint8_t>(build.gem_cost);
            row.scenario = static_cast<std::uint16_t>(s);
            row.sets_label = intern(set_label(row.rune_ids));
            row.index = static_cast<std::uint32_t>(i);
            rows_.push_back(row);
        }
    }
    // Ordre du solveur, puis le tri en cours du tableau.
    row_order_.resize(rows_.size());
    std::iota(row_order_.begin(), row_order_.end(), std::uint32_t{0});
    refresh_result_view();
    apply_result_sort();
}

void App::stop_sort() {
    if (sort_thread_.joinable()) {
        sort_thread_.request_stop();
        sort_thread_.join();
    }
    sorting_.store(false);
    std::lock_guard lock(sort_mutex_);
    sorted_order_.reset();
}

void App::refresh_result_view() {
    view_order_ = filter_scenario(row_order_, rows_, scenario_filter_);
    pager_.clamp(view_order_.size());
}

void App::apply_result_sort() {
    if (rows_.size() < kBackgroundSortRows) {
        row_order_ = sorted_result_order(rows_, sort_keys_, set_labels_);
        refresh_result_view();
        return;
    }
    // Gros tableau : tri hors du thread UI (rows_ n'est pas modifié pendant le
    // tri : toute nouvelle recherche appelle d'abord stop_sort()).
    stop_sort();
    sorting_.store(true);
    sort_thread_ = std::jthread([this, keys = sort_keys_](std::stop_token) {
        auto order = sorted_result_order(rows_, keys, set_labels_);
        {
            std::lock_guard lock(sort_mutex_);
            sorted_order_ = std::move(order);
        }
        sorting_.store(false);
    });
}

void App::request_result_sort(const ImGuiTableSortSpecs* specs) {
    SortKeys keys;
    for (int i = 0; specs && i < specs->SpecsCount; ++i)
        keys.emplace_back(static_cast<int>(specs->Specs[i].ColumnUserID),
                          specs->Specs[i].SortDirection == ImGuiSortDirection_Ascending);
    sort_keys_ = std::move(keys);
    pager_.reset();  // un tri ramène à la page 1
    apply_result_sort();
}

void App::export_results_csv() {
    if (!report_) return;
    const auto target = pfd::save_file("Exporter les builds",
                                       to_utf8(data_.root() / "builds.csv"),
                                       {"CSV", "*.csv"})
                            .result();
    if (target.empty()) return;
    // Tous les builds (tous scénarios), dans l'ordre du tri en cours.
    std::vector<std::pair<std::size_t, std::size_t>> order;
    order.reserve(row_order_.size());
    for (const auto index : row_order_)
        order.emplace_back(rows_[index].scenario, rows_[index].index);
    if (swcore::text::export_builds_csv(from_utf8(target), *report_, order))
        set_status("Builds exportés : " + target);
    else
        set_status("Impossible d'écrire " + target);
}

void App::render_pagination() {
    const std::size_t rows = view_order_.size();
    const std::size_t count = Pager::page_count(rows, pager_.page_size);
    pager_.clamp(rows);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Lignes par page");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.5f);
    const std::string size_label = std::to_string(pager_.page_size);
    if (ImGui::BeginCombo("##page_size", size_label.c_str())) {
        for (const auto size : kPageSizes)
            if (ImGui::Selectable(std::to_string(size).c_str(), pager_.page_size == size))
                pager_.set_page_size(size);
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, ImGui::GetFontSize() * 1.5f);

    const auto go = [&](std::size_t page) {
        pager_.page = page;
        pager_.clamp(rows);
    };
    ImGui::BeginDisabled(pager_.page <= 1);
    if (ImGui::Button("<<")) go(1);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Première page");
    ImGui::SameLine();
    if (ImGui::Button("<")) go(pager_.page - 1);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Page précédente");
    ImGui::EndDisabled();
    for (const auto page : page_buttons(pager_.page, count)) {
        ImGui::SameLine();
        if (page == 0) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("…");
            continue;
        }
        const bool current = page == pager_.page;
        if (current)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushID(static_cast<int>(page));
        if (ImGui::Button(std::to_string(page).c_str())) go(page);
        ImGui::PopID();
        if (current) ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(pager_.page >= count);
    if (ImGui::Button(">")) go(pager_.page + 1);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Page suivante");
    ImGui::SameLine();
    if (ImGui::Button(">>")) go(count);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Dernière page");
    ImGui::EndDisabled();
    ImGui::SameLine(0, ImGui::GetFontSize() * 1.5f);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Page %s / %s", group_digits(static_cast<long long>(pager_.page)).c_str(),
                group_digits(static_cast<long long>(count)).c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
    // Saisie directe : suit la page courante tant qu'on ne tape pas.
    static int typed_page = 1;
    static std::size_t shown_page = 0;
    if (shown_page != pager_.page) {
        shown_page = pager_.page;
        typed_page = static_cast<int>(pager_.page);
    }
    ImGui::InputInt("##goto", &typed_page, 0, 0);
    if (ImGui::IsItemDeactivatedAfterEdit()) {  // Entrée ou sortie du champ
        go(static_cast<std::size_t>(std::max(1, typed_page)));
        typed_page = static_cast<int>(pager_.page);  // numéro hors limites : ramené
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Aller à la page (Entrée)");
}

void App::render_results() {
    {
        std::lock_guard lock(sort_mutex_);
        if (sorted_order_) {
            row_order_ = std::move(*sorted_order_);
            sorted_order_.reset();
            refresh_result_view();
        }
    }
    if (!report_) {
        ImGui::TextColored(color::muted, "Aucun résultat. Lance une recherche.");
        return;
    }

    // En-tête : nombre de builds, badges, filtre de scénario, export.
    bool incomplete = report_cancelled_;
    bool truncated = false;
    for (const auto& scenario : report_->scenarios) {
        incomplete = incomplete || scenario.incomplete;
        truncated = truncated || scenario.truncated;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s builds valides", group_digits(static_cast<long long>(view_order_.size())).c_str());
    if (incomplete) {
        ImGui::SameLine();
        ImGui::TextColored(color::warning, "[recherche incomplète]");
    }
    if (truncated) {
        ImGui::SameLine();
        ImGui::TextColored(color::warning, "[plafond atteint]");
    }
    const bool several = report_->scenarios.size() > 1;
    if (several) {
        ImGui::SameLine(0, ImGui::GetFontSize() * 1.5f);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        const std::string preview =
            scenario_filter_ < 0 ? std::string{"Tous les scénarios"}
                                 : report_->scenarios[static_cast<std::size_t>(scenario_filter_)].name;
        if (ImGui::BeginCombo("##scenario_filter", preview.c_str())) {
            if (ImGui::Selectable("Tous les scénarios", scenario_filter_ < 0)) {
                scenario_filter_ = -1;
                pager_.reset();
                refresh_result_view();
            }
            for (std::size_t s = 0; s < report_->scenarios.size(); ++s) {
                ImGui::PushID(static_cast<int>(s));
                if (ImGui::Selectable(report_->scenarios[s].name.c_str(),
                                      scenario_filter_ == static_cast<int>(s))) {
                    scenario_filter_ = static_cast<int>(s);
                    pager_.reset();
                    refresh_result_view();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    if (sorting_.load()) {
        ImGui::SameLine();
        ImGui::TextColored(color::accent, "Tri en cours...");
    }
    ImGui::SameLine(0, ImGui::GetFontSize() * 1.5f);
    if (ImGui::Button("Exporter CSV...")) export_results_csv();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Tous les builds, dans l'ordre du tri en cours, ids de runes inclus.");
    for (const auto& scenario : report_->scenarios)
        if (!scenario.validation_error.empty())
            ImGui::TextColored(color::error, "%s : %s", scenario.name.c_str(),
                               scenario.validation_error.c_str());

    // Tableau : seule la page affichée est dessinée.
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingFixedFit;
    const ImVec2 outer(0, -ImGui::GetFrameHeightWithSpacing());
    if (ImGui::BeginTable("##results", kResultColumnCount, flags, outer)) {
        const auto descending = ImGuiTableColumnFlags_PreferSortDescending;
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoHide,
                                0.0f, kColRank);
        ImGui::TableSetupColumn("Scénario", several ? 0 : ImGuiTableColumnFlags_Disabled, 0.0f,
                                kColScenario);
        ImGui::TableSetupColumn("Sets", 0, 0.0f, kColSets);
        ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_DefaultSort | descending, 0.0f,
                                kColScore);
        const char* stat_labels[] = {"PV",       "ATQ", "DEF", "VIT", "Tx crit.",
                                     "Dgts crit.", "RES", "Préc."};
        for (int i = 0; i < 8; ++i)
            ImGui::TableSetupColumn(stat_labels[i], descending, 0.0f,
                                    static_cast<ImGuiID>(kColHp + i));
        ImGui::TableSetupColumn("PV eff.", descending, 0.0f, kColEffectiveHp);
        ImGui::TableSetupColumn("PV eff. DEF brisée", descending, 0.0f, kColEffectiveHpBroken);
        ImGui::TableSetupColumn("DMG", descending, 0.0f, kColDamage);
        ImGui::TableSetupColumn("Efficacité", descending, 0.0f, kColEfficiency);
        ImGui::TableSetupColumn("Craft", 0, 0.0f, kColCraft);
        ImGui::TableSetupColumn("Ids de runes", ImGuiTableColumnFlags_DefaultHide, 0.0f,
                                kColIds);
        ImGui::TableHeadersRow();
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            request_result_sort(specs);
            specs->SpecsDirty = false;
        }

        std::optional<std::uint32_t> open;
        const auto [first, last] = pager_.range(view_order_.size());
        for (std::size_t i = first; i < last; ++i) {
            const std::uint32_t index = view_order_[i];
            const auto& row = rows_[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(kColRank);
            const std::string rank = std::to_string(i + 1);
            if (ImGui::Selectable(rank.c_str(), selected_row_ == index,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap))
                open = index;
            if (ImGui::BeginPopupContextItem("##row_menu")) {
                if (ImGui::MenuItem("Ouvrir")) open = index;
                ImGui::EndPopup();
            }
            if (ImGui::TableSetColumnIndex(kColScenario))
                ImGui::TextUnformatted(report_->scenarios[row.scenario].name.c_str());
            ImGui::TableSetColumnIndex(kColSets);
            ImGui::TextUnformatted(set_labels_[row.sets_label].c_str());
            ImGui::TableSetColumnIndex(kColScore);
            ImGui::TextUnformatted(swcore::text::format_score(row.score).c_str());
            for (int s = 0; s < 8; ++s) {
                ImGui::TableSetColumnIndex(kColHp + s);
                ImGui::TextUnformatted(group_digits(stat_at(row.total, s)).c_str());
            }
            ImGui::TableSetColumnIndex(kColEffectiveHp);
            ImGui::TextUnformatted(group_digits(row.effective_hp).c_str());
            ImGui::TableSetColumnIndex(kColEffectiveHpBroken);
            ImGui::TextUnformatted(group_digits(row.effective_hp_broken_def).c_str());
            ImGui::TableSetColumnIndex(kColDamage);
            ImGui::TextUnformatted(group_digits(row.expected_damage).c_str());
            ImGui::TableSetColumnIndex(kColEfficiency);
            ImGui::Text("%.2f %%", row.efficiency);
            ImGui::TableSetColumnIndex(kColCraft);
            if (row.grinds + row.gems > 0)
                ImGui::Text("%d meule(s), %d gemme(s)", row.grinds, row.gems);
            else
                ImGui::TextColored(color::muted, "-");
            if (ImGui::TableSetColumnIndex(kColIds))
                ImGui::TextUnformatted(ids_text(row.rune_ids).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (open) {
            // Ouvre le build dans un onglet et l'enregistre ; la ligne reste surlignée.
            selected_row_ = *open;
            open_result(*open);
        }
    }
    render_pagination();
}

}  // namespace swgui
