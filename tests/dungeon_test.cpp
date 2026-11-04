// Étape 1 du module donjon : chargement du modèle (donjons, étages, vagues,
// compétences + overrides), configs de teams, mechanics.json. Aucun calcul.

#include <catch2/catch_test_macros.hpp>

#include "swcore/account.hpp"
#include "swcore/dungeon.hpp"
#include "swcore/runes.hpp"
#include "swcore/team_config.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

const std::string kRoot{SW_PROJECT_SOURCE_DIR};

struct Loaded {
    std::vector<std::string> warnings;
    swcore::ReferenceDB reference =
        swcore::ReferenceDB::load(kRoot + "/data/swarfarm/monsters.json");
    swcore::DungeonData data = swcore::DungeonData::load(
        kRoot + "/data/swarfarm", kRoot + "/data/overrides/skills_overrides.json", reference,
        [this](const std::string& message) { warnings.push_back(message); });
};

const Loaded& loaded() {
    static const Loaded instance;
    return instance;
}

}  // namespace

TEST_CASE("Dungeon model: Giant's Keep B12 waves, enemies and skills") {
    const auto& data = loaded().data;
    CHECK(loaded().warnings.empty());
    const auto* keep = data.dungeon(8001);
    REQUIRE(keep);
    CHECK(keep->name == "Giant's Keep");
    CHECK(keep->category == "Cairos Dungeon");
    CHECK(std::find(keep->level_ids.begin(), keep->level_ids.end(), 685) != keep->level_ids.end());
    CHECK(data.has_wave_data(*keep));

    const auto* level = data.level(685);
    REQUIRE(level);
    CHECK(level->dungeon_id == 8001);
    CHECK(level->floor == 12);
    CHECK_FALSE(level->difficulty.has_value());  // null dans SWARFARM
    REQUIRE(level->waves.size() == 4);

    // Vague du boss : Géant (VIT 90) et deux tours (VIT 90 et 121).
    const auto& boss = level->waves.back().enemies;
    REQUIRE(boss.size() == 3);
    CHECK(boss[0].name == "Ancient Giant");
    CHECK(boss[0].speed == 90);
    CHECK(boss[0].hp == 238620);
    CHECK(boss[1].speed == 90);
    CHECK(boss[2].speed == 121);
    REQUIRE(boss[0].skill_ids.size() == 3);

    // Rockslide : aoe=false dans SWARFARM, corrigé par skills_overrides.json.
    const auto* rockslide = data.skill(boss[0].skill_ids[0]);
    REQUIRE(rockslide);
    CHECK(rockslide->name == "Rockslide");
    CHECK(rockslide->aoe);
    CHECK(rockslide->overridden);
    CHECK(rockslide->multiplier_formula == "5.4*{ATK}");
    CHECK(data.overrides_applied() >= 1);

    // Compétence avec bonus de skill-up et recharge (Teshar S2).
    const auto* nova = data.skill(1180);
    REQUIRE(nova);
    CHECK(nova->cooltime == 4);
    CHECK(nova->upgrades.size() == 4);
    CHECK_FALSE(nova->overridden);

    // Le Géant est séparé de son abîme (donjon 9840).
    CHECK(data.dungeon(9840) != nullptr);
}

TEST_CASE("Dungeon model: content without waves is flagged, not a crash") {
    const auto& data = loaded().data;
    std::size_t without = 0;
    for (const auto& dungeon : data.dungeons())
        if (!data.has_wave_data(dungeon)) ++without;
    CHECK(without > 0);   // Rift, raids... : "données indisponibles"
    CHECK(without < data.dungeons().size());
}

TEST_CASE("Skill overrides replace only the given fields") {
    swcore::SkillData skill;
    skill.name = "skill_120003";
    skill.aoe = false;
    skill.cooltime = 4;
    skill.multiplier_formula = "18.0*{ATK}";
    swcore::SkillOverride override;
    override.name = "Cristal";
    override.cooltime = std::optional<int>{};  // null : plus de recharge
    override.note = "test";
    swcore::apply_skill_override(skill, override);
    CHECK(skill.name == "Cristal");
    CHECK_FALSE(skill.cooltime.has_value());
    CHECK_FALSE(skill.aoe);                              // inchangé
    CHECK(skill.multiplier_formula == "18.0*{ATK}");     // inchangé
    CHECK(skill.overridden);
    CHECK(skill.override_note == "test");
}

