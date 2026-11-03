// Résultats de la recherche : lignes compactes, tri multi-colonnes (hors du
// thread UI pour les gros tableaux), export CSV identique à swcli --export.

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

enum ResultColumn {
    kIds, kSets, kHp, kAttack, kDefense, kSpeed, kCritRate, kCritDamage, kResistance,
    kAccuracy, kEffectiveHp, kEffectiveHpBroken, kDamage, kEfficiency, kScore, kCraft,
    kResultColumns
};

constexpr std::size_t kBackgroundSortRows = 50'000;

using SortKeys = std::vector<std::pair<int, bool>>;  // colonne, croissant

int compare(const ResultRow& a, const ResultRow& b, int column,
            const std::vector<std::string>& labels) {
    const auto cmp = [](const auto& x, const auto& y) { return x < y ? -1 : y < x ? 1 : 0; };
    switch (column) {
    case kIds: return cmp(a.rune_ids, b.rune_ids);
    case kSets: return cmp(labels[a.sets_label], labels[b.sets_label]);
    case kEffectiveHp: return cmp(a.effective_hp, b.effective_hp);
    case kEffectiveHpBroken: return cmp(a.effective_hp_broken_def, b.effective_hp_broken_def);
    case kDamage: return cmp(a.expected_damage, b.expected_damage);
    case kEfficiency: return cmp(a.efficiency, b.efficiency);
    case kScore: return cmp(a.score, b.score);
    case kCraft: return cmp(a.grinds + a.gems, b.grinds + b.gems);
    default:
        if (column >= kHp && column <= kAccuracy)
            return cmp(stat_at(a.total, column - kHp), stat_at(b.total, column - kHp));
        return 0;
    }
}

// Ordre trié ; à clés égales, l'ordre du solveur (tie-break déterministe).
std::vector<std::uint32_t> sorted_order(const std::vector<ResultRow>& rows,
                                        const SortKeys& keys,
                                        const std::vector<std::string>& labels) {
    std::vector<std::uint32_t> order(rows.size());
    std::iota(order.begin(), order.end(), std::uint32_t{0});
    if (keys.empty()) return order;
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t l, std::uint32_t r) {
        for (const auto& [column, ascending] : keys) {
            const int result = compare(rows[l], rows[r], column, labels);
            if (result != 0) return ascending ? result < 0 : result > 0;
        }
        return false;
    });
    return order;
}

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
    if (!report_) return;
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
    row_order_.resize(rows_.size());
    std::iota(row_order_.begin(), row_order_.end(), std::uint32_t{0});
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

void App::request_result_sort(const ImGuiTableSortSpecs* specs) {
    SortKeys keys;
    for (int i = 0; specs && i < specs->SpecsCount; ++i)
        keys.emplace_back(static_cast<int>(specs->Specs[i].ColumnUserID),
                          specs->Specs[i].SortDirection == ImGuiSortDirection_Ascending);
    if (rows_.size() < kBackgroundSortRows) {
        row_order_ = sorted_order(rows_, keys, set_labels_);
        return;
    }
    // Gros tableau : tri hors du thread UI (rows_ n'est pas modifié pendant le
    // tri : toute nouvelle recherche appelle d'abord stop_sort()).
    stop_sort();
    sorting_.store(true);
    sort_thread_ = std::jthread([this, keys](std::stop_token) {
        auto order = sorted_order(rows_, keys, set_labels_);
        {
            std::lock_guard lock(sort_mutex_);
            sorted_order_ = std::move(order);
        }
        sorting_.store(false);
    });
}

void App::export_results_csv() {
    if (!report_) return;
    const auto target = pfd::save_file("Exporter les builds",
                                       to_utf8(data_.root() / "builds.csv"),
                                       {"CSV", "*.csv"})
                            .result();
    if (target.empty()) return;
    if (swcore::text::export_builds_csv(from_utf8(target), *report_))
        set_status("Builds exportés : " + target);
    else
        set_status("Impossible d'écrire " + target);
}

