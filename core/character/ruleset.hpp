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

    int max_level() const { return static_cast<int>(proficiency_by_level.size()); }
    // Throws std::invalid_argument for a level outside [1, max_level].
    int proficiency_bonus(int level) const;
    const SkillDef* find_skill(const std::string& id) const;
};

}  // namespace rpg::character
