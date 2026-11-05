// Étape 1 du module donjon : chargement du modèle (donjons, étages, vagues,
// compétences + overrides), configs de teams, mechanics.json. Aucun calcul.

#include <catch2/catch_test_macros.hpp>

#include "swcore/account/account.hpp"
#include "swcore/dungeon/dungeon.hpp"
#include "swcore/runes/runes.hpp"
#include "swcore/dungeon/team_config.hpp"
#include "swcore/dungeon/team_analysis.hpp"

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
        kRoot + "/data/swarfarm", kRoot + "/data/game/monsters/skills_overrides.json", reference,
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

    const auto mapping = swcore::RuneMapping::load(kRoot + "/data/game");
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
    const auto mechanics = swcore::Mechanics::load(kRoot + "/data/game/combat/mechanics.json");
    REQUIRE(mechanics.loaded());
    CHECK(mechanics.number("tick_atb_percent") == 7.0);
    CHECK(mechanics.number("def_formula.c") == 3.5);
    CHECK(mechanics.text("tie_break") == "config_order");
    CHECK(mechanics.flag("fend_off_counts_passive_extra_attack") == true);
    CHECK(mechanics.flag("boss_immune_to_stun") == true);
    CHECK(mechanics.number("resist_floor_percent") == 15);
    // Tempest vient maintenant des données (fusion other_skill), plus de mechanics.json.
    CHECK_FALSE(mechanics.has("per_monster.Teshar.tempest_multiplier"));
    CHECK_FALSE(mechanics.has("per_monster.Teshar.tempest_every_attack"));
    // Inconnu : null -> listé comme hypothèse non vérifiée, aucune valeur.
    const auto parsed = swcore::Mechanics::parse(R"({"known": 2, "group": {"unknown": null}})");
    REQUIRE(parsed.loaded());
    CHECK_FALSE(parsed.number("group.unknown").has_value());
    const auto unknown = parsed.unverified();
    CHECK(std::find(unknown.begin(), unknown.end(), "group.unknown") != unknown.end());
    CHECK_FALSE(swcore::Mechanics::parse("pas du json").loaded());
}

TEST_CASE("Skill merge: other_skill parts complete the main skill (Tempest)") {
    const auto& data = loaded().data;
    // 1181 "Tempest (Passive)" (Teshar) <- 2982 : "3.7*{ATK}", AoE, used_on vide.
    const auto* tempest = data.skill(1181);
    REQUIRE(tempest);
    CHECK(tempest->passive);
    CHECK(tempest->multiplier_formula == "3.7*{ATK}");
    CHECK_FALSE(tempest->multiplier_formula_raw.empty());
    CHECK(tempest->aoe);
    REQUIRE(tempest->merged_from.size() == 1);
    CHECK(tempest->merged_from.front() == 2982);
    // Skill-ups de la principale : +30 % de dégâts, recharge 4 - 1 = 3.
    CHECK(swcore::skill_damage_bonus(*tempest) == 30);
    CHECK(swcore::skill_cooldown(*tempest) == 3);
    CHECK(std::any_of(tempest->effects.begin(), tempest->effects.end(),
                      [](const swcore::SkillEffect& effect) { return effect.name == "Stun"; }));
    CHECK(std::any_of(tempest->effects.begin(), tempest->effects.end(),
                      [](const swcore::SkillEffect& effect) {
                          return effect.name == "Reduce Cooltime" && effect.on_death;
                      }));
    CHECK(swcore::is_after_attack_passive(*tempest));
    CHECK(data.skills_merged() > 1);  // générique : pas seulement Teshar

    // Principale qui a déjà un multiplicateur : non touchée.
    swcore::SkillData main;
    main.multiplier_formula = "3.8*{ATK}";
    swcore::SkillData part;
    part.id = 1;
    part.multiplier_formula = "3.0*{ATK}";
    CHECK_FALSE(swcore::merge_other_skill(main, part));
    CHECK(main.multiplier_formula == "3.8*{ATK}");

    // Trous : compétences de monstres qui frappent, toujours sans multiplicateur.
    const auto& gaps = data.damage_gaps();
    CHECK_FALSE(gaps.empty());
    CHECK(std::find(gaps.begin(), gaps.end(), 1181) == gaps.end());
    for (const int id : gaps) {
        const auto* skill = data.skill(id);
        REQUIRE(skill);
        CHECK(skill->multiplier_formula.empty());
        CHECK_FALSE(skill->used_on.empty());
    }
}

// ---------------------------------------------------------------------------
// Étape 2 : ATB, fenêtres de VIT, taux critique cible.
// ---------------------------------------------------------------------------

#include "swcore/dungeon/atb.hpp"
#include "swcore/dungeon/team_analysis.hpp"

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

