#include "swcore/dungeon/team_analysis.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

namespace swcore {

namespace {

// Compétence de dégâts : active, avec un multiplicateur.
bool deals_damage(const SkillData* skill) {
    return skill && !skill->passive && !skill->multiplier_formula.empty();
}

int rounded(double value, const std::string& mode) {
    if (mode == "floor") return static_cast<int>(std::floor(value + 1e-9));
    if (mode == "round") return static_cast<int>(std::lround(value));
    return static_cast<int>(std::ceil(value - 1e-9));  // "ceil" (défaut)
}

// "atb_boost_100" -> 100 ; "atb_reduce_50" -> -50 ; sinon nullopt.
std::optional<double> atb_effect(const std::string& effect) {
    for (const auto& [prefix, sign] :
         {std::pair{"atb_boost_", 1.0}, std::pair{"atb_reduce_", -1.0}}) {
        const std::string text = prefix;
        if (effect.rfind(text, 0) != 0) continue;
        try {
            return sign * std::stod(effect.substr(text.size()));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::string number_text(double value, int decimals = 0) {
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}

// Analyseur de formules : nombres, {variables}, identifiants, + - * / ( ).
class FormulaParser {
public:
    FormulaParser(const std::string& text, const std::map<std::string, double>& variables,
                  std::vector<std::string>* unknown)
        : text_(text), variables_(variables), unknown_(unknown) {}

    double parse() { return expression(); }

private:
    void skip() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_])))
            ++pos_;
    }
    bool accept(char c) {
        skip();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }
    double expression() {
        double value = term();
        while (true) {
            if (accept('+')) value += term();
            else if (accept('-')) value -= term();
            else return value;
        }
    }
    double term() {
        double value = factor();
        while (true) {
            if (accept('*')) {
                value *= factor();
            } else if (accept('/')) {
                const double divisor = factor();
                value = divisor != 0 ? value / divisor : 0;
            } else {
                return value;
            }
        }
    }
    double variable(const std::string& name) {
        const auto it = variables_.find(name);
        if (it != variables_.end()) return it->second;
        if (unknown_ && std::find(unknown_->begin(), unknown_->end(), name) == unknown_->end())
            unknown_->push_back(name);
        return 0;
    }
    double factor() {
        skip();
        if (accept('-')) return -factor();
        if (accept('+')) return factor();
        if (accept('(')) {
            const double value = expression();
            accept(')');
            return value;
        }
        if (pos_ < text_.size() && text_[pos_] == '{') {
            const auto end = text_.find('}', pos_);
            const std::string name =
                text_.substr(pos_ + 1, end == std::string::npos ? std::string::npos : end - pos_ - 1);
            pos_ = end == std::string::npos ? text_.size() : end + 1;
            return variable(name);
        }
        const std::size_t start = pos_;
        if (pos_ < text_.size() &&
            (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.')) {
            while (pos_ < text_.size() &&
                   (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.'))
                ++pos_;
            try {
                return std::stod(text_.substr(start, pos_ - start));
            } catch (const std::exception&) {
                return 0;
            }
        }
        // Identifiant brut (ex. TARGET_CUR_HP) : inconnu.
        while (pos_ < text_.size() &&
               (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_'))
            ++pos_;
        if (pos_ > start) return variable(text_.substr(start, pos_ - start));
        if (pos_ < text_.size()) ++pos_;  // caractère inattendu : ignoré
        return 0;
    }

    const std::string& text_;
    const std::map<std::string, double>& variables_;
    std::vector<std::string>* unknown_;
    std::size_t pos_ = 0;
};

enum class Mode { Worst, Best, Score };

}  // namespace

double evaluate_skill_formula(const std::string& formula,
                              const std::map<std::string, double>& variables,
                              std::vector<std::string>* unknown) {
    if (formula.empty()) return 0;
    return FormulaParser(formula, variables, unknown).parse();
}

double skill_damage_bonus(const SkillData& skill) {
    double bonus = 0;
    for (const auto& upgrade : skill.upgrades)
        if (upgrade.effect.find("Damage") != std::string::npos) bonus += upgrade.amount;
    return bonus;
}

std::optional<int> skill_cooldown(const SkillData& skill) {
    if (!skill.cooltime) return std::nullopt;
    int cooldown = *skill.cooltime;
    for (const auto& upgrade : skill.upgrades)
        if (upgrade.effect.find("Cooltime") != std::string::npos)
            cooldown -= static_cast<int>(upgrade.amount);
    return std::max(1, cooldown);
}

bool is_after_attack_passive(const SkillData& skill) {
    if (!skill.passive || skill.multiplier_formula.empty()) return false;
    std::string text = skill.description;
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text.find("after you attack") != std::string::npos ||
           text.find("after attacking") != std::string::npos;
}

const AccountMonster* choose_box_unit(const std::vector<AccountMonster>& box, int com2us_id) {
    const AccountMonster* best = nullptr;
    const auto runes = [](const AccountMonster& unit) {
        return std::count_if(unit.runes.begin(), unit.runes.end(), [](const Rune& rune) {
            return rune.occupancy == RuneOccupancy::Equipped;
        });
    };
    for (const auto& unit : box) {
        if (unit.unit_master_id != com2us_id) continue;
        if (!best || runes(unit) > runes(*best) ||
            (runes(unit) == runes(*best) &&
             (unit.level > best->level || (unit.level == best->level && unit.stars > best->stars))))
            best = &unit;
    }
    return best;
}

bool has_element_advantage(Element attacker, Element defender) {
    return (attacker == Element::Water && defender == Element::Fire) ||
           (attacker == Element::Fire && defender == Element::Wind) ||
           (attacker == Element::Wind && defender == Element::Water) ||
           (attacker == Element::Light && defender == Element::Dark) ||
           (attacker == Element::Dark && defender == Element::Light);
}

bool leader_applies(const LeaderSkill& leader, const std::string& dungeon_category,
                    Element member_element) {
    if (leader.area == "General") return true;
    // "Dungeon" : contenus de type donjon (Cairos, donjons secrets).
    if (leader.area == "Dungeon")
        return dungeon_category == "Cairos Dungeon" || dungeon_category == "Secret Dungeon";
    if (leader.area == "Element") return leader.element && *leader.element == member_element;
    return false;  // Arena, Guild
}

CombatBonus combat_bonus(Element element, const TowerBonuses* towers,
                         const std::optional<LeaderSkill>& leader,
                         const std::string& dungeon_category) {
    CombatBonus bonus;
    if (towers) {
        bonus.hp_percent += towers->hp;
        bonus.attack_percent += towers->attack;
        if (const auto it = towers->element_attack.find(element); it != towers->element_attack.end())
            bonus.attack_percent += it->second;
        bonus.defense_percent += towers->defense;
        bonus.speed_percent += towers->speed;
        bonus.crit_damage += static_cast<int>(std::lround(towers->crit_damage));
    }
    if (leader && leader_applies(*leader, dungeon_category, element)) {
        const auto& attribute = leader->attribute;
        const int amount = leader->amount;
        if (attribute == "HP") bonus.hp_percent += amount;
        else if (attribute == "Attack Power") bonus.attack_percent += amount;
        else if (attribute == "Defense") bonus.defense_percent += amount;
        else if (attribute == "Attack Speed") bonus.speed_percent += amount;
        else if (attribute == "Critical Rate") bonus.crit_rate += amount;
        else if (attribute == "Critical DMG") bonus.crit_damage += amount;
        else if (attribute == "Resistance") bonus.resistance += amount;
        else if (attribute == "Accuracy") bonus.accuracy += amount;
    }
    return bonus;
}

// Stats de combat. Les bonus de tours et de leader sont des % de la stat de
// base, ajoutés aux stats des runes. Arrondi : une seule fois sur la somme des
// % (tour + leader), vers le haut par défaut ("ceil"), réglable par
// mechanics.json (combat_speed_rounding). Référence à vérifier (SWLens) :
// Prilea affichée 105 + 121 = 226, où 121 = runes + sets + tour (pas de
// leader de VIT dans la team Teshar).
StatValues combat_stats(const StatValues& white, const StatValues& runes,
                        const CombatBonus& bonus, const std::string& rounding) {
    StatValues combat = runes;
    combat.hp += rounded(white.hp * bonus.hp_percent / 100.0, rounding);
    combat.attack += rounded(white.attack * bonus.attack_percent / 100.0, rounding);
    combat.defense += rounded(white.defense * bonus.defense_percent / 100.0, rounding);
    combat.speed += rounded(white.speed * bonus.speed_percent / 100.0, rounding);
    combat.crit_rate += bonus.crit_rate;
    combat.crit_damage += bonus.crit_damage;
    combat.resistance += bonus.resistance;
    combat.accuracy += bonus.accuracy;
    return combat;
}

StatBlock stats_with_runes(const AccountMonster& unit, const std::vector<Rune>& runes,
                           const SetEffects& effects) {
    AccountMonster copy = unit;
    copy.runes = runes;
    for (auto& rune : copy.runes) rune.occupancy = RuneOccupancy::Equipped;
    return calculate_monster_stats(copy, effects);
}

TeamAnalysis analyze_team(const TeamAnalysisInput& input) {
    TeamAnalysis analysis;
    if (!input.team || !input.data || !input.level || !input.reference || !input.mechanics)
        return analysis;
    const auto& team = *input.team;
    const auto& mechanics = *input.mechanics;
    const std::string category = input.dungeon ? input.dungeon->category : std::string{};
    std::set<std::string> unverified;  // hypothèses null réellement utilisées

    const auto number = [&](const std::string& key, double fallback) {
        if (const auto value = mechanics.number(key)) return *value;
        if (mechanics.has(key)) unverified.insert(key);
        return fallback;
    };

    // Boss : dernière vague, nom de la config (sinon premier ennemi).
    if (!input.level->has_waves()) {
        analysis.notes.push_back("Étage sans données de vagues : analyse impossible.");
        return analysis;
    }
    const auto& boss_wave = input.level->waves.back().enemies;
    for (const auto& enemy : boss_wave)
        if (!analysis.boss && (team.boss.empty() || enemy.name == team.boss))
            analysis.boss = &enemy;
    if (!analysis.boss) {
        analysis.notes.push_back("Boss \"" + team.boss + "\" absent de la dernière vague.");
        return analysis;
    }
    const DungeonEnemy& boss = *analysis.boss;

    const TeamMonster* leader_config = team.leader.empty() ? nullptr : team.monster(team.leader);
    const ReferenceMonster* leader_reference =
        leader_config ? input.reference->find_swarfarm(leader_config->swarfarm_id) : nullptr;
    const std::optional<LeaderSkill> leader =
        leader_reference ? leader_reference->leader_skill : std::nullopt;

    const std::string rounding = mechanics.text("combat_speed_rounding").value_or("ceil");
    const double advantage_cr = number("element_advantage_crit_rate", 0);
    const double advantage_damage = number("element_advantage_damage_bonus", 0);
    const double def_a = number("def_formula.a", 1000);
    const double def_b = number("def_formula.b", 1140);
    const double def_c = number("def_formula.c", 3.5);
    const double def_break = number("def_break_percent", 0);
    const double atk_buff = number("atk_buff_percent", 0);
    const bool atk_buff_on_total = mechanics.text("atk_buff_on").value_or("base") == "total";

    // --- Membres : stats de combat sur les runes analysées ---
    analysis.all_runes = true;
    for (const auto& monster : team.monsters) {
        TeamMemberAnalysis member;
        member.config = &monster;
        member.reference = input.reference->find_swarfarm(monster.swarfarm_id);
        if (member.reference) {
            member.element = member.reference->element;
            if (input.box) {
                const auto pinned = input.pinned_units.find(monster.name);
                if (pinned != input.pinned_units.end()) {
                    for (const auto& unit : *input.box)
                        if (unit.unit_id == pinned->second) member.unit = &unit;
                } else {
                    member.unit = choose_box_unit(*input.box, member.reference->com2us_id);
                }
            }
        }
        const std::vector<int>& skill_ids =
            !monster.skill_overrides.empty() || !member.reference ? monster.skill_overrides
                                                                  : member.reference->skills;
        for (const int id : skill_ids)
            if (const auto* skill = input.data->skill(id)) member.skills[skill->slot] = skill;

        // Base : celle de la box, sinon 6* niv. 40 de SWARFARM.
        if (member.unit) {
            member.white = member.unit->stats.white;
        } else if (member.reference) {
            const auto& r = *member.reference;
            member.white = {r.max_lvl_hp.value_or(0), r.max_lvl_attack.value_or(0),
                            r.max_lvl_defense.value_or(0), r.speed, r.crit_rate, r.crit_damage,
                            r.resistance, r.accuracy};
        }
        // Runes analysées : build candidat, sinon runes actuelles (comparaison),
        // sinon aucune rune (artefacts conservés).
        member.runes = member.white;
        const auto build = input.builds.find(monster.name);
        if (member.unit && input.effects) {
            if (build != input.builds.end()) {
                member.runes = stats_with_runes(*member.unit, build->second, *input.effects).total;
                member.has_runes = true;
            } else if (input.use_current_runes) {
                member.runes = member.unit->stats.total;
                member.has_runes = true;
            } else {
                member.runes = stats_with_runes(*member.unit, {}, *input.effects).total;
            }
        }
        analysis.all_runes = analysis.all_runes && member.has_runes;
        const auto bonus = combat_bonus(member.element, input.towers, leader, category);
        member.combat = combat_stats(member.white, member.runes, bonus, rounding);

        auto& speed = member.speed;
        speed.base = member.white.speed;
        speed.runes = member.runes.speed;
        speed.tower_percent = input.towers ? input.towers->speed : 0;
        speed.leader_percent = bonus.speed_percent - speed.tower_percent;
        speed.bonus = member.combat.speed - member.runes.speed;
        speed.combat = member.combat.speed;
        speed.minimum = speed.base + rounded(speed.base * bonus.speed_percent / 100.0, rounding);
        if (const auto it = input.assumed_speeds.find(monster.name);
            it != input.assumed_speeds.end())
            speed.combat = it->second;

        auto& crit = member.crit;
        for (const auto& step : team.boss_script)
            if (step.actor == monster.name) {
                const auto skill = member.skills.find(step.skill);
                crit.needed |= skill != member.skills.end() && deals_damage(skill->second);
            }
        crit.base = member.white.crit_rate;
        crit.leader = bonus.crit_rate;
        if (has_element_advantage(member.element, boss.element))
            crit.advantage = static_cast<int>(advantage_cr);
        crit.required_runes = std::max(0, 100 - crit.base - crit.leader - crit.advantage);
        crit.current_runes = member.runes.crit_rate - crit.base;
        analysis.members.push_back(std::move(member));
    }

    // --- Unités ATB : leader, puis la team, puis la vague du boss ---
    analysis.member_unit.assign(analysis.members.size(), 0);
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < analysis.members.size(); ++i)
        if (analysis.members[i].config == leader_config) order.insert(order.begin(), i);
        else order.push_back(i);
    std::map<std::size_t, std::size_t> member_of_unit;
    for (const auto index : order) {
        member_of_unit[analysis.units.size()] = index;
        analysis.member_unit[index] = analysis.units.size();
        analysis.units.push_back({analysis.members[index].config->name, true,
                                  analysis.members[index].speed.combat, false});
    }
    std::map<std::string, int> name_count;
    for (const auto& enemy : boss_wave) ++name_count[enemy.name];
    std::vector<const DungeonEnemy*> enemy_of_unit(analysis.units.size(), nullptr);
    for (const auto& enemy : boss_wave) {
        std::string name = enemy.name;
        if (name_count[name] > 1 && !enemy.skill_ids.empty())
            if (const auto* skill = input.data->skill(enemy.skill_ids.front()))
                name += " (" + skill->name + ")";
        if (&enemy == &boss) analysis.boss_unit = analysis.units.size();
        enemy_of_unit.push_back(&enemy);
        analysis.units.push_back({name, false, enemy.speed, &enemy == &boss});
    }

    const auto member_index = [&](const std::string& name) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < analysis.members.size(); ++i)
            if (analysis.members[i].config->name == name) return i;
        return std::nullopt;
    };
    const auto skill_of = [&](std::size_t member, int slot) -> const SkillData* {
        const auto& skills = analysis.members[member].skills;
        const auto it = skills.find(slot);
        return it == skills.end() ? nullptr : it->second;
    };

    // --- Script -> actions ATB (pire cas : replis, effets aléatoires ratés) ---
    const auto build_script = [&](bool worst) {
        std::vector<AtbStep> script;
        for (const auto& step : team.boss_script) {
            const auto actor = member_index(step.actor);
            if (!actor) continue;
            AtbStep atb;
            atb.actor = analysis.member_unit[*actor];
            atb.skill = worst && step.fallback_skill ? *step.fallback_skill : step.skill;
            bool config_effect = false;
            for (const auto& effect : step.effects) {
                const auto amount = atb_effect(effect);
                if (!amount) continue;
                config_effect = true;
                AtbBoost boost;
                boost.amount = *amount;
                if (step.target == "previous_actor") {
                    boost.target = AtbBoost::Target::PreviousAlly;
                } else if (const auto ally = member_index(step.target); ally) {
                    boost.target = AtbBoost::Target::Ally;
                    boost.ally = analysis.member_unit[*ally];
                }
                atb.boosts.push_back(boost);
            }
            if (!config_effect)
                if (const auto* skill = skill_of(*actor, atb.skill))
                    for (const auto& effect : skill->effects) {
                        if (effect.name == "Increase ATB" && effect.self_effect &&
                            effect.quantity > 0)
                            atb.boosts.push_back({AtbBoost::Target::Self, 0,
                                                  static_cast<double>(effect.quantity)});
                        if (!worst && effect.name == "Decrease ATB" && !effect.self_effect &&
                            effect.quantity > 0)
                            atb.boosts.push_back({AtbBoost::Target::Ally, analysis.boss_unit,
                                                  -static_cast<double>(effect.quantity)});
                    }
            script.push_back(std::move(atb));
        }
        return script;
    };

    AtbOptions options;
    options.tick_percent = number("tick_atb_percent", 7.0);
    analysis.script = build_script(true);

    // --- Plan de vitesse joint : VIT minimales sans runes, puis +1 au fautif ---
    {
        auto plan_units = analysis.units;
        for (std::size_t i = 0; i < analysis.members.size(); ++i)
            plan_units[analysis.member_unit[i]].speed = analysis.members[i].speed.minimum;
        analysis.plan = plan_speeds(plan_units, analysis.script, options);
        for (std::size_t u = 0; u < plan_units.size(); ++u) plan_units[u].speed = analysis.plan.speeds[u];
        analysis.plan_atb = simulate_atb(plan_units, analysis.script, options);
        std::set<std::string> relative;
        for (std::size_t i = 0; i < analysis.members.size(); ++i) {
            auto& member = analysis.members[i];
            member.plan_min = analysis.plan.speeds[analysis.member_unit[i]];
            if (!input.compute_windows || !analysis.plan.found) continue;
            const auto window =
                speed_window(plan_units, analysis.script, analysis.member_unit[i], options);
            if (window.found && !window.upper_open) member.plan_max = window.max;
        }
        // Contraintes relatives : une borne du plan tient à la VIT d'un autre allié.
        if (input.compute_windows && analysis.plan.found)
            for (std::size_t i = 0; i < analysis.members.size(); ++i)
                for (std::size_t j = 0; j < analysis.members.size(); ++j) {
                    if (i == j) continue;
                    const auto& a = analysis.members[i];
                    const auto& b = analysis.members[j];
                    if (a.plan_max && *a.plan_max + 1 == b.plan_min)
                        relative.insert(b.config->name + " > " + a.config->name);
                    if (a.plan_min == b.plan_min + 1) {
                        // a doit-il vraiment dépasser b ? (a à la VIT de b : échec)
                        auto probe = plan_units;
                        probe[analysis.member_unit[i]].speed = b.plan_min;
                        if (!check_speed_plan(probe, analysis.script, options).ok)
                            relative.insert(a.config->name + " > " + b.config->name);
                    }
                }
        analysis.relative.assign(relative.begin(), relative.end());
    }

    // --- Vérification stricte et fenêtres avec les VIT analysées ---
    analysis.speed_check = check_speed_plan(analysis.units, analysis.script, options);
    if (input.compute_windows)
        for (std::size_t i = 0; i < analysis.members.size(); ++i)
            analysis.members[i].window =
                speed_window(analysis.units, analysis.script, analysis.member_unit[i], options);

    // --- Dégâts sur le boss ---
    const auto def_factor = [&](double defense, bool broken) {
        const double effective = broken ? defense * (1.0 - def_break / 100.0) : defense;
        return def_a / (def_b + def_c * effective);
    };
    const auto max_hp_factor = [&](Mode mode) {
        const std::string key = "max_hp_damage_vs_boss";
        if (const auto text = mechanics.text(key)) {
            if (*text == "full") return 1.0;
            if (*text == "none") return 0.0;
            try {
                return std::stod(*text);
            } catch (const std::exception&) {
                return 0.0;
            }
        }
        if (const auto value = mechanics.number(key)) return *value;
        if (mechanics.has(key)) unverified.insert(key);
        return mode == Mode::Best ? 1.0 : 0.0;
    };
    std::vector<std::string> unknown_variables;
    std::size_t counter_threshold = 0;
    if (const auto value = mechanics.number("boss_counter_after_attacks"))
        counter_threshold = static_cast<std::size_t>(*value);
    bool boss_counters = false;
    std::string counter_name;
    for (const int id : boss.skill_ids)
        if (const auto* skill = input.data->skill(id);
            skill && skill->passive && !skill->multiplier_formula.empty()) {
            boss_counters = true;
            counter_name = skill->name;
        }
    bool auto_atb_ignored = false;

    const auto flag = [&](const std::string& key, bool fallback) {
        if (const auto value = mechanics.flag(key)) return *value;
        if (mechanics.has(key)) unverified.insert(key);
        return fallback;
    };
    const bool extra_counts_for_counter = flag("fend_off_counts_passive_extra_attack", true);
    const bool boss_stun_immune = flag("boss_immune_to_stun", true);
    const double resist_floor = number("resist_floor_percent", 15);

    // --- Passives "attaque supplémentaire après ton attaque" (Tempest) ---
    struct ExtraAttack {
        const SkillData* skill = nullptr;
        int slot = 0;
        std::string name;              // sans " (Passive)"
        std::optional<int> cooldown;   // en tours du monstre
        bool reset_on_kill = false;    // "Reduce Cooltime" on_death
        const SkillEffect* stun = nullptr;
    };
    std::vector<std::optional<ExtraAttack>> extras(analysis.members.size());
    for (std::size_t i = 0; i < analysis.members.size(); ++i)
        for (const auto& [slot, skill] : analysis.members[i].skills) {
            if (!is_after_attack_passive(*skill)) continue;
            ExtraAttack extra;
            extra.skill = skill;
            extra.slot = slot;
            extra.name = skill->name;
            for (const char* suffix : {" (Passive)", "(Passive)"})
                if (const auto at = extra.name.find(suffix); at != std::string::npos)
                    extra.name.erase(at);
            extra.cooldown = skill_cooldown(*skill);
            for (const auto& effect : skill->effects) {
                if (effect.name == "Reduce Cooltime" && effect.self_effect && effect.on_death)
                    extra.reset_on_kill = true;
                if (effect.name == "Stun" && !effect.self_effect) extra.stun = &effect;
            }
            extras[i] = std::move(extra);
            break;
        }
    for (std::size_t i = 0; i < analysis.members.size(); ++i) {
        if (!extras[i]) continue;
        std::string line = extras[i]->name + " (" + analysis.members[i].config->name + ") ";
        if (!team.tempest_ready_on_boss)
            line += "en arrivant sur le boss : état inconnu (pire cas en recharge complète, "
                    "meilleur cas prête)";
        else if (*team.tempest_ready_on_boss)
            line += "prête en arrivant sur le boss (hypothèse de la config, vagues non simulées)";
        else
            line += "en recharge en arrivant sur le boss (config)";
        if (!team.tempest_ready_note.empty()) line += " : " + team.tempest_ready_note;
        analysis.hypotheses.push_back(line);
    }

    std::vector<std::size_t> enemy_units;
    for (std::size_t u = 0; u < analysis.units.size(); ++u)
        if (!analysis.units[u].ally) enemy_units.push_back(u);

    // Parcours des tours d'un scénario jusqu'au premier tour du boss : dégâts,
    // passives, morts et étourdissements (renvoyés dans outcome.events).
    const auto walk = [&](Mode mode, AtbResult atb) {
        CaseOutcome outcome;
        outcome.atb = std::move(atb);
        outcome.boss_hp = boss.hp;
        std::map<std::size_t, double> hp;  // PV des ennemis, par unité
        for (const auto u : enemy_units) hp[u] = enemy_of_unit[u]->hp;
        double& current = hp[analysis.boss_unit];
        std::set<std::size_t> dead;
        bool broken = false;
        std::set<std::size_t> buffed;
        std::optional<std::size_t> last_ally;
        std::map<std::size_t, int> own_turns;                    // tours joués par membre
        std::map<std::pair<std::size_t, int>, int> ready_at;     // (membre, slot) -> tour
        std::map<std::size_t, int> extra_ready;                  // membre -> tour où la passive est prête
        for (std::size_t i = 0; i < extras.size(); ++i)
            if (extras[i]) {
                const bool ready = team.tempest_ready_on_boss ? *team.tempest_ready_on_boss
                                                              : mode == Mode::Best;
                extra_ready[i] = ready ? 0 : extras[i]->cooldown.value_or(0);
            }
        // Attaque reçue par le boss (contre-attaque "après la N-ième attaque").
        const auto count_attack = [&](const ActionDamage& line, const std::string& what) {
            ++outcome.attacks;
            if (boss_counters && counter_threshold > 0 && !outcome.counter &&
                outcome.attacks >= counter_threshold && mode != Mode::Score)
                outcome.counter = counter_name + " : contre-attaque après l'attaque n°" +
                                  std::to_string(counter_threshold) + " (t" +
                                  std::to_string(line.tick) + ", " + what + " de " + line.actor +
                                  "), sur " + line.actor;
        };

        const std::size_t limit =
            outcome.atb.boss_turn ? *outcome.atb.boss_turn : outcome.atb.turns.size();
        for (std::size_t t = 0; t < limit; ++t) {
            const auto& turn = outcome.atb.turns[t];
            const auto& unit = analysis.units[turn.actor];
            if (!unit.ally) {
                const auto* enemy = enemy_of_unit[turn.actor];
                const SkillData* first = enemy && !enemy->skill_ids.empty()
                                             ? input.data->skill(enemy->skill_ids.front())
                                             : nullptr;
                const bool attack_tower =
                    first && first->name.find("Strengthen Attack") != std::string::npos;
                const bool defense_tower =
                    first && first->name.find("Weaken Defense") != std::string::npos;
                if (turn.stunned) {
                    std::string note =
                        unit.name + " étourdie : tour perdu au t" + std::to_string(turn.tick);
                    if (attack_tower) note += " (le boss frappera sans le buff d'ATQ)";
                    if (defense_tower) note += " (pas de réduction de DEF sur la team)";
                    outcome.stun_notes.push_back(std::move(note));
                    continue;
                }
                if (attack_tower) outcome.attack_tower_before_boss = true;
                if (defense_tower) outcome.defense_tower_before_boss = true;
                continue;
            }
            const std::size_t mi = member_of_unit[turn.actor];
            const auto& member = analysis.members[mi];
            const int own = own_turns[mi]++;
            const ScriptStep* step =
                turn.step >= 0 ? &team.boss_script[static_cast<std::size_t>(turn.step)] : nullptr;
            ActionDamage action;
            action.tick = turn.tick;
            action.actor = member.config->name;
            action.step = turn.step;
            if (step) {
                action.skill = step->skill;
                if (mode != Mode::Best && step->fallback_skill) {
                    action.skill = *step->fallback_skill;
                    action.fallback = true;
                }
            } else {
                // Hors script : l'IA auto prend la compétence active la plus haute
                // disponible (recharge comptée en tours du monstre).
                action.skill = 1;
                for (const auto& [slot, skill] : member.skills) {
                    if (skill->passive) continue;
                    const auto ready = ready_at.find({mi, slot});
                    if (ready == ready_at.end() || ready->second <= own) action.skill = slot;
                }
                action.note = "IA auto";
            }
            const SkillData* skill = skill_of(mi, action.skill);
            if (skill)
                if (const auto cooldown = skill_cooldown(*skill))
                    ready_at[{mi, action.skill}] = own + *cooldown;
            if (!step && skill)
                for (const auto& effect : skill->effects)
                    auto_atb_ignored |= effect.name.find("ATB") != std::string::npos;
            action.atk_buff = buffed.erase(mi) > 0;
            const bool keep_going = mode == Mode::Score || !outcome.kills;

            // ATQ et critique du tour (communs à la compétence et à la passive).
            const double attack =
                member.combat.attack +
                (action.atk_buff
                     ? atk_buff / 100.0 *
                           (atk_buff_on_total ? member.combat.attack : member.white.attack)
                     : 0.0);
            const int combat_cr = member.combat.crit_rate + member.crit.advantage;
            const bool crit = combat_cr >= 100 || mode != Mode::Worst;
            const auto crit_factor = [&](const DungeonEnemy& target) {
                return crit ? 1.0 + std::max(0, member.combat.crit_damage -
                                                    target.crit_damage_reduction) /
                                        100.0
                            : 1.0;
            };
            const auto element_factor = [&](const DungeonEnemy& target) {
                return has_element_advantage(member.element, target.element)
                           ? 1.0 + advantage_damage / 100.0
                           : 1.0;
            };
            const auto alive_enemies = [&] {
                return static_cast<double>(std::count_if(
                    enemy_units.begin(), enemy_units.end(),
                    [&](std::size_t u) { return !dead.contains(u); }));
            };
            // Multiplicateur (avec et sans la part ATQ) d'une compétence sur une cible.
            const auto evaluate = [&](const SkillData& used, const DungeonEnemy& target,
                                      double target_hp, bool is_boss) {
                std::map<std::string, double> variables{
                    {"ATK", attack},
                    {"DEF", static_cast<double>(member.combat.defense)},
                    {"MAX HP", static_cast<double>(member.combat.hp)},
                    {"SPD", static_cast<double>(member.speed.combat)},
                    {"Current HP %", 1.0},
                    {"Current HP", static_cast<double>(member.combat.hp)},
                    {"Missing HP", 0.0},
                    {"Attacker's Level", member.unit ? member.unit->level : 40.0},
                    {"Target MAX HP", target.hp * (is_boss ? max_hp_factor(mode) : 1.0)},
                    {"Target Current HP %",
                     target.hp > 0 ? std::max(0.0, target_hp) / target.hp : 0.0},
                    {"Target SPD", static_cast<double>(target.speed)},
                    {"Alive Enemies", alive_enemies()}};
                const double value =
                    evaluate_skill_formula(used.multiplier_formula, variables, &unknown_variables);
                variables["ATK"] = 0;
                const double fixed = evaluate_skill_formula(used.multiplier_formula, variables);
                return std::pair{value, fixed};
            };
            std::vector<std::size_t> killed;   // ennemis morts pendant ce tour
            std::vector<std::size_t> stunned;  // ennemis étourdis pendant ce tour
            const auto collect_deaths = [&] {
                for (const auto u : enemy_units)
                    if (hp[u] <= 0 && dead.insert(u).second) killed.push_back(u);
            };

            bool attacked = false;
            if (deals_damage(skill) && keep_going) {
                attacked = true;
                action.crit = crit;
                action.def_broken = broken;
                const double factor = (1.0 + skill_damage_bonus(*skill) / 100.0) *
                                      crit_factor(boss) * def_factor(boss.defense, broken) *
                                      element_factor(boss) * std::max(1, skill->hits);
                const auto [value, fixed] = evaluate(*skill, boss, current, true);
                action.damage = value * factor;
                action.atk_damage = (value - fixed) * factor;
                current -= action.damage;
                outcome.damage += action.damage;
                outcome.atk_damage += action.atk_damage;
                count_attack(action, "S" + std::to_string(action.skill));
            }
            // Morts de la compétence, avant la passive.
            collect_deaths();
            if (!outcome.kills && current <= 0 && attacked) {
                outcome.kills = true;
                action.kills = true;
            }
            action.boss_hp_after = std::max(0.0, current);

            // Passive d'attaque supplémentaire : zone, après la compétence.
            std::optional<ActionDamage> extra_line;
            if (extras[mi] && attacked) {
                const auto& extra = *extras[mi];
                int& ready = extra_ready[mi];
                if (extra.reset_on_kill && !killed.empty()) ready = own;  // tué par S1/S2
                if (own >= ready && (mode == Mode::Score || !outcome.kills)) {
                    ActionDamage line;
                    line.tick = turn.tick;
                    line.actor = action.actor;
                    line.skill = extra.slot;
                    line.skill_name = extra.name;
                    line.step = turn.step;
                    line.extra = true;
                    line.crit = crit;
                    line.def_broken = broken;
                    line.atk_buff = action.atk_buff;
                    const std::size_t kills_before = killed.size();
                    const double bonus = 1.0 + skill_damage_bonus(*extra.skill) / 100.0;
                    for (const auto u : enemy_units) {
                        const bool is_boss = u == analysis.boss_unit;
                        if (dead.contains(u) && !(is_boss && mode == Mode::Score)) continue;
                        const auto& target = *enemy_of_unit[u];
                        const auto [value, fixed] = evaluate(*extra.skill, target, hp[u], is_boss);
                        const double factor = bonus * crit_factor(target) *
                                              def_factor(target.defense, is_boss && broken) *
                                              element_factor(target) *
                                              std::max(1, extra.skill->hits);
                        TargetDamage hit;
                        hit.name = analysis.units[u].name;
                        hit.boss = is_boss;
                        hit.damage = value * factor;
                        const bool was_alive = hp[u] > 0;
                        hp[u] -= hit.damage;
                        hit.hp_after = std::max(0.0, hp[u]);
                        hit.kills = was_alive && hp[u] <= 0;
                        if (is_boss) {
                            line.damage = hit.damage;
                            line.atk_damage = (value - fixed) * factor;
                        }
                        if (extra.stun) {
                            if (is_boss && boss_stun_immune) {
                                hit.stun_text = "insensible au stun";
                            } else {
                                const double resisted = std::max(
                                    resist_floor,
                                    static_cast<double>(target.resist - member.combat.accuracy));
                                const double base =
                                    extra.stun->chance > 0 ? extra.stun->chance : 100.0;
                                hit.stun_chance =
                                    std::clamp(base * (100.0 - resisted) / 100.0, 0.0, 100.0);
                                if (hp[u] <= 0) {
                                    hit.stun_text = "morte";
                                } else if (mode == Mode::Best) {
                                    hit.stunned = true;
                                    hit.stun_text = "étourdie";
                                    stunned.push_back(u);
                                } else {
                                    hit.stun_text = "stun résisté (pire cas)";
                                }
                            }
                        }
                        line.targets.push_back(std::move(hit));
                    }
                    outcome.damage += line.damage;
                    outcome.atk_damage += line.atk_damage;
                    if (extra_counts_for_counter) count_attack(line, extra.name);
                    collect_deaths();
                    if (!outcome.kills && current <= 0) {
                        outcome.kills = true;
                        line.kills = true;
                    }
                    line.boss_hp_after = std::max(0.0, current);
                    ready = own + extra.cooldown.value_or(1);
                    if (extra.reset_on_kill && killed.size() > kills_before) {
                        ready = own + 1;
                        line.note = "ennemi tué : recharge de " + extra.name + " réinitialisée";
                    }
                    action.passive = "+ " + extra.name;
                    extra_line = std::move(line);
                } else if (own < ready) {
                    action.passive = extra.name + " en recharge";
                    if (extra.cooldown) action.passive += " : " + std::to_string(*extra.cooldown);
                    action.passive += ", prête dans " + std::to_string(ready - own) + " tour" +
                                      (ready - own > 1 ? "s" : "");
                }
            }
            if (!killed.empty() || !stunned.empty())
                outcome.events.push_back({turn.actor, own, stunned, killed});

            // Effets après le coup : config, sinon SWARFARM (aléatoires : ratés au
            // pire cas, réussis au meilleur).
            if (step && !step->effects.empty()) {
                for (const auto& effect : step->effects) {
                    if (effect == "def_break_guaranteed") broken = true;
                    if (effect.rfind("atk_buff", 0) == 0) {
                        std::optional<std::size_t> target = mi;
                        if (step->target == "previous_actor") target = last_ally;
                        else if (const auto ally = member_index(step->target); ally) target = ally;
                        if (target) buffed.insert(*target);
                    }
                }
            } else if (skill && mode == Mode::Best) {
                for (const auto& effect : skill->effects)
                    if (effect.name == "Decrease DEF" && !effect.self_effect && effect.chance > 0)
                        broken = true;
            }
            last_ally = mi;
            outcome.actions.push_back(std::move(action));
            if (extra_line) outcome.actions.push_back(std::move(*extra_line));
            if (outcome.kills && mode != Mode::Score) break;
        }
        outcome.remaining = std::max(0.0, current);
        outcome.remaining_percent = boss.hp > 0 ? outcome.remaining * 100.0 / boss.hp : 0;
        if (outcome.kills)
            outcome.required_factor = 1;
        else if (outcome.atk_damage > 0)
            outcome.required_factor =
                (boss.hp - (outcome.damage - outcome.atk_damage)) / outcome.atk_damage;
        else
            outcome.required_factor = 0;
        return outcome;
    };

    // Les étourdissements et les morts changent l'ordre des tours : on rejoue
    // l'ATB avec les événements trouvés jusqu'à ce qu'ils ne changent plus.
    const auto run_case = [&](Mode mode) {
        std::vector<AtbEvent> events;
        CaseOutcome outcome;
        for (int iteration = 0; iteration < 8; ++iteration) {
            AtbOptions run = options;
            run.events = events;
            outcome = walk(mode, simulate_atb(analysis.units, build_script(mode != Mode::Best), run));
            if (outcome.events == events) break;
            events = outcome.events;
        }
        return outcome;
    };
    analysis.worst = run_case(Mode::Worst);
    analysis.best = run_case(Mode::Best);
    const CaseOutcome scoring = run_case(Mode::Score);
    analysis.atb = analysis.worst.atb;

    // --- Score de team et poids c_i ---
    analysis.score = scoring.damage;
    for (auto& member : analysis.members) {
        double atk_damage = 0;
        for (const auto& action : scoring.actions)
            if (action.actor == member.config->name) atk_damage += action.atk_damage;
        const double scale = member.combat.attack * (1.0 + member.combat.crit_damage / 100.0);
        member.coefficient = scale > 0 ? atk_damage / scale : 0;
    }

    // --- Survie au premier coup du boss (tours de cristal réellement jouées) ---
    const SkillData* boss_skill = nullptr;
    double strongest = -1;
    for (const int id : boss.skill_ids) {
        const auto* skill = input.data->skill(id);
        if (!deals_damage(skill)) continue;
        const double value = evaluate_skill_formula(
            skill->multiplier_formula, {{"ATK", static_cast<double>(boss.attack)},
                                        {"DEF", static_cast<double>(boss.defense)},
                                        {"MAX HP", static_cast<double>(boss.hp)}});
        if (value * std::max(1, skill->hits) > strongest) {
            strongest = value * std::max(1, skill->hits);
            boss_skill = skill;
        }
    }
    if (boss_skill) {
        analysis.boss_skill = boss_skill->name;
        const ReferenceMonster* boss_reference =
            boss.com2us_id ? input.reference->find(boss.com2us_id) : nullptr;
        const int boss_cd = boss_reference ? boss_reference->crit_damage : 50;
        const double boss_attack =
            boss.attack * (analysis.worst.attack_tower_before_boss ? 1.0 + atk_buff / 100.0 : 1.0);
        for (const auto& member : analysis.members) {
            SurvivalLine line;
            line.member = member.config->name;
            line.hp = member.combat.hp;
            const double value = evaluate_skill_formula(
                boss_skill->multiplier_formula,
                {{"ATK", boss_attack},
                 {"DEF", static_cast<double>(boss.defense)},
                 {"MAX HP", static_cast<double>(boss.hp)},
                 {"SPD", static_cast<double>(boss.speed)},
                 {"Target MAX HP", static_cast<double>(member.combat.hp)},
                 {"Target Current HP %", 1.0}});
            line.hit = value * std::max(1, boss_skill->hits) *
                       def_factor(member.combat.defense, analysis.worst.defense_tower_before_boss);
            line.crit_hit = line.hit * (1.0 + boss_cd / 100.0);
            line.survives = line.hit < line.hp;
            line.survives_crit = line.crit_hit < line.hp;
            analysis.survival.push_back(line);
        }
        analysis.survival_matters =
            std::any_of(analysis.survival.begin(), analysis.survival.end(),
                        [](const SurvivalLine& line) { return line.survives; });
    }

    // --- Contraintes et ce qui manque ---
    const bool crit_ok = std::all_of(analysis.members.begin(), analysis.members.end(),
                                     [](const auto& member) { return member.crit.satisfied(); });
    analysis.constraints_ok = analysis.speed_check.ok && crit_ok && analysis.all_runes;
    auto& missing = analysis.missing;
    bool speed_reported = false;
    for (const auto& member : analysis.members) {
        const auto& name = member.config->name;
        if (!member.unit) {
            missing.push_back(name + " : absent de la box.");
            continue;
        }
        if (!member.has_runes) {
            missing.push_back(name + " : pas de build analysé.");
            continue;
        }
        // VIT : fenêtre avec les VIT des autres, sinon le plan joint.
        const int speed = member.speed.combat;
        const int lower = member.window.found ? member.window.min : member.plan_min;
        const std::optional<int> upper =
            member.window.found ? (member.window.upper_open ? std::nullopt
                                                            : std::optional<int>{member.window.max})
                                : member.plan_max;
        if (speed < lower) {
            missing.push_back(name + " : +" + std::to_string(lower - speed) + " VIT (combat " +
                              std::to_string(speed) + ", requise " + std::to_string(lower) + ").");
            speed_reported = true;
        } else if (upper && speed > *upper) {
            missing.push_back(name + " : -" + std::to_string(speed - *upper) + " VIT (combat " +
                              std::to_string(speed) + ", maximum " + std::to_string(*upper) + ").");
            speed_reported = true;
        }
        if (!member.crit.satisfied())
            missing.push_back(name + " : +" +
                              std::to_string(member.crit.required_runes - member.crit.current_runes) +
                              " % CR sur les runes (" + std::to_string(member.crit.current_runes) +
                              " / " + std::to_string(member.crit.required_runes) + ").");
    }
    if (!analysis.speed_check.ok && !speed_reported && analysis.all_runes)
        missing.push_back("Ordre des tours : " + analysis.speed_check.reason + ".");
    if (!analysis.worst.kills && analysis.all_runes) {
        std::string line = "Boss vivant au pire cas : " + number_text(analysis.worst.remaining) +
                           " PV restants (" + number_text(analysis.worst.remaining_percent, 1) +
                           " %).";
        if (analysis.worst.required_factor > 0)
            line += " Il faut ×" + number_text(analysis.worst.required_factor, 2) +
                    " sur ATQ × (1 + DC) des nukers.";
        missing.push_back(line);
    }
    if (analysis.worst.counter && analysis.all_runes) missing.push_back(*analysis.worst.counter);
    if (!analysis.worst.kills && analysis.survival_matters && analysis.all_runes)
        for (const auto& line : analysis.survival)
            if (!line.survives)
                missing.push_back(line.member + " ne survit pas au premier coup du boss (" +
                                  number_text(line.hit) + " dégâts pour " +
                                  std::to_string(line.hp) + " PV).");

    analysis.unverified.assign(unverified.begin(), unverified.end());
    for (const auto& name : unknown_variables)
        analysis.notes.push_back("variable de formule inconnue (comptée 0) : " + name);
    if (auto_atb_ignored)
        analysis.notes.push_back("tours hors script : effets de barre de la compétence de l'IA "
                                 "auto non simulés");
    if (!analysis.plan.found)
        analysis.notes.push_back("plan de vitesse introuvable : " + analysis.plan.reason);
    analysis.valid = true;
    return analysis;
}

}  // namespace swcore