void App::render_results() {
    {
        std::lock_guard lock(sort_mutex_);
        if (sorted_order_) {
            row_order_ = std::move(*sorted_order_);
            sorted_order_.reset();
        }
    }
    if (!report_) {
        ImGui::TextColored(color::muted, "Aucun résultat pour l'instant.");
        return;
    }

    // Résumé par scénario, badge "incomplet" si arrêtée ou annulée.
    for (const auto& scenario : report_->scenarios) {
        const std::size_t count = scenario.all_builds_mode
                                      ? static_cast<std::size_t>(scenario.valid_builds)
                                      : scenario.builds.size();
        ImGui::Text("%s : %s build(s)", scenario.name.c_str(),
                    group_digits(static_cast<long long>(count)).c_str());
        if (scenario.incomplete || report_cancelled_) {
            ImGui::SameLine();
            ImGui::TextColored(color::warning, "[incomplet]");
        }
        if (scenario.truncated) {
            ImGui::SameLine();
            ImGui::TextColored(color::warning, "[tronqué au plafond]");
        }
        if (!scenario.validation_error.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(color::error, "Erreur : %s", scenario.validation_error.c_str());
        }
    }
    if (ImGui::Button("Exporter CSV...")) export_results_csv();
    ImGui::SameLine();
    ImGui::BeginDisabled(selected_row_ >= rows_.size());
    if (ImGui::Button("Copier les ids de runes"))
        ImGui::SetClipboardText(ids_text(rows_[selected_row_].rune_ids).c_str());
    ImGui::EndDisabled();
    if (sorting_.load()) {
        ImGui::SameLine();
        ImGui::TextColored(color::accent, "Tri en cours...");
    }

    const ImGuiTableFlags flags =
        ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_SortTristate |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##results", kResultColumns, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Ids des runes", 0, 0.0f, kIds);
    ImGui::TableSetupColumn("Sets", 0, 0.0f, kSets);
    const char* short_labels[] = {"PV", "ATQ", "DEF", "VIT", "TC", "DC", "RES", "PRÉC"};
    for (int i = 0; i < 8; ++i)
        ImGui::TableSetupColumn(short_labels[i], ImGuiTableColumnFlags_PreferSortDescending,
                                0.0f, static_cast<ImGuiID>(kHp + i));
    ImGui::TableSetupColumn("PV eff.", ImGuiTableColumnFlags_PreferSortDescending, 0.0f,
                            kEffectiveHp);
    ImGui::TableSetupColumn("PV eff. DEF brisée", ImGuiTableColumnFlags_PreferSortDescending,
                            0.0f, kEffectiveHpBroken);
    ImGui::TableSetupColumn("DMG", ImGuiTableColumnFlags_PreferSortDescending, 0.0f, kDamage);
    ImGui::TableSetupColumn("Efficacité", ImGuiTableColumnFlags_PreferSortDescending, 0.0f,
                            kEfficiency);
    ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_PreferSortDescending, 0.0f, kScore);
    ImGui::TableSetupColumn("Meules / gemmes", 0, 0.0f, kCraft);
    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
        request_result_sort(specs);
        specs->SpecsDirty = false;
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(row_order_.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const std::uint32_t index = row_order_[static_cast<std::size_t>(i)];
            const auto& row = rows_[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(kIds);
            const std::string ids = ids_text(row.rune_ids);
            if (ImGui::Selectable(ids.c_str(), selected_row_ == index,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap)) {
                selected_row_ = index;
                open_result(index);
            }
            ImGui::TableSetColumnIndex(kSets);
            ImGui::TextUnformatted(set_labels_[row.sets_label].c_str());
            for (int s = 0; s < 8; ++s) {
                ImGui::TableSetColumnIndex(kHp + s);
                ImGui::TextUnformatted(group_digits(stat_at(row.total, s)).c_str());
            }
            ImGui::TableSetColumnIndex(kEffectiveHp);
            ImGui::TextUnformatted(group_digits(row.effective_hp).c_str());
            ImGui::TableSetColumnIndex(kEffectiveHpBroken);
            ImGui::TextUnformatted(group_digits(row.effective_hp_broken_def).c_str());
            ImGui::TableSetColumnIndex(kDamage);
            ImGui::TextUnformatted(group_digits(row.expected_damage).c_str());
            ImGui::TableSetColumnIndex(kEfficiency);
            ImGui::Text("%.2f %%", row.efficiency);
            ImGui::TableSetColumnIndex(kScore);
            ImGui::TextUnformatted(swcore::text::format_score(row.score).c_str());
            ImGui::TableSetColumnIndex(kCraft);
            if (row.grinds + row.gems > 0) ImGui::Text("%d / %d", row.grinds, row.gems);
            else ImGui::TextColored(color::muted, "-");
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

}  // namespace swgui