TEST_CASE("ATB: stunned enemy loses its turn, dead enemy never plays") {
    const auto units = reference_units();
    // Après le 1er tour de Teshar (t9) : les deux tours sont étourdies.
    swcore::AtbOptions options;
    options.events = {{0, 0, {6, 7}, {}}};
    const auto stunned = swcore::simulate_atb(units, reference_script(), options);
    REQUIRE(stunned.boss_turn.has_value());
    bool right_lost = false;
    for (const auto& turn : stunned.turns)
        if (turn.actor == 7 && turn.tick == 15) right_lost = turn.stunned;
    CHECK(right_lost);  // t15 : tour droite étourdie, tour perdu
    CHECK(stunned.turns[*stunned.boss_turn].tick == 17);
    CHECK_FALSE(stunned.turns[*stunned.boss_turn].stunned);

    // Tour droite tuée au 1er tour de Teshar : elle ne joue plus.
    options.events = {{0, 0, {}, {7}}};
    const auto killed = swcore::simulate_atb(units, reference_script(), options);
    for (const auto& turn : killed.turns) CHECK(turn.actor != 7);
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

TEST_CASE("Strict speed check: ties and broken order are rejected") {
    // Référence telle quelle : Teshar et Konamiya à 167 jouent à égalité au tic
    // 9 (ordre non garanti) -> refusé, il faut monter Teshar.
    auto units = reference_units();
    const auto script = reference_script();
    auto check = swcore::check_speed_plan(units, script);
    CHECK_FALSE(check.ok);
    REQUIRE(check.raise.has_value());
    CHECK(units[*check.raise].name == "Teshar");
    // Teshar 168 : plus d'égalité, script complet avant le Géant.
    units[0].speed = 168;
    check = swcore::check_speed_plan(units, script);
    CHECK(check.ok);
    // Konamiya plus rapide que Teshar : Konamiya joue avant l'action 2 alors
    // qu'elle a encore l'action 3 -> ordre cassé, il faut monter Teshar.
    units[2].speed = 170;
    check = swcore::check_speed_plan(units, script);
    CHECK_FALSE(check.ok);
    REQUIRE(check.raise.has_value());
    CHECK(units[*check.raise].name == "Teshar");
}

TEST_CASE("Speed windows on the GB12 reference (strict, Teshar 168)") {
    auto units = reference_units();
    units[0].speed = 168;
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
    CHECK(teshar.min == 168);      // 167 : égalité avec Konamiya, refusée
    CHECK(konamiya.max == 167);    // Teshar > Konamiya, 1 point d'écart
    CHECK(luna.min == 154);        // 153 : égalité avec Homonculus, refusée
    CHECK(luna.max == 160);        // 161 : Luna joue avant l'action 4 de Teshar
    CHECK(homunculus.max == 154);  // Luna > Homonculus
    // Prilea 179 : 8 tics x 12,53 = 100,24 -> joue au tic 8, script intact.
    CHECK(prilea.min == 179);
}

TEST_CASE("Joint speed plan from minimal speeds") {
    // Toutes les alliées à 100 : le plan monte le fautif de 1 jusqu'au succès.
    auto units = reference_units();
    for (std::size_t i = 0; i < 5; ++i) units[i].speed = 100;
    const auto script = reference_script();
    const auto plan = swcore::plan_speeds(units, script);
    REQUIRE(plan.found);
    for (std::size_t i = 0; i < 5; ++i) units[i].speed = plan.speeds[i];
    CHECK(swcore::check_speed_plan(units, script).ok);
    CHECK(plan.speeds[0] > plan.speeds[2]);  // Teshar > Konamiya
    CHECK(plan.speeds[3] > plan.speeds[4]);  // Luna > Homonculus
    // On ne fait que monter les VIT ; les ennemis ne bougent pas.
    for (std::size_t i = 0; i < 5; ++i) CHECK(plan.speeds[i] >= 100);
    for (std::size_t i = 5; i < units.size(); ++i)
        CHECK(plan.speeds[i] == reference_units()[i].speed);
    // Ordre du plan : Prilea, Teshar, Konamiya, Teshar, Luna, Homonculus avant le Géant.
    const auto atb = swcore::simulate_atb(units, script);
    std::vector<std::string> allies;
    for (std::size_t t = 0; t < *atb.boss_turn; ++t)
        if (units[atb.turns[t].actor].ally && atb.turns[t].step >= 0)
            allies.push_back(units[atb.turns[t].actor].name);
    CHECK(allies == std::vector<std::string>{"Prilea", "Teshar", "Konamiya", "Teshar", "Luna",
                                             "Homunculus"});
}

TEST_CASE("Tower bonuses at level 20 and combat stats") {
    const auto towers =
        swcore::load_tower_bonuses(kRoot + "/data/game/account/summoner_skills.json", 20);
    REQUIRE(towers.loaded);
    CHECK(towers.speed == 15);
    CHECK(towers.crit_damage == 25);
    CHECK(towers.attack == 20);
    CHECK(towers.element_attack.at(swcore::Element::Wind) == 21);
    // Prilea sans runes : 105 + ceil(105 x 15 %) = 121 de VIT de combat.
    const auto bonus = swcore::combat_bonus(swcore::Element::Wind, &towers, std::nullopt,
                                            "Cairos Dungeon");
    const swcore::StatValues white{8235, 736, 527, 105, 15, 50, 15, 0};
    const auto combat = swcore::combat_stats(white, white, bonus);
    CHECK(combat.speed == 121);
    CHECK(combat.crit_damage == 75);
    CHECK(combat.attack == 736 + static_cast<int>(std::ceil(736 * 0.41)));  // tour 20 + vent 21
    // Leader Teshar (CR 33 %, donjon) : seulement dans les contenus de type donjon.
    swcore::LeaderSkill teshar{138, 33, "Dungeon", "Critical Rate", std::nullopt};
    CHECK(swcore::combat_bonus(swcore::Element::Wind, nullptr, teshar, "Cairos Dungeon").crit_rate ==
          33);
    CHECK(swcore::combat_bonus(swcore::Element::Wind, nullptr, teshar, "Scenario").crit_rate == 0);
}

TEST_CASE("Team analysis GB12: reference speeds give the reference sequence and CR targets") {
    auto teams = swcore::load_team_configs(kRoot + "/data/teams");
    const auto it = std::find_if(teams.begin(), teams.end(),
                                 [](const auto& team) { return team.id == "gb12_speed_teshar"; });
    REQUIRE(it != teams.end());
    const auto mechanics = swcore::Mechanics::load(kRoot + "/data/game/combat/mechanics.json");
    swcore::TeamAnalysisInput input;
    input.team = &*it;
    input.data = &loaded().data;
    input.dungeon = loaded().data.dungeon(8001);
    input.level = loaded().data.level(685);
    input.reference = &loaded().reference;
    input.mechanics = &mechanics;
    input.assumed_speeds = {{"Prilea", 226}, {"Teshar", 167}, {"Konamiya", 167},
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

#include "swcore/dungeon/team_optimizer.hpp"

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
    const auto mechanics = swcore::Mechanics::load(kRoot + "/data/game/combat/mechanics.json");
    auto input = gb12_input(team, mechanics);
    input.assumed_speeds = {{"Prilea", 226}, {"Teshar", 167}, {"Konamiya", 167},
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
    // Tempest (prête sur le boss, hypothèse de la config) : ligne séparée au même
    // tic, zone sur le boss et les deux tours ; 3,7 x ATQ x 1,30 x facteur DEF.
    const auto& tempest = worst.actions[2];
    CHECK(tempest.extra);
    CHECK(tempest.actor == "Teshar");
    CHECK(tempest.tick == worst.actions[1].tick);
    CHECK(tempest.skill_name == "Tempest");
    CHECK(worst.actions[1].passive == "+ Tempest");
    REQUIRE(tempest.targets.size() == 3);
    const auto teshar = std::find_if(analysis.members.begin(), analysis.members.end(),
                                     [](const auto& m) { return m.config->name == "Teshar"; });
    REQUIRE(teshar != analysis.members.end());
    const double tempest_expected = 3.7 * teshar->combat.attack * 1.30 * 1000.0 /
                                    (1140.0 + 3.5 * analysis.boss->defense * 0.3);
    CHECK(std::abs(tempest.damage - tempest_expected) < 1e-6);
    for (const auto& hit : tempest.targets) {
        INFO(hit.name);
        if (hit.boss) {
            CHECK(hit.stun_text == "insensible au stun");
            CHECK_FALSE(hit.stun_chance.has_value());
        } else {
            CHECK(hit.stun_chance.has_value());
            CHECK_FALSE(hit.stunned);  // pire cas : résisté
        }
        CHECK_FALSE(hit.kills);
    }
    CHECK(tempest.boss_hp_after < worst.actions[1].boss_hp_after);
    CHECK_FALSE(analysis.hypotheses.empty());
    // Konamiya S2 : ATQ boostée pour la prochaine action de Teshar, sans Tempest (recharge 3).
    CHECK(worst.actions[4].actor == "Teshar");
    CHECK(worst.actions[4].atk_buff);
    CHECK_FALSE(worst.actions[4].extra);
    CHECK(worst.actions[4].passive.find("Tempest en recharge : 3") != std::string::npos);
    CHECK_FALSE(worst.actions[5].extra);
    CHECK(best.damage > worst.damage);
    // Meilleur cas : tours étourdies -> au moins un tour ennemi perdu avant le boss.
    CHECK(std::any_of(best.atb.turns.begin(), best.atb.turns.end(),
                      [](const swcore::AtbTurn& turn) { return turn.stunned; }));
    CHECK_FALSE(best.stun_notes.empty());

    // Stats de base : le boss survit ; il manque un facteur > 1 sur ATQ x (1 + DC).
    CHECK_FALSE(worst.kills);
    CHECK(worst.required_factor > 1);
    CHECK_FALSE(analysis.missing.empty());
    // 7e attaque reçue (Tempest compte) : Luna au tic 14 (IA auto), contre-attaque du Géant.
    REQUIRE(worst.counter.has_value());
    CHECK(worst.counter->find("Luna") != std::string::npos);
    CHECK(worst.counter->find("t14") != std::string::npos);
    // Survie : une ligne par monstre ; plus d'hypothèse Tempest dans mechanics.json.
    CHECK(analysis.survival.size() == 5);
    CHECK(analysis.unverified.empty());
    // Poids c_i : nukers > 0, Konamiya (sans dégâts) = 0.
    for (const auto& member : analysis.members) {
        if (member.config->name == "Konamiya") CHECK(member.coefficient == 0);
        else CHECK(member.coefficient > 0);
    }
    CHECK(analysis.score > 0);
}

TEST_CASE("Preset sets become solver scenarios") {
    const auto mapping = swcore::RuneMapping::load(kRoot + "/data/game");
    const auto effects = swcore::SetEffects::load(kRoot + "/data/game/runes/set_effects.json");
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
    const auto mechanics = swcore::Mechanics::load(kRoot + "/data/game/combat/mechanics.json");
    const auto mapping = swcore::RuneMapping::load(kRoot + "/data/game");
    const auto effects = swcore::SetEffects::load(kRoot + "/data/game/runes/set_effects.json");
    const std::string export_path = kRoot + "/tests/data/exports/Qui-Gon-21196058-2026-11-02_001212.json";
    auto box = swcore::load_account(export_path, mapping, {});
    for (auto& monster : box) monster.stats = swcore::calculate_monster_stats(monster, effects);
    const auto runes = swcore::load_runes(export_path, mapping, {});

    const auto towers =
        swcore::load_tower_bonuses(kRoot + "/data/game/account/summoner_skills.json", 20);
    swcore::TeamOptimizerInput input;
    input.analysis = gb12_input(team, mechanics);
    input.analysis.effects = &effects;
    input.analysis.towers = &towers;
    input.box = &box;
    input.runes = &runes;
    input.mapping_directory = kRoot + "/data/game";
    input.set_names = &mapping.set_names;
    swcore::TeamOptimizerSettings settings;
    settings.sets = team.rune_presets.at("set1");
    settings.locked = {"Konamiya"};
    settings.time_budget_ms = 1500;
    settings.max_passes = 1;
    const auto result = swcore::optimize_team(input, settings, std::stop_token{});
    REQUIRE(result.members.size() == 5);
    REQUIRE(result.units.size() == 5);

    // Konamiya verrouillée : son build est exactement ses runes actuelles.
    const auto konamiya_id = result.units.at("Konamiya");
    const auto before = std::find_if(box.begin(), box.end(),
                                     [&](const auto& unit) { return unit.unit_id == konamiya_id; });
    REQUIRE(before != box.end());
    std::set<std::uint64_t> before_ids;
    std::set<std::uint64_t> after_ids;
    for (const auto& rune : before->runes) before_ids.insert(rune.rune_id);
    REQUIRE(result.builds.contains("Konamiya"));
    for (const auto& rune : result.builds.at("Konamiya")) after_ids.insert(rune.rune_id);
    CHECK(before_ids == after_ids);

    // Aucune rune dans deux builds candidats ; la box n'est jamais modifiée.
    std::set<std::uint64_t> used;
    for (const auto& [name, build] : result.builds)
        for (const auto& rune : build) CHECK(used.insert(rune.rune_id).second);

    // "Contraintes tenues" = vrai sur la simulation finale, recalculée ici.
    auto check_input = input.analysis;
    check_input.box = &box;
    check_input.builds = result.builds;
    check_input.pinned_units = result.units;
    const auto final_analysis = swcore::analyze_team(check_input);
    REQUIRE(final_analysis.valid);
    CHECK(result.constraints_ok == final_analysis.constraints_ok);
    CHECK(result.kills == final_analysis.worst.kills);
    if (result.constraints_ok) {
        CHECK(final_analysis.speed_check.ok);  // pas d'égalité, ordre respecté
        for (const auto& member : final_analysis.members) CHECK(member.crit.satisfied());
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
