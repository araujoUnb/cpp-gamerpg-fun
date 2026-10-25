#pragma once

#include "character/attributes.hpp"

#include <string>
#include <vector>

namespace rpg::character {

struct SkillDef {
    std::string id;
    Attribute attribute = Attribute::Strength;
    std::string name_key;
};

struct IntRange {
    int min = 0;
    int max = 0;
    bool contains(int v) const { return v >= min && v <= max; }
};

// Status effects that cards and monsters can apply. Their effect on combat is
// declared in data and interpreted by the combat module.
struct ConditionEffects {
    int damage_taken_bonus = 0;  // added to each attack's damage against the creature
    int damage_reduction = 0;    // subtracted from each attack's damage against the creature
    bool skip_turn = false;
    bool no_move = false;
    bool attack_advantage = false;
    bool attack_disadvantage = false;
};

struct ConditionDef {
    std::string id;
    std::string name_key;
    std::string description_key;
    ConditionEffects effects;
};

// 5e-style death saving throws for heroes at 0 HP, rolled at the end of each round.
struct DeathSaveRules {
    int dc = 10;
    int failures_to_die = 3;
    int successes_to_stabilize = 3;
};

struct CardRules {
    IntRange initiative{1, 99};
    int basic_attack = 2;          // replaces a top half
    int basic_move = 2;            // replaces a bottom half
    int rest_min_discard = 2;      // cards needed in the discard pile to rest
    int long_rest_heal = 2;
    int long_rest_initiative = 99;
};

// The numeric rules of a game, loaded from data/rules/ruleset.json.
// Nothing here is hard-coded in the rules functions, so the same core can run
// a different game by shipping a different ruleset.
// The defaults match docs/SPEC.md and keep tests independent of data files.
struct Ruleset {
    IntRange attribute_score{8, 20};
    IntRange hand_size{8, 12};
    // Index 0 is level 1. Its size defines the maximum level.
    std::vector<int> proficiency_by_level{2, 2, 2, 2, 3, 3, 3, 3, 4};
    std::vector<SkillDef> skills;
    std::vector<ConditionDef> conditions;
    CardRules cards;
    DeathSaveRules death_saves;

    int max_level() const { return static_cast<int>(proficiency_by_level.size()); }
    // Throws std::invalid_argument for a level outside [1, max_level].
    int proficiency_bonus(int level) const;
    const SkillDef* find_skill(const std::string& id) const;
    const ConditionDef* find_condition(const std::string& id) const;
};

}  // namespace rpg::character
