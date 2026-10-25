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
struct ConditionDef {
    std::string id;
    std::string name_key;
    std::string description_key;
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

    int max_level() const { return static_cast<int>(proficiency_by_level.size()); }
    // Throws std::invalid_argument for a level outside [1, max_level].
    int proficiency_bonus(int level) const;
    const SkillDef* find_skill(const std::string& id) const;
    const ConditionDef* find_condition(const std::string& id) const;
};

}  // namespace rpg::character