TEST_CASE("Team config GB12 loads, validates and checks the box") {
    std::vector<std::string> warnings;
    auto teams = swcore::load_team_configs(kRoot + "/data/teams",
                                           [&](const std::string& w) { warnings.push_back(w); });
    CHECK(warnings.empty());
    const auto it = std::find_if(teams.begin(), teams.end(), [](const swcore::TeamConfig& team) {
        return team.id == "gb12_speed_teshar";
    });
    REQUIRE(it != teams.end());
    auto& team = *it;
    CHECK(team.dungeon_id == 8001);
    CHECK(team.matches(8001, 685));
    CHECK_FALSE(team.matches(8001, 684));
    CHECK(team.leader == "Teshar");
    REQUIRE(team.monsters.size() == 5);
    REQUIRE(team.boss_script.size() == 6);
    CHECK(team.boss_script[1].fallback_skill == 1);
    CHECK(team.boss_script[2].target == "previous_actor");
    CHECK(team.rune_presets.at("set1").at("Konamiya").size() == 3);
    CHECK(team.rune_presets.at("set2").empty());

    const auto mapping = swcore::RuneMapping::load(kRoot + "/data/mapping");
    swcore::validate_team_config(team, loaded().reference, mapping.set_names);
    for (const auto& problem : team.problems) INFO(problem);
    CHECK(team.problems.empty());

    // Possession : box vide -> les 5 manquent ; avec Prilea seulement -> 4.
    std::vector<swcore::AccountMonster> box;
    CHECK(swcore::team_ownership(team, loaded().reference, box).missing.size() == 5);
    swcore::AccountMonster prilea;
    prilea.unit_master_id = loaded().reference.find_swarfarm(372)->com2us_id;
    box.push_back(prilea);
    const auto ownership = swcore::team_ownership(team, loaded().reference, box);
    CHECK(ownership.missing.size() == 4);
    CHECK(std::find(ownership.missing.begin(), ownership.missing.end(), "Prilea") ==
          ownership.missing.end());

    // Problèmes détectés : acteur inconnu, set inconnu, id qui ne correspond pas.
    auto broken = team;
    broken.boss_script.push_back({"Inconnu", 1, std::nullopt, "", {}});
    broken.rune_presets["set1"]["Prilea"].push_back("PasUnSet");
    broken.monsters[0].swarfarm_id = 579;  // Teshar, pas Prilea
    swcore::validate_team_config(broken, loaded().reference, mapping.set_names);
    CHECK(broken.problems.size() == 3);
}

TEST_CASE("Mechanics: values by dotted key, null means unverified") {
    const auto mechanics = swcore::Mechanics::load(kRoot + "/data/mechanics.json");
    REQUIRE(mechanics.loaded());
    CHECK(mechanics.number("tick_atb_percent") == 7.0);
    CHECK(mechanics.number("def_formula.c") == 3.5);
    CHECK(mechanics.text("tie_break") == "config_order");
    CHECK(mechanics.flag("per_monster.Teshar.tempest_every_attack") == false);
    CHECK(mechanics.number("per_monster.Teshar.tempest_cooldown") == 3);
    // Inconnu : null -> listé comme hypothèse non vérifiée, aucune valeur.
    CHECK_FALSE(mechanics.number("per_monster.Teshar.tempest_multiplier").has_value());
    const auto unknown = mechanics.unverified();
    CHECK(std::find(unknown.begin(), unknown.end(), "per_monster.Teshar.tempest_multiplier") !=
          unknown.end());
    CHECK_FALSE(swcore::Mechanics::parse("pas du json").loaded());
}

// ---------------------------------------------------------------------------
// Étape 2 : ATB, fenêtres de VIT, taux critique cible.
// ---------------------------------------------------------------------------

#include "swcore/atb.hpp"
#include "swcore/team_analysis.hpp"

namespace {

// Référence GB12 : ordre de départage = leader (Teshar), puis la team, puis la
// vague du boss (Géant, tour gauche, tour droite).
std::vector<swcore::AtbUnit> reference_units() {
    return {{"Teshar", true, 167, false},     {"Prilea", true, 226, false},
            {"Konamiya", true, 167, false},   {"Luna", true, 155, false},
            {"Homunculus", true, 153, false}, {"Giant", false, 90, true},
            {"TowerLeft", false, 90, false},  {"TowerRight", false, 121, false}};
}

std::vector<swcore::AtbStep> reference_script() {
    using swcore::AtbBoost;
    return {{1, 3, {}},
            {0, 2, {}},
            {2, 2, {{AtbBoost::Target::PreviousAlly, 0, 100}}},
            {0, 1, {}},
            {3, 3, {{AtbBoost::Target::Self, 0, 100}}},
            {4, 1, {}}};
}

}  // namespace

