#include "gui_components.hpp"

#include "gui_common.hpp"
#include "text_util.hpp"

#include "swcore/build_text.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>

namespace swgui {
namespace fs = std::filesystem;

namespace {

using IdNames = std::map<int, std::string>;

IdNames load_names(const fs::path& path) {
    IdNames names;
    std::ifstream input(path);
    if (!input) return names;
    try {
        nlohmann::json root;
        input >> root;
        for (const auto& [id, name] : root.items())
            if (name.is_string()) names.emplace(std::stoi(id), name.get<std::string>());
    } catch (const std::exception&) {
    }
    return names;
}

std::string name_of(const IdNames& names, int id) {
    const auto it = names.find(id);
    return it == names.end() ? swcore::text::unknown(id) : it->second;
}

// Libellé d'effet d'artefact (gabarit "${value}" de data/i18n/en.json).
std::string artifact_effect(const IdNames& labels, int id, double value) {
    if (id == 0) return "aucun";
    std::string text = name_of(labels, id);
    const std::string needle = "${value}";
    const std::string replacement = swcore::text::format_number(value);
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + replacement.size()))
        text.replace(at, needle.size(), replacement);
    return text;
}

void colored_diff(long long value) {
    if (value > 0)
        ImGui::TextColored(color::positive, "%s", swcore::text::signed_group(value).c_str());
    else if (value < 0)
        ImGui::TextColored(color::negative, "%s", swcore::text::signed_group(value).c_str());
    else
        ImGui::TextColored(color::muted, "0");
}

}  // namespace

void render_stat_comparison(const StatComparison& stats) {
    if (!ImGui::BeginTable("##stat_comparison", 4,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("Stat");
    for (const char* header : stats.headers) ImGui::TableSetupColumn(header);
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < 8; ++i) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(swcore::text::stat_label(i));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(group_digits(stats.left[i]).c_str());
        ImGui::TableSetColumnIndex(2);
        const int green = stats.green[i];
        ImGui::Text("%s [%s]", group_digits(stats.right[i]).c_str(),
                    (green >= 0 ? "+" + group_digits(green) : group_digits(green)).c_str());
        ImGui::TableSetColumnIndex(3);
        colored_diff(static_cast<long long>(stats.right[i]) - stats.left[i]);
    }
    const char* labels[] = {"DMG", "PV eff.", "PV eff. DEF brisée"};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto left = static_cast<long long>(stats.left_metrics[i]);
        const auto right = static_cast<long long>(stats.right_metrics[i]);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(labels[i]);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(group_digits(left).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(group_digits(right).c_str());
        ImGui::TableSetColumnIndex(3);
        colored_diff(right - left);
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Efficacité %");
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(
        stats.left_efficiency ? swcore::text::fixed2(*stats.left_efficiency).c_str() : "-");
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(
        stats.right_efficiency ? swcore::text::fixed2(*stats.right_efficiency).c_str() : "-");
    ImGui::TableSetColumnIndex(3);
    if (stats.left_efficiency && stats.right_efficiency) {
        const double delta = *stats.right_efficiency - *stats.left_efficiency;
        ImGui::TextColored(delta > 0.005    ? color::positive
                           : delta < -0.005 ? color::negative
                                            : color::muted,
                           "%s", swcore::text::signed_fixed2(delta).c_str());
    } else {
        ImGui::TextColored(color::muted, "-");
    }
    ImGui::EndTable();
}

bool lock_button(const char* id, swcore::LockState state, const std::string& tooltip) {
    const float height = ImGui::GetTextLineHeight();
    const ImVec2 size(height * 0.85f, height);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    ImVec4 tint = state == swcore::LockState::Full      ? color::accent
                  : state == swcore::LockState::Partial ? color::warning
                                                        : color::muted;
    if (hovered) tint.w = 1.0f;
    else if (state == swcore::LockState::None) tint.w = 0.6f;
    const ImU32 col = ImGui::GetColorU32(tint);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // Corps du cadenas.
    const ImVec2 body_min(origin.x + size.x * 0.08f, origin.y + size.y * 0.45f);
    const ImVec2 body_max(origin.x + size.x * 0.92f, origin.y + size.y * 0.95f);
    if (state == swcore::LockState::Full) {
        draw->AddRectFilled(body_min, body_max, col, 2.0f);
    } else if (state == swcore::LockState::Partial) {
        draw->AddRect(body_min, body_max, col, 2.0f, 0, 1.5f);
        draw->AddRectFilled(body_min, ImVec2((body_min.x + body_max.x) * 0.5f, body_max.y),
                            col, 2.0f);
    } else {
        draw->AddRect(body_min, body_max, col, 2.0f, 0, 1.5f);
    }
    // Anse : fermée (verrouillé) ou relevée (ouvert).
    const float radius = size.x * 0.28f;
    const float lift = state == swcore::LockState::None ? size.y * 0.12f : 0.0f;
    const ImVec2 center((body_min.x + body_max.x) * 0.5f, body_min.y - lift);
    draw->PathArcTo(center, radius, IM_PI, 2.0f * IM_PI, 12);
    draw->PathStroke(col, 0, 1.6f);
    draw->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x - radius, body_min.y),
                  col, 1.6f);
    if (state != swcore::LockState::None)
        draw->AddLine(ImVec2(center.x + radius, center.y),
                      ImVec2(center.x + radius, body_min.y), col, 1.6f);
    if (hovered && !tooltip.empty()) ImGui::SetTooltip("%s", tooltip.c_str());
    return clicked;
}

