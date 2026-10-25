#pragma once

#include "character/attributes.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rpg::character {

// AC = base + DEX modifier (capped) + modifier of an extra attribute, if any.
// Covers worn armor (chain mail: base 16, cap 0) and unarmored defense
// (base 10 + DEX + CON).
struct ArmorDef {
    int base = 10;
    std::optional<int> max_dex_bonus;  // nullopt: no cap
    std::optional<Attribute> extra_attribute;
};

struct SkillChoice {
    int count = 0;
    bool any = false;                 // true: any skill of the ruleset
    std::vector<std::string> options; // used when any is false
};

// Presentation data for the frontend. The rules never read it.
struct VisualDef {
    std::string color;     // "#rrggbb"
    std::string portrait;  // path relative to the assets folder
};

// A hero class, loaded from data/classes/*.json.
struct ClassDef {
    std::string id;
    std::string name_key;         // text key, resolved through the TextTable
    std::string description_key;  // text key
    Attribute primary = Attribute::Strength;  // added to card damage
    int hand_size = 8;
    int hp_base = 1;       // max HP at level 1, before CON
    int hp_per_level = 0;  // gained on each level after the first, before CON
    ArmorDef armor;
    std::vector<Attribute> saving_throws;
    SkillChoice skill_choice;
    AttributeScores suggested_attributes;
    VisualDef visual;
};

}  // namespace rpg::character