TEST_CASE("ATB: GB12 reference sequence") {
    const auto units = reference_units();
    const auto result = swcore::simulate_atb(units, reference_script());
    REQUIRE(result.boss_turn.has_value());
    const std::vector<std::pair<int, std::string>> expected{
        {7, "Prilea"}, {9, "Teshar"},  {10, "Konamiya"}, {11, "Teshar"},     {12, "Luna"},
        {13, "Homunculus"}, {14, "Luna"}, {15, "TowerRight"}, {16, "Prilea"}, {17, "Giant"}};
    REQUIRE(result.turns.size() >= expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("tour " << i);
        CHECK(result.turns[i].tick == expected[i].first);
        CHECK(units[result.turns[i].actor].name == expected[i].second);
    }
    CHECK(*result.boss_turn == 9);
    // Barres au moment de jouer (référence : 123,4 / 130,2 / 139,2 / 121,7).
    CHECK(std::abs(result.turns[3].bar - 123.38) < 0.01);
    CHECK(std::abs(result.turns[4].bar - 130.2) < 0.01);
    CHECK(std::abs(result.turns[5].bar - 139.23) < 0.01);
    CHECK(std::abs(result.turns[6].bar - 121.7) < 0.01);
    // Script : actions 1 à 6 aux tours 0 à 5 ; Luna t14 = tour en plus.
    for (int i = 0; i < 6; ++i) CHECK(result.turns[static_cast<std::size_t>(i)].step == i);
    CHECK(result.turns[6].step == -1);
    CHECK(result.script_before_boss);
    CHECK(result.steps_done_before_boss == 6);
    // Égalité Teshar / Konamiya au tic 9 : signalée.
    CHECK(result.turns[1].tie);
    CHECK_FALSE(result.warnings.empty());
    // Une ligne de barres par tic simulé.
    CHECK(result.bars.size() >= 17);
}

TEST_CASE("ATB: boost is an addition, reduction floors at 0") {
    using swcore::AtbBoost;
    // A (VIT 100) joue au tic 15 (105) et donne +100 à B (VIT 50 : 52,5 -> 152,5).
    const std::vector<swcore::AtbUnit> units{{"A", true, 100, false}, {"B", true, 50, false},
                                             {"Boss", false, 10, true}};
    std::vector<swcore::AtbStep> script{{0, 1, {{AtbBoost::Target::Ally, 1, 100}}}, {1, 1, {}}};
    auto result = swcore::simulate_atb(units, script);
    REQUIRE(result.turns.size() >= 2);
    CHECK(units[result.turns[1].actor].name == "B");
    CHECK(result.turns[1].tick == 16);  // 152,5 + 3,5 au tic suivant
    CHECK(std::abs(result.turns[1].bar - 156.0) < 0.01);
    // Réduction plus grande que la barre : plancher 0 (B rejoue au tic 15 + 29).
    script[0].boosts = {{AtbBoost::Target::Ally, 1, -500}};
    result = swcore::simulate_atb(units, script);
    REQUIRE(result.turns.size() >= 2);
    CHECK(units[result.turns[1].actor].name == "A");  // A rejoue avant B
}

TEST_CASE("Speed windows on the GB12 reference") {
    const auto units = reference_units();
    const auto script = reference_script();
    const auto window = [&](std::size_t unit) { return swcore::speed_window(units, script, unit); };
    const auto teshar = window(0);
    const auto prilea = window(1);
    const auto konamiya = window(2);
    const auto luna = window(3);
    const auto homunculus = window(4);
    for (const auto& w : {teshar, prilea, konamiya, luna, homunculus}) {
        CHECK(w.found);
        CHECK(w.current_inside);
    }
    CHECK(teshar.min == 167);    // égalité avec Konamiya : le leader joue d'abord
    CHECK(konamiya.max == 167);  // Konamiya ne doit pas dépasser Teshar
    CHECK(luna.min == 153);
    CHECK(luna.max == 160);
    CHECK(homunculus.max == 155);  // égalité avec Luna tolérée (Luna d'abord)
    // Prilea 179 : 8 tics x 12,53 = 100,24 -> joue au tic 8, script intact.
    CHECK(prilea.min == 179);
}