void render_rune_card(const swcore::RuneView& view, const swcore::RuneMapping& mapping,
                      const swcore::LockTable* locks, bool* lock_clicked,
                      std::uint64_t context_unit) {
    using namespace swcore::text;
    const auto& rune = view.rune;
    if (locks) {
        const auto* lock = locks->find(swcore::LockKind::Rune, rune.rune_id);
        // Verrouillée pour un autre monstre que celui du contexte : état distinct.
        const bool other = lock && context_unit != 0 && lock->reserved_for != context_unit;
        const std::string tooltip =
            !lock ? std::string{"Non verrouillée (clic : verrouiller)"}
            : lock->reserved_for == 0
                ? std::string{"Verrouillée : réservée à personne (clic : déverrouiller)"}
                : (other ? "Verrouillée pour " + lock->monster_name + ", pas pour ce monstre"
                         : "Verrouillée pour " + lock->monster_name) +
                      " (clic : déverrouiller)";
        const auto state = !lock ? swcore::LockState::None
                           : other ? swcore::LockState::Partial
                                   : swcore::LockState::Full;
        if (lock_button("##rune_lock", state, tooltip) && lock_clicked) *lock_clicked = true;
        ImGui::SameLine();
    }
    ImGui::Text("+%d %s (slot %d)  %s", rune.upgrade_level,
                set_name(mapping, rune.set_id).c_str(), rune.slot,
                stars_text(rune.stars).c_str());
    ImGui::Text("Principale : %s", effect_text(mapping, rune.primary).c_str());
    const bool innate = rune.innate && rune.innate->stat != swcore::StatType::None;
    ImGui::Text("Innée : %s", innate ? effect_text(mapping, rune.innate).c_str() : "-");
    for (std::size_t j = 0; j < rune.secondary.size(); ++j) {
        const auto& line = rune.secondary[j];
        std::string text = stat_name(mapping, line.value.stat_id) + " " +
                           std::to_string(line.value.value + line.grind);
        if (line.grind != 0) text += " [+" + std::to_string(line.grind) + "]";
        if (line.enchanted) text += " (gemme)";
        const auto gem = std::find_if(view.gems.begin(), view.gems.end(),
                                      [&](const swcore::AppliedGem& g) {
                                          return g.line == static_cast<int>(j);
                                      });
        if (gem != view.gems.end()) {
            ImGui::TextColored(color::muted, "  %s [remplacée]", text.c_str());
            ImGui::TextColored(color::accent, "    -> gemme %s %s %d (simulée)",
                               rarity_label(gem->rarity),
                               stat_name(mapping, gem->stat_id).c_str(), gem->value);
        } else {
            ImGui::Text("  %s", text.c_str());
        }
        for (const auto& grind : view.grinds)
            if (grind.line == static_cast<int>(j))
                ImGui::TextColored(color::accent, "    -> meule %s +%d (simulée)",
                                   rarity_label(grind.rarity), grind.value);
    }
    ImGui::TextColored(color::muted, "Id : %s", std::to_string(rune.rune_id).c_str());
    ImGui::TextColored(color::muted, "Origine : %s", origin_name(rune).c_str());
    ImGui::Text("Efficacité : %s %%", fixed2(view.efficiency).c_str());
    if (view.simulated_efficiency) {
        ImGui::SameLine();
        ImGui::TextColored(color::accent, "(après simulation : %s %%)",
                           fixed2(*view.simulated_efficiency).c_str());
    }
}

