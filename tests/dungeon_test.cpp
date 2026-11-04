// Étape 1 du module donjon : chargement du modèle (donjons, étages, vagues,
// compétences + overrides), configs de teams, mechanics.json. Aucun calcul.

#include <catch2/catch_test_macros.hpp>

#include "swcore/account.hpp"
#include "swcore/dungeon.hpp"
#include "swcore/runes.hpp"
#include "swcore/team_config.hpp"

#include <algorithm>
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