TEST_CASE("Team analysis GB12: reference speeds give the reference sequence and CR targets") {
    auto teams = swcore::load_team_configs(kRoot + "/data/teams");
    const auto it = std::find_if(teams.begin(), teams.end(),
                                 [](const auto& team) { return team.id == "gb12_speed_teshar"; });
    REQUIRE(it != teams.end());
    const auto mechanics = swcore::Mechanics::load(kRoot + "/data/mechanics.json");
    swcore::TeamAnalysisInput input;
    input.team = &*it;
    input.data = &loaded().data;
    input.dungeon = loaded().data.dungeon(8001);
    input.level = loaded().data.level(685);
    input.reference = &loaded().reference;
    input.mechanics = &mechanics;
    input.manual_speeds = {{"Prilea", 226}, {"Teshar", 167}, {"Konamiya", 167},
                           {"Luna", 155},   {"Homunculus", 153}};
    const auto analysis = swcore::analyze_team(input);
    REQUIRE(analysis.valid);
    REQUIRE(analysis.boss);
    CHECK(analysis.boss->name == "Ancient Giant");
    // Leader (Teshar) en premier, puis la team, puis la vague du boss.
    REQUIRE(analysis.units.size() == 8);
    CHECK(analysis.units[0].name == "Teshar");
    CHECK(analysis.units[5].boss);
    CHECK(analysis.atb.script_before_boss);
    std::vector<std::string> order;
    for (const auto& turn : analysis.atb.turns) order.push_back(analysis.units[turn.actor].name);
    REQUIRE(order.size() >= 10);
    CHECK(order[0] == "Prilea");
    CHECK(order[2] == "Konamiya");
    CHECK(order[4] == "Luna");        // Luna S3 : +100 à elle-même (donnée SWARFARM)
    CHECK(order[6] == "Luna");
    CHECK(order[9] == "Ancient Giant");
    CHECK(analysis.atb.turns[9].tick == 17);

    // Taux critique : leader Teshar 33 % (Dungeon), avantage vent > eau 15 %.
    for (const auto& member : analysis.members) {
        const auto& name = member.config->name;
        INFO(name);
        if (name == "Konamiya") {
            CHECK_FALSE(member.crit.needed);  // S2 sans dégâts
            continue;
        }
        CHECK(member.crit.needed);
        CHECK(member.crit.leader == 33);
        CHECK(member.crit.required_runes == (name == "Luna" ? 52 : 37));
    }
}

// ---------------------------------------------------------------------------
// Étape 3 : dégâts, pire / meilleur cas, verdict. Étape 4 : optimisation.
// ---------------------------------------------------------------------------

#include "swcore/team_optimizer.hpp"

#include <set>

namespace {

swcore::TeamAnalysisInput gb12_input(const swcore::TeamConfig& team,
                                     const swcore::Mechanics& mechanics) {
    swcore::TeamAnalysisInput input;
    input.team = &team;
    input.data = &loaded().data;
    input.dungeon = loaded().data.dungeon(8001);
    input.level = loaded().data.level(685);
    input.reference = &loaded().reference;
    input.mechanics = &mechanics;
    return input;
}

swcore::TeamConfig gb12_team() {
    auto teams = swcore::load_team_configs(kRoot + "/data/teams");
    for (auto& team : teams)
        if (team.id == "gb12_speed_teshar") return team;
    FAIL("team GB12 absente");
    return {};
}

}  // namespace

TEST_CASE("Skill formulas: SWARFARM expressions and skill-up bonus") {
    std::vector<std::string> unknown;
    const std::map<std::string, double> vars{{"ATK", 1000}, {"Target Current HP %", 1.0},
                                             {"Target MAX HP", 200000}, {"DEF", 500}};
    CHECK(swcore::evaluate_skill_formula("{ATK}*(4.0*{Target Current HP %} + 5.0)", vars) == 9000);
    CHECK(swcore::evaluate_skill_formula("7.2*{ATK} + 0.15*{Target MAX HP}", vars) == 37200);
    CHECK(swcore::evaluate_skill_formula("3.7*{ATK} - 15", vars) == 3685);
    CHECK(swcore::evaluate_skill_formula("1.8*{ATK} + 2.5*{DEF}", vars) == 3050);
    CHECK(swcore::evaluate_skill_formula("2*{SPD} + 1", vars, &unknown) == 1);
    REQUIRE(unknown.size() == 1);
    CHECK(unknown.front() == "SPD");
    CHECK(swcore::evaluate_skill_formula("", vars) == 0);
    const auto* nova = loaded().data.skill(1180);
    REQUIRE(nova);
    CHECK(swcore::skill_damage_bonus(*nova) == 20);  // 2 x "Damage +10%"
}

