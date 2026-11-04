#include "swcore/team_analysis.hpp"

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

    double parse() {
        const double value = expression();
        skip();
        return value;
    }

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
            if (accept('*')) value *= factor();
            else if (accept('/')) {
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
            const std::string name = text_.substr(pos_ + 1, end == std::string::npos
                                                                ? std::string::npos
                                                                : end - pos_ - 1);
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

TeamAnalysis analyze_team(const TeamAnalysisInput& input) {
    TeamAnalysis analysis;
    if (!input.team || !input.data || !input.level || !input.reference || !input.mechanics)
        return analysis;
    const auto& team = *input.team;
    const auto& mechanics = *input.mechanics;
    const std::string category = input.dungeon ? input.dungeon->category : std::string{};
    std::set<std::string> unverified;  // hypothèses null réellement utilisées

    // Valeur numérique de mechanics.json ; null / absente -> défaut (noté si null).
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

    // Leader de la team.
    const TeamMonster* leader_config = team.leader.empty() ? nullptr : team.monster(team.leader);
    const ReferenceMonster* leader_reference =
        leader_config ? input.reference->find_swarfarm(leader_config->swarfarm_id) : nullptr;
    const std::optional<LeaderSkill> leader =
        leader_reference ? leader_reference->leader_skill : std::nullopt;

    const double tower = number("speed_tower_percent", 0);
    const std::string rounding = mechanics.text("combat_speed_rounding").value_or("ceil");
    const double advantage_cr = number("element_advantage_crit_rate", 0);
    const double advantage_damage = number("element_advantage_damage_bonus", 0);
    const double def_a = number("def_formula.a", 1000);
    const double def_b = number("def_formula.b", 1140);
    const double def_c = number("def_formula.c", 3.5);
    const double def_break = number("def_break_percent", 0);
    const double atk_buff = number("atk_buff_percent", 0);
    const bool atk_buff_on_total = mechanics.text("atk_buff_on").value_or("base") == "total";

    // --- Membres (ordre de la config) ---
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

        // Stats : celles de la box, sinon la base 6* niv. 40 de SWARFARM.
        if (member.unit) {
            member.white = member.unit->stats.white;
            member.total = member.unit->stats.total;
        } else if (member.reference) {
            const auto& r = *member.reference;
            member.white = {r.max_lvl_hp.value_or(0), r.max_lvl_attack.value_or(0),
                            r.max_lvl_defense.value_or(0), r.speed, r.crit_rate, r.crit_damage,
                            r.resistance, r.accuracy};
            member.total = member.white;
        }

        // VIT de combat.
        auto& speed = member.speed;
        speed.base = member.white.speed;
        speed.total = member.total.speed;
        if (leader && leader->attribute == "Attack Speed" &&
            leader_applies(*leader, category, member.element))
            speed.leader_percent = leader->amount;
        speed.tower_percent = tower;
        speed.bonus = rounded(speed.base * (speed.leader_percent + tower) / 100.0, rounding);
        speed.combat = speed.total + speed.bonus;
        if (const auto it = input.manual_speeds.find(monster.name); it != input.manual_speeds.end()) {
            speed.combat = it->second;
            speed.manual = true;
        }

        // Taux critique cible (seulement si le monstre inflige des dégâts).
        auto& crit = member.crit;
        for (const auto& step : team.boss_script)
            if (step.actor == monster.name) {
                const auto skill = member.skills.find(step.skill);
                crit.needed |= skill != member.skills.end() && deals_damage(skill->second);
            }
        crit.base = member.white.crit_rate;
        if (leader && leader->attribute == "Critical Rate" &&
            leader_applies(*leader, category, member.element))
            crit.leader = leader->amount;
        if (has_element_advantage(member.element, boss.element))
            crit.advantage = static_cast<int>(advantage_cr);
        crit.required_runes = std::max(0, 100 - crit.base - crit.leader - crit.advantage);
        crit.current_runes = member.total.crit_rate - crit.base;
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
        // Ennemis homonymes (tours) : distingués par leur compétence.
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
            if (!actor) continue;  // signalé par la validation de la config
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
                        // Réduction de barre du boss : aléatoire (résistée au pire cas).
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

    // --- Fenêtres de VIT (pire cas) et contraintes relatives ---
    if (input.compute_windows) {
        for (std::size_t i = 0; i < analysis.members.size(); ++i)
            analysis.members[i].window =
                speed_window(analysis.units, analysis.script, analysis.member_unit[i], options);
        std::set<std::string> relative;
        for (const auto& member : analysis.members) {
            if (!member.window.found) continue;
            for (const auto& other : analysis.members) {
                if (&other == &member) continue;
                const int speed = other.speed.combat;
                const std::string& a = member.config->name;
                const std::string& b = other.config->name;
                // Borne de la fenêtre = VIT d'un autre allié (égalité tolérée
                // grâce à l'ordre de départage) ou cette VIT ± 1 (marge stricte).
                if (!member.window.upper_open && member.window.max == speed)
                    relative.insert(b + " ≥ " + a + " (égalité tolérée : ordre de départage)");
                else if (!member.window.upper_open && member.window.max + 1 == speed)
                    relative.insert(b + " > " + a + " (marge stricte d'au moins 1)");
                if (!member.window.lower_open && member.window.min == speed)
                    relative.insert(a + " ≥ " + b + " (égalité tolérée : ordre de départage)");
                else if (!member.window.lower_open && member.window.min == speed + 1)
                    relative.insert(a + " > " + b + " (marge stricte d'au moins 1)");
            }
        }
        analysis.relative.assign(relative.begin(), relative.end());
    }

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
        return mode == Mode::Best ? 1.0 : 0.0;  // inconnu : 0 au pire, plein au mieux
    };
    std::vector<std::string> unknown_variables;
    std::size_t boss_attacks_threshold = 0;
    if (const auto value = mechanics.number("boss_counter_after_attacks"))
        boss_attacks_threshold = static_cast<std::size_t>(*value);
    bool boss_counters = false;  // passive avec dégâts (contre-attaque)
    std::string counter_name;
    for (const int id : boss.skill_ids)
        if (const auto* skill = input.data->skill(id);
            skill && skill->passive && !skill->multiplier_formula.empty()) {
            boss_counters = true;
            counter_name = skill->name;
        }

    const auto run_case = [&](Mode mode) {
        CaseOutcome outcome;
        outcome.atb = simulate_atb(analysis.units, build_script(mode != Mode::Best), options);
        outcome.boss_hp = boss.hp;
        double current = boss.hp;
        bool broken = false;
        std::set<std::size_t> buffed;        // ATQ boostée pour sa prochaine action
        std::set<std::size_t> passive_used;  // passive "tempest" déjà déclenchée
        std::optional<std::size_t> last_ally;
        const std::size_t limit =
            outcome.atb.boss_turn ? *outcome.atb.boss_turn : outcome.atb.turns.size();
        for (std::size_t t = 0; t < limit; ++t) {
            const auto& turn = outcome.atb.turns[t];
            const auto& unit = analysis.units[turn.actor];
            if (!unit.ally) {
                // Tours du boss : la tour d'ATQ booste le boss, celle de DEF affaiblit.
                const auto* enemy = enemy_of_unit[turn.actor];
                if (enemy && !enemy->skill_ids.empty())
                    if (const auto* skill = input.data->skill(enemy->skill_ids.front())) {
                        if (skill->name.find("Strengthen Attack") != std::string::npos)
                            outcome.attack_tower_before_boss = true;
                        if (skill->name.find("Weaken Defense") != std::string::npos)
                            outcome.defense_tower_before_boss = true;
                    }
                continue;
            }
            const std::size_t mi = member_of_unit[turn.actor];
            const auto& member = analysis.members[mi];
            const ScriptStep* step =
                turn.step >= 0 ? &team.boss_script[static_cast<std::size_t>(turn.step)] : nullptr;
            ActionDamage action;
            action.tick = turn.tick;
            action.actor = member.config->name;
            action.extra_turn = step == nullptr;
            action.skill = step ? step->skill : 1;  // tour en plus : S1
            if (step && mode != Mode::Best && step->fallback_skill) {
                action.skill = *step->fallback_skill;
                action.fallback = true;
            }
            const SkillData* skill = skill_of(mi, action.skill);
            action.atk_buff = buffed.erase(mi) > 0;  // consommée par cette action
            const bool keep_going = mode == Mode::Score || !outcome.kills;
            if (deals_damage(skill) && keep_going) {
                const double attack =
                    member.total.attack +
                    (action.atk_buff
                         ? atk_buff / 100.0 *
                               (atk_buff_on_total ? member.total.attack : member.white.attack)
                         : 0.0);
                const int combat_cr =
                    member.total.crit_rate + member.crit.leader + member.crit.advantage;
                action.crit = combat_cr >= 100 || mode != Mode::Worst;
                action.def_broken = broken;
                const double crit_factor =
                    action.crit
                        ? 1.0 + std::max(0, member.total.crit_damage - boss.crit_damage_reduction) /
                                    100.0
                        : 1.0;
                const double element_factor =
                    has_element_advantage(member.element, boss.element)
                        ? 1.0 + advantage_damage / 100.0
                        : 1.0;
                const double factor = (1.0 + skill_damage_bonus(*skill) / 100.0) * crit_factor *
                                      def_factor(boss.defense, broken) * element_factor *
                                      std::max(1, skill->hits);
                std::map<std::string, double> variables{
                    {"ATK", attack},
                    {"DEF", static_cast<double>(member.total.defense)},
                    {"MAX HP", static_cast<double>(member.total.hp)},
                    {"SPD", static_cast<double>(member.speed.combat)},
                    {"Current HP %", 1.0},
                    {"Current HP", static_cast<double>(member.total.hp)},
                    {"Missing HP", 0.0},
                    {"Attacker's Level", member.unit ? member.unit->level : 40.0},
                    {"Target MAX HP", boss.hp * max_hp_factor(mode)},
                    {"Target Current HP %", std::max(0.0, current) / boss.hp},
                    {"Target SPD", static_cast<double>(boss.speed)},
                    {"Alive Enemies", static_cast<double>(boss_wave.size())}};
                const double value =
                    evaluate_skill_formula(skill->multiplier_formula, variables, &unknown_variables);
                variables["ATK"] = 0;
                const double fixed = evaluate_skill_formula(skill->multiplier_formula, variables);
                action.damage = value * factor;
                action.atk_damage = (value - fixed) * factor;
                ++outcome.attacks;

                // Passive en plus de l'attaque (ex. Tempest de Teshar), mechanics.json.
                const std::string passive = "per_monster." + member.config->name + ".";
                if (mechanics.has(passive + "tempest_multiplier")) {
                    const bool every = mechanics.flag(passive + "tempest_every_attack").value_or(false);
                    if (every || !passive_used.count(mi)) {
                        if (const auto multiplier = mechanics.number(passive + "tempest_multiplier")) {
                            const double extra = *multiplier * attack * crit_factor *
                                                 def_factor(boss.defense, broken) * element_factor;
                            action.damage += extra;
                            action.atk_damage += extra;
                            action.note = "passive comprise";
                            ++outcome.attacks;
                        } else {
                            unverified.insert(passive + "tempest_multiplier");
                            action.note = "passive non chiffrée (0)";
                        }
                        passive_used.insert(mi);
                    }
                }
                current -= action.damage;
                outcome.damage += action.damage;
                outcome.atk_damage += action.atk_damage;
                if (!outcome.kills && current <= 0) {
                    outcome.kills = true;
                    action.kills = true;
                }
                if (boss_counters && boss_attacks_threshold > 0 && !outcome.counter &&
                    outcome.attacks >= boss_attacks_threshold && mode != Mode::Score)
                    outcome.counter = counter_name + " : contre-attaque après l'attaque n°" +
                                      std::to_string(boss_attacks_threshold) + " (t" +
                                      std::to_string(turn.tick) + "), sur " + action.actor;
            }

            // Effets après le coup : ceux de la config, sinon ceux de SWARFARM
            // (aléatoires : ratés au pire cas, réussis au meilleur).
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
            outcome.required_factor = 0;  // aucun dégât : impossible
        return outcome;
    };
    analysis.worst = run_case(Mode::Worst);
    analysis.best = run_case(Mode::Best);
    const CaseOutcome scoring = run_case(Mode::Score);
    analysis.atb = analysis.worst.atb;

    // --- Score de team et poids c_i ---
    analysis.score = scoring.damage;
    for (std::size_t i = 0; i < analysis.members.size(); ++i) {
        auto& member = analysis.members[i];
        double atk_damage = 0;
        for (const auto& action : scoring.actions)
            if (action.actor == member.config->name) atk_damage += action.atk_damage;
        const double scale = member.total.attack * (1.0 + member.total.crit_damage / 100.0);
        member.coefficient = scale > 0 ? atk_damage / scale : 0;
    }

    // --- Survie au premier coup du boss (pire cas : tours jouées avant) ---
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
            line.hp = member.total.hp;
            const double value = evaluate_skill_formula(
                boss_skill->multiplier_formula,
                {{"ATK", boss_attack},
                 {"DEF", static_cast<double>(boss.defense)},
                 {"MAX HP", static_cast<double>(boss.hp)},
                 {"SPD", static_cast<double>(boss.speed)},
                 {"Target MAX HP", static_cast<double>(member.total.hp)},
                 {"Target Current HP %", 1.0}});
            line.hit = value * std::max(1, boss_skill->hits) *
                       def_factor(member.total.defense, analysis.worst.defense_tower_before_boss);
            line.crit_hit = line.hit * (1.0 + boss_cd / 100.0);
            line.survives = line.hit < line.hp;
            line.survives_crit = line.crit_hit < line.hp;
            analysis.survival.push_back(line);
        }
        analysis.survival_matters =
            std::any_of(analysis.survival.begin(), analysis.survival.end(),
                        [](const SurvivalLine& line) { return line.survives; });
    }

    // --- Contraintes et ce qui manque (pire cas) ---
    const bool crit_ok = std::all_of(analysis.members.begin(), analysis.members.end(),
                                     [](const auto& member) { return member.crit.satisfied(); });
    analysis.constraints_ok = analysis.worst.atb.script_before_boss && crit_ok;
    auto& missing = analysis.missing;
    if (!analysis.worst.atb.script_before_boss)
        missing.push_back("Script incomplet avant le premier tour du boss (" +
                          std::to_string(analysis.worst.atb.steps_done_before_boss) + "/" +
                          std::to_string(analysis.script.size()) + " actions).");
    if (!analysis.worst.kills) {
        std::string line = "Boss vivant au pire cas : " +
                           number_text(analysis.worst.remaining) + " PV restants (" +
                           number_text(analysis.worst.remaining_percent, 1) + " %).";
        if (analysis.worst.required_factor > 0)
            line += " Il faut ×" + number_text(analysis.worst.required_factor, 2) +
                    " sur ATQ × (1 + DC) des attaquants.";
        missing.push_back(line);
    }
    for (const auto& member : analysis.members) {
        const auto& window = member.window;
        const int speed = member.speed.combat;
        if (input.compute_windows && window.found && !window.current_inside) {
            const int target = speed < window.min ? window.min : window.max;
            missing.push_back(member.config->name + " : VIT " + std::to_string(speed) +
                              " hors fenêtre, " + (target > speed ? "+" : "") +
                              std::to_string(target - speed) + " VIT.");
        } else if (input.compute_windows && !window.found) {
            missing.push_back(member.config->name + " : aucune VIT ne fait passer le script.");
        }
        if (!member.crit.satisfied())
            missing.push_back(member.config->name + " : CR des runes " +
                              std::to_string(member.crit.current_runes) + " % / " +
                              std::to_string(member.crit.required_runes) + " % requis (manque " +
                              std::to_string(member.crit.required_runes - member.crit.current_runes) +
                              ").");
    }
    if (analysis.worst.counter) missing.push_back(*analysis.worst.counter);
    if (!analysis.worst.kills && analysis.survival_matters)
        for (const auto& line : analysis.survival)
            if (!line.survives)
                missing.push_back(line.member + " ne survit pas au premier coup du boss (" +
                                  number_text(line.hit) + " dégâts pour " +
                                  std::to_string(line.hp) + " PV).");

    // Hypothèses non vérifiées utilisées, et variables de formule inconnues.
    analysis.unverified.assign(unverified.begin(), unverified.end());
    for (const auto& name : unknown_variables)
        analysis.notes.push_back("variable de formule inconnue (comptée 0) : " + name);
    if (std::any_of(analysis.members.begin(), analysis.members.end(), [](const auto& member) {
            for (const auto& [slot, skill] : member.skills)
                if (skill->hits > 1) return true;
            return false;
        }))
        analysis.notes.push_back("compétences multi-coups : multiplicateur appliqué à chaque coup "
                                 "(hypothèse)");
    analysis.valid = true;
    return analysis;
}

}  // namespace swcore