int render_rune_cards(const std::array<swcore::RuneView, 6>& runes,
                      const std::array<bool, 6>& present,
                      const swcore::RuneMapping& mapping, const swcore::LockTable* locks,
                      std::uint64_t context_unit) {
    int clicked_slot = 0;
    if (!ImGui::BeginTable("##cards", 3,
                           ImGuiTableFlags_BordersInner | ImGuiTableFlags_SizingStretchSame))
        return clicked_slot;
    for (const auto& slots : {std::array<int, 3>{6, 1, 2}, std::array<int, 3>{5, 4, 3}}) {
        ImGui::TableNextRow();
        for (int column = 0; column < 3; ++column) {
            const auto slot = static_cast<std::size_t>(slots[static_cast<std::size_t>(column)]);
            ImGui::TableSetColumnIndex(column);
            ImGui::PushID(static_cast<int>(slot));
            if (present[slot - 1]) {
                bool lock_clicked = false;
                render_rune_card(runes[slot - 1], mapping, locks, &lock_clicked, context_unit);
                if (lock_clicked) clicked_slot = static_cast<int>(slot);
            } else {
                ImGui::TextColored(color::muted, "Slot %d : vide", static_cast<int>(slot));
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    return clicked_slot;
}

std::uint64_t render_artifacts(const swcore::AccountMonster& monster, const fs::path& root,
                               bool show_empty, const swcore::LockTable* locks) {
    std::uint64_t clicked = 0;
    static const IdNames labels = load_names(root / "data" / "i18n" / "en.json");
    static const IdNames attributes =
        load_names(root / "data" / "mapping" / "monster_attributes.json");
    static const IdNames archetypes =
        load_names(root / "data" / "mapping" / "monster_archetypes.json");
    if (monster.artifacts.empty() && !show_empty) return clicked;
    ImGui::SeparatorText("Artefacts équipés");
    for (int slot = 1; slot <= 2; ++slot) {
        const auto it = std::find_if(monster.artifacts.begin(), monster.artifacts.end(),
                                     [&](const swcore::Artifact& a) { return a.slot == slot; });
        if (it == monster.artifacts.end()) {
            if (show_empty) ImGui::TextColored(color::muted, "Slot %d : vide", slot);
            continue;
        }
        if (locks) {
            ImGui::PushID(slot);
            const auto* lock = locks->find(swcore::LockKind::Artifact, it->rid);
            if (lock_button("##artifact_lock",
                            lock ? swcore::LockState::Full : swcore::LockState::None,
                            lock ? "Verrouillé pour " + lock->monster_name
                                 : std::string{"Non verrouillé"}))
                clicked = it->rid;
            ImGui::PopID();
            ImGui::SameLine();
        }
        // Slot 1 : artefact d'élément (attribut) ; slot 2 : d'archétype.
        const std::string kind = slot == 1
                                     ? "attribut : " + name_of(attributes, it->attribute)
                                     : "archétype : " + name_of(archetypes, it->unit_style);
        ImGui::Text("Slot %d (%s)  niv. %d : %s", slot, kind.c_str(), it->level,
                    artifact_effect(labels, static_cast<int>(it->primary[0]), it->primary[1])
                        .c_str());
        for (const auto& effect : it->secondary)
            ImGui::TextColored(color::muted, "    %s",
                               artifact_effect(labels, static_cast<int>(effect[0]), effect[1])
                                   .c_str());
    }
    return clicked;
}

std::string set_bonus_text(const swcore::SetEffects& effects, int set_id, int activations) {
    const auto it = effects.effects.find(set_id);
    if (it == effects.effects.end()) return {};
    const auto& effect = it->second;
    const std::string prefix =
        activations > 0 ? std::string{} : std::to_string(effect.pieces_required) + " p. : ";
    if (effect.status != "confirmed" || effect.stat.empty() || effect.value == 0)
        return prefix + "bonus non chiffré";
    static const std::pair<const char*, const char*> labels[] = {
        {"hp", "PV"}, {"attack", "ATQ"}, {"def", "DEF"}, {"spd", "VIT"},
        {"crit_rate", "Tx crit."}, {"crit_damage", "Dgts crit."},
        {"resistance", "RES"}, {"accuracy", "Préc."}};
    std::string stat = effect.stat;
    for (const auto& [key, label] : labels)
        if (effect.stat == key) stat = label;
    const int value = effect.value * std::max(1, activations);
    return prefix + stat + " +" + std::to_string(value) + " " + effect.unit;
}

}  // namespace swgui