TEST_CASE("GB12 damage: worst and best case, counter, survival, what is missing") {
    const auto team = gb12_team();
    const auto mechanics = swcore::Mechanics::load(kRoot + "/data/mechanics.json");
    auto input = gb12_input(team, mechanics);
    input.manual_speeds = {{"Prilea", 226}, {"Teshar", 167}, {"Konamiya", 167},
                           {"Luna", 155},   {"Homunculus", 153}};
    const auto analysis = swcore::analyze_team(input);  // sans box : stats de base
    REQUIRE(analysis.valid);
    const auto& worst = analysis.worst;
    const auto& best = analysis.best;
    REQUIRE(worst.actions.size() >= 6);

    // Action 1 : Prilea S3, 7.0 x ATQ, sans critique au pire cas (CR 63 %), DEF intacte.
    const auto& prilea = worst.actions[0];
    CHECK(prilea.actor == "Prilea");
    CHECK(prilea.skill == 3);
    CHECK_FALSE(prilea.crit);
    CHECK_FALSE(prilea.def_broken);
    const auto* boar_hunt = loaded().data.skill(449);
    REQUIRE(boar_hunt);
    const auto* prilea_ref = loaded().reference.find_swarfarm(372);
    REQUIRE(prilea_ref);
    const double expected = 7.0 * *prilea_ref->max_lvl_attack *
                            (1.0 + swcore::skill_damage_bonus(*boar_hunt) / 100.0) * 1000.0 /
                            (1140.0 + 3.5 * analysis.boss->defense);
    CHECK(std::abs(prilea.damage - expected) < 1e-6);

    // Action 2 : Teshar, repli S1 au pire cas ; DEF brisée par Prilea.
    CHECK(worst.actions[1].actor == "Teshar");
    CHECK(worst.actions[1].fallback);
    CHECK(worst.actions[1].skill == 1);
    CHECK(worst.actions[1].def_broken);
    // Meilleur cas : S2, critique.
    CHECK(best.actions[1].skill == 2);
    CHECK(best.actions[1].crit);
    // Konamiya S2 : ATQ boostée pour la prochaine action de Teshar.
    CHECK(worst.actions[3].actor == "Teshar");
    CHECK(worst.actions[3].atk_buff);
    CHECK(best.damage > worst.damage);

    // Stats de base : le boss survit ; il manque un facteur > 1 sur ATQ x (1 + DC).
    CHECK_FALSE(worst.kills);
    CHECK(worst.required_factor > 1);
    CHECK_FALSE(analysis.missing.empty());
    // 7e attaque reçue (Prilea, tour en plus au tic 16) : contre-attaque du Géant.
    REQUIRE(worst.counter.has_value());
    CHECK(worst.counter->find("Prilea") != std::string::npos);
    // Survie : une ligne par monstre ; hypothèse inconnue de Teshar signalée.
    CHECK(analysis.survival.size() == 5);
    CHECK(std::find(analysis.unverified.begin(), analysis.unverified.end(),
                    "per_monster.Teshar.tempest_multiplier") != analysis.unverified.end());
    // Poids c_i : nukers > 0, Konamiya (sans dégâts) = 0.
    for (const auto& member : analysis.members) {
        if (member.config->name == "Konamiya") CHECK(member.coefficient == 0);
        else CHECK(member.coefficient > 0);
    }
    CHECK(analysis.score > 0);
}

