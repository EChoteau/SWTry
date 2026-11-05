#include "swcore/account/equip.hpp"

#include "swcore/solver/solver.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

namespace swcore {

namespace {

using json = nlohmann::json;
constexpr int kEquipVersion = 1;

void warn(const WarningSink& warnings, const std::string& message) {
    if (warnings) warnings(message);
}

AccountMonster* find_monster(AccountState& state, std::uint64_t unit_id) {
    const auto it = std::find_if(state.monsters.begin(), state.monsters.end(),
                                 [&](const AccountMonster& m) { return m.unit_id == unit_id; });
    return it == state.monsters.end() ? nullptr : &*it;
}

const AccountMonster* find_monster(const AccountState& state, std::uint64_t unit_id) {
    const auto it = std::find_if(state.monsters.begin(), state.monsters.end(),
                                 [&](const AccountMonster& m) { return m.unit_id == unit_id; });
    return it == state.monsters.end() ? nullptr : &*it;
}

void place_rune(Rune& rune, std::uint64_t owner) {
    rune.occupancy = owner ? RuneOccupancy::Equipped : RuneOccupancy::Inventory;
    rune.source_unit_id = owner;
    rune.occupied_id = owner;
}

// Déplace une rune (état cohérent : liste des runes + runes des monstres).
void move_rune(AccountState& state, const EquipMove& move, std::set<std::uint64_t>& touched) {
    const auto it = std::find_if(state.runes.begin(), state.runes.end(),
                                 [&](const Rune& r) { return r.rune_id == move.id; });
    if (it == state.runes.end()) return;  // rune absente : ignorée
    const std::uint64_t owner = rune_owner(*it);
    if (AccountMonster* from = find_monster(state, owner)) {
        auto& runes = from->runes;
        runes.erase(std::remove_if(runes.begin(), runes.end(),
                                   [&](const Rune& r) { return r.rune_id == move.id; }),
                    runes.end());
        touched.insert(owner);
    }
    AccountMonster* to = move.to ? find_monster(state, move.to) : nullptr;
    if (!to) {
        place_rune(*it, 0);
        return;
    }
    // Slot occupé par une rune non déplacée : elle retourne en inventaire.
    for (auto& other : to->runes)
        if (other.slot == it->slot)
            for (auto& listed : state.runes)
                if (listed.rune_id == other.rune_id) place_rune(listed, 0);
    to->runes.erase(std::remove_if(to->runes.begin(), to->runes.end(),
                                   [&](const Rune& r) { return r.slot == it->slot; }),
                    to->runes.end());
    place_rune(*it, move.to);
    to->runes.push_back(*it);
    touched.insert(move.to);
}

void move_artifact(AccountState& state, const EquipMove& move,
                   std::set<std::uint64_t>& touched) {
    Artifact artifact;
    bool found = false;
    for (auto& monster : state.monsters) {
        const auto it = std::find_if(monster.artifacts.begin(), monster.artifacts.end(),
                                     [&](const Artifact& a) { return a.rid == move.id; });
        if (it == monster.artifacts.end()) continue;
        artifact = *it;
        monster.artifacts.erase(it);
        touched.insert(monster.unit_id);
        found = true;
        break;
    }
    if (!found) {
        const auto it = std::find_if(state.spare_artifacts.begin(), state.spare_artifacts.end(),
                                     [&](const Artifact& a) { return a.rid == move.id; });
        if (it == state.spare_artifacts.end()) return;
        artifact = *it;
        state.spare_artifacts.erase(it);
    }
    AccountMonster* to = move.to ? find_monster(state, move.to) : nullptr;
    if (!to) {
        artifact.occupied_id = 0;
        state.spare_artifacts.push_back(artifact);
        return;
    }
    // Même type (slot) déjà équipé : il est déséquipé.
    for (auto it = to->artifacts.begin(); it != to->artifacts.end();)
        if (it->slot == artifact.slot) {
            it->occupied_id = 0;
            state.spare_artifacts.push_back(*it);
            it = to->artifacts.erase(it);
        } else {
            ++it;
        }
    artifact.occupied_id = move.to;
    to->artifacts.push_back(artifact);
    touched.insert(move.to);
}

json grind_json(const AppliedGrindstone& g) {
    return {g.stat_id, g.value, g.previous, g.rarity, g.line};
}

json gem_json(const AppliedGem& g) {
    return {g.line, g.old_stat_id, g.old_value, g.old_grind, g.old_enchanted,
            g.stat_id, g.value, g.rarity};
}

const char* scope_text(EquipScope scope) {
    switch (scope) {
    case EquipScope::Runes: return "runes";
    case EquipScope::Artifacts: return "artifacts";
    case EquipScope::Both: break;
    }
    return "both";
}

}  // namespace

void name_rune_sources(AccountState& state, const ReferenceDB& reference) {
    for (auto& rune : state.runes) {
        if (rune.occupancy == RuneOccupancy::Inventory) {
            rune.source_name = "Inventaire";
            continue;
        }
        const AccountMonster* owner = find_monster(state, rune.source_unit_id);
        if (!owner) continue;
        const auto* known = reference.find(owner->unit_master_id);
        rune.source_name = (known ? known->name : std::string{"Monstre"}) + " (" +
                           std::to_string(owner->unit_id) + ")";
    }
    // Les copies équipées sur les monstres portent le même nom.
    for (auto& monster : state.monsters)
        for (auto& equipped : monster.runes)
            for (const auto& rune : state.runes)
                if (rune.rune_id == equipped.rune_id) equipped.source_name = rune.source_name;
}

AccountState effective_state(const AccountState& exported,
                             const std::vector<EquipAction>& actions,
                             const SetEffects& effects, const ReferenceDB& reference) {
    if (actions.empty()) return exported;
    AccountState state = exported;
    std::set<std::uint64_t> touched;
    for (const auto& action : actions)
        for (const auto& move : action.moves) {
            if (move.kind == LockKind::Rune) move_rune(state, move, touched);
            else move_artifact(state, move, touched);
        }
    for (auto& monster : state.monsters) {
        if (!touched.contains(monster.unit_id)) continue;
        std::stable_sort(monster.runes.begin(), monster.runes.end(),
                         [](const Rune& a, const Rune& b) { return a.slot < b.slot; });
        std::stable_sort(monster.artifacts.begin(), monster.artifacts.end(),
                         [](const Artifact& a, const Artifact& b) { return a.slot < b.slot; });
        monster.stats = calculate_monster_stats(monster, effects);
    }
    name_rune_sources(state, reference);
    return state;
}

EquipAction make_equip_action(const AccountState& state, std::uint64_t target,
                              const SolverBuild& build, EquipScope scope) {
    EquipAction action;
    action.target = target;
    action.scope = scope;
    const AccountMonster* monster = find_monster(state, target);
    if (!monster) return action;
    if (scope != EquipScope::Artifacts) {
        for (std::size_t slot = 0; slot < 6; ++slot) {
            // Rune réelle du build (les meules/gemmes simulées restent des tâches).
            const std::uint64_t wanted = build.actual_runes[slot].rune_id != 0
                                             ? build.actual_runes[slot].rune_id
                                             : build.runes[slot].rune_id;
            if (wanted == 0) continue;
            const int slot_number = static_cast<int>(slot + 1);
            const auto current = std::find_if(
                monster->runes.begin(), monster->runes.end(),
                [&](const Rune& r) { return r.slot == slot_number; });
            if (current != monster->runes.end() && current->rune_id == wanted) continue;
            if (current != monster->runes.end())
                action.moves.push_back(
                    {LockKind::Rune, current->rune_id, target, 0, slot_number});
            const auto source = std::find_if(state.runes.begin(), state.runes.end(),
                                             [&](const Rune& r) { return r.rune_id == wanted; });
            const std::uint64_t from = source == state.runes.end() ? 0 : rune_owner(*source);
            action.moves.push_back({LockKind::Rune, wanted, from, target, slot_number});
        }
        for (std::size_t slot = 0; slot < 6; ++slot) {
            const auto& rune = build.runes[slot];
            if (rune.applied_grindstones.empty() && rune.applied_gems.empty()) continue;
            action.craft.push_back({rune.rune_id, static_cast<int>(slot + 1),
                                    rune.applied_grindstones, rune.applied_gems});
        }
    }
    // Artefacts : le build garde ceux du monstre, donc aucun déplacement tant que
    // le solveur ne les optimise pas.
    return action;
}

bool build_equipped(const AccountState& state, std::uint64_t target,
                    const SolverBuild& build, EquipScope scope) {
    const auto action = make_equip_action(state, target, build, scope);
    return action.moves.empty();
}

std::map<std::uint64_t, int> pending_changes(const std::vector<EquipAction>& actions) {
    std::map<std::uint64_t, int> changes;
    for (const auto& action : actions)
        for (const auto& move : action.moves) {
            if (move.from) ++changes[move.from];
            if (move.to) ++changes[move.to];
        }
    return changes;
}

EquipReconcile reconcile_equip_log(EquipLog& log, const AccountState& exported,
                                   std::uint64_t wizard_id) {
    EquipReconcile result;
    if (log.wizard_id != 0 && wizard_id != 0 && log.wizard_id != wizard_id) {
        result.dropped = !log.actions.empty();
        log.actions.clear();
        log.wizard_id = wizard_id;
        return result;
    }
    log.wizard_id = wizard_id;
    const auto location = [&](const EquipMove& move) -> std::optional<std::uint64_t> {
        if (move.kind == LockKind::Rune) {
            for (const auto& rune : exported.runes)
                if (rune.rune_id == move.id) return rune_owner(rune);
            return std::nullopt;
        }
        for (const auto& monster : exported.monsters)
            for (const auto& artifact : monster.artifacts)
                if (artifact.rid == move.id) return monster.unit_id;
        return std::uint64_t{0};
    };
    for (auto& action : log.actions) {
        std::vector<EquipMove> kept;
        for (const auto& move : action.moves) {
            const auto where = location(move);
            if (where && *where == move.to) ++result.satisfied;  // déjà fait en jeu
            else kept.push_back(move);
        }
        action.moves = std::move(kept);
        result.remaining += static_cast<int>(action.moves.size());
    }
    log.actions.erase(std::remove_if(log.actions.begin(), log.actions.end(),
                                     [](const EquipAction& a) { return a.moves.empty(); }),
                      log.actions.end());
    return result;
}

std::string EquipLog::to_json() const {
    json actions_json = json::array();
    for (const auto& action : actions) {
        json moves = json::array();
        for (const auto& move : action.moves)
            moves.push_back({{"type", move.kind == LockKind::Artifact ? "artifact" : "rune"},
                             {"id", move.id},
                             {"from", move.from},
                             {"to", move.to},
                             {"slot", move.slot}});
        json craft = json::array();
        for (const auto& task : action.craft) {
            json grinds = json::array();
            for (const auto& g : task.grinds) grinds.push_back(grind_json(g));
            json gems = json::array();
            for (const auto& g : task.gems) gems.push_back(gem_json(g));
            craft.push_back({{"rune_id", task.rune_id},
                             {"slot", task.slot},
                             {"grinds", grinds},
                             {"gems", gems}});
        }
        actions_json.push_back({{"id", action.id},
                                {"date", action.date},
                                {"target", action.target},
                                {"scope", scope_text(action.scope)},
                                {"moves", moves},
                                {"craft", craft}});
    }
    return json{{"version", kEquipVersion}, {"wizard_id", wizard_id}, {"actions", actions_json}}
        .dump(2);
}

EquipLog EquipLog::parse(const std::string& json_text, WarningSink warnings) {
    EquipLog log;
    try {
        const json root = json::parse(json_text);
        log.wizard_id = root.value("wizard_id", std::uint64_t{0});
        for (const auto& item : root.value("actions", json::array())) {
            EquipAction action;
            action.id = item.value("id", std::uint64_t{0});
            action.date = item.value("date", std::string{});
            action.target = item.value("target", std::uint64_t{0});
            const auto scope = item.value("scope", std::string{"both"});
            action.scope = scope == "runes"       ? EquipScope::Runes
                           : scope == "artifacts" ? EquipScope::Artifacts
                                                  : EquipScope::Both;
            for (const auto& move : item.value("moves", json::array()))
                action.moves.push_back(
                    {move.value("type", std::string{"rune"}) == "artifact" ? LockKind::Artifact
                                                                           : LockKind::Rune,
                     move.value("id", std::uint64_t{0}), move.value("from", std::uint64_t{0}),
                     move.value("to", std::uint64_t{0}), move.value("slot", 0)});
            for (const auto& task : item.value("craft", json::array())) {
                EquipCraftTask craft;
                craft.rune_id = task.value("rune_id", std::uint64_t{0});
                craft.slot = task.value("slot", 0);
                for (const auto& g : task.value("grinds", json::array()))
                    craft.grinds.push_back({g.at(0).get<int>(), g.at(1).get<int>(),
                                            g.at(2).get<int>(), g.at(3).get<int>(),
                                            g.at(4).get<int>()});
                for (const auto& g : task.value("gems", json::array()))
                    craft.gems.push_back({g.at(0).get<int>(), g.at(1).get<int>(),
                                          g.at(2).get<int>(), g.at(3).get<int>(),
                                          g.at(4).get<bool>(), g.at(5).get<int>(),
                                          g.at(6).get<int>(), g.at(7).get<int>()});
                action.craft.push_back(std::move(craft));
            }
            log.actions.push_back(std::move(action));
        }
    } catch (const std::exception& error) {
        warn(warnings, std::string("EquipLog: invalid JSON: ") + error.what());
        return {};
    }
    return log;
}

EquipLog EquipLog::load(const std::filesystem::path& path, WarningSink warnings) {
    std::ifstream input(path);
    if (!input) return {};
    std::stringstream buffer;
    buffer << input.rdbuf();
    return parse(buffer.str(), std::move(warnings));
}

bool EquipLog::save(const std::filesystem::path& path) const {
    return write_text_atomic(path, to_json());
}

std::uint64_t read_wizard_id(const std::filesystem::path& export_path) {
    std::ifstream input(export_path);
    if (!input) return 0;
    try {
        json root;
        input >> root;
        if (const auto info = root.find("wizard_info"); info != root.end() && info->is_object())
            return info->value("wizard_id", std::uint64_t{0});
        return root.value("wizard_id", std::uint64_t{0});
    } catch (const std::exception&) {
        return 0;
    }
}

}  // namespace swcore