TEST_CASE("Preset sets become solver scenarios") {
    const auto mapping = swcore::RuneMapping::load(kRoot + "/data/mapping");
    const auto effects = swcore::SetEffects::load(kRoot + "/data/mapping/set_effects.json");
    std::string problem;
    const auto swift =
        swcore::scenario_from_sets({"Swift", "Fight"}, mapping.set_names, effects, &problem);
    REQUIRE(swift);
    REQUIRE(swift->sets.size() == 2);
    CHECK(swift->sets[0].set_id == 3);
    CHECK(swift->sets[0].pieces == 4);
    CHECK(swift->sets[1].set_id == 19);
    CHECK(swift->sets[1].pieces == 2);
    const auto fight = swcore::scenario_from_sets({"Fight", "Fight", "Fight"}, mapping.set_names,
                                                  effects, &problem);
    REQUIRE(fight);
    REQUIRE(fight->sets.size() == 1);
    CHECK(fight->sets[0].pieces == 6);
    CHECK(swcore::scenario_from_sets({}, mapping.set_names, effects, &problem)->any_set);
    CHECK_FALSE(swcore::scenario_from_sets({"Swift"}, mapping.set_names, effects, &problem));
    CHECK(problem.find("4") != std::string::npos);
    CHECK_FALSE(swcore::scenario_from_sets({"PasUnSet"}, mapping.set_names, effects, &problem));
}

TEST_CASE("Team optimizer: distinct runes, locked member untouched, constraints kept") {
    const auto team = gb12_team();
    const auto mechanics = swcore::Mechanics::load(kRoot + "/data/mechanics.json");
    const auto mapping = swcore::RuneMapping::load(kRoot + "/data/mapping");
    const auto effects = swcore::SetEffects::load(kRoot + "/data/mapping/set_effects.json");
    const std::string export_path = kRoot + "/tests/data/Qui-Gon-21196058-2026-11-02_001212.json";
    auto box = swcore::load_account(export_path, mapping, {});
    for (auto& monster : box) monster.stats = swcore::calculate_monster_stats(monster, effects);
    const auto runes = swcore::load_runes(export_path, mapping, {});

    swcore::TeamOptimizerInput input;
    input.analysis = gb12_input(team, mechanics);
    input.box = &box;
    input.runes = &runes;
    input.effects = &effects;
    input.mapping_directory = kRoot + "/data/mapping";
    input.set_names = &mapping.set_names;
    swcore::TeamOptimizerSettings settings;
    settings.sets = team.rune_presets.at("set1");
    settings.locked = {"Konamiya"};
    settings.time_budget_ms = 1500;
    settings.max_passes = 1;
    const auto result = swcore::optimize_team(input, settings, std::stop_token{});
    REQUIRE(result.members.size() == 5);
    REQUIRE(result.units.size() == 5);

    // Konamiya verrouillée : même exemplaire, mêmes runes qu'avant.
    const auto find = [](const std::vector<swcore::AccountMonster>& monsters,
                         std::uint64_t unit_id) {
        return std::find_if(monsters.begin(), monsters.end(),
                            [&](const auto& unit) { return unit.unit_id == unit_id; });
    };
    const auto konamiya_id = result.units.at("Konamiya");
    const auto before = find(box, konamiya_id);
    const auto after = find(result.box, konamiya_id);
    REQUIRE(before != box.end());
    REQUIRE(after != result.box.end());
    std::set<std::uint64_t> before_ids;
    std::set<std::uint64_t> after_ids;
    for (const auto& rune : before->runes) before_ids.insert(rune.rune_id);
    for (const auto& rune : after->runes) after_ids.insert(rune.rune_id);
    CHECK(before_ids == after_ids);

    // Aucune rune portée par deux membres de la team ; builds conformes aux
    // contraintes de leur recherche.
    std::set<std::uint64_t> used;
    for (const auto& [name, unit_id] : result.units) {
        const auto unit = find(result.box, unit_id);
        REQUIRE(unit != result.box.end());
        for (const auto& rune : unit->runes) CHECK(used.insert(rune.rune_id).second);
    }
    for (const auto& member : result.members) {
        INFO(member.name);
        if (member.locked) CHECK_FALSE(member.build.has_value());
        if (!member.build) continue;
        const auto& total = member.build->stats.total;
        for (const auto& [metric, constraint] : member.config.constraints) {
            const double value = metric == "speed" ? total.speed : total.crit_rate;
            if (constraint.minimum) CHECK(value >= *constraint.minimum);
            if (constraint.maximum) CHECK(value <= *constraint.maximum);
        }
    }
    CHECK_FALSE(result.log.empty());
    CHECK_FALSE(result.cancelled);
}
