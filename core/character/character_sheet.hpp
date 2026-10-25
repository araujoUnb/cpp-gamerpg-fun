#pragma once

#include "character/attributes.hpp"
#include "character/class_def.hpp"
#include "character/ruleset.hpp"
#include "dice/d20.hpp"

#include <string>
#include <vector>

namespace rpg::character {

// The persistent part of a hero. Everything else is derived from it, its class
// and the ruleset.
struct CharacterSheet {
    std::string id;
    std::string name;
    std::string class_id;
    int level = 1;
    AttributeScores attributes;
    std::vector<std::string> skills;  // proficient skill ids
    std::vector<std::string> deck;    // card ids; checked by cards::validate_deck
    int current_hp = 0;

    friend bool operator==(const CharacterSheet&, const CharacterSheet&) = default;
};

// hp_base + hp_per_level * (level - 1) + CON modifier * level, never below level.
int max_hp(const CharacterSheet& sheet, const ClassDef& cls);

int armor_class(const CharacterSheet& sheet, const ClassDef& cls);

bool has_save_proficiency(const ClassDef& cls, Attribute a);
bool has_skill_proficiency(const CharacterSheet& sheet, const std::string& skill_id);

// What is being rolled. Modifiers follow 5e: a raw attribute check adds no
// proficiency; a save or a skill adds it when the hero is proficient.
struct CheckSpec {
    enum class Kind { Attribute, Save, Skill };

    Kind kind = Kind::Attribute;
    Attribute attribute = Attribute::Strength;
    std::string skill;  // Kind::Skill only

    static CheckSpec of_attribute(Attribute a) { return {Kind::Attribute, a, {}}; }
    static CheckSpec save(Attribute a) { return {Kind::Save, a, {}}; }
    static CheckSpec of_skill(std::string id) { return {Kind::Skill, Attribute::Strength, std::move(id)}; }
};

// Throws std::invalid_argument for an unknown skill.
int check_modifier(const Ruleset& rules, const CharacterSheet& sheet, const ClassDef& cls,
                   const CheckSpec& check);

dice::CheckResult roll_check(dice::Rng& rng, const Ruleset& rules, const CharacterSheet& sheet,
                             const ClassDef& cls, const CheckSpec& check, int dc,
                             dice::RollMode mode = dice::RollMode::Normal);

// Throws std::invalid_argument when the sheet breaks a rule: class, level,
// attribute range, skill choices or HP.
void validate(const Ruleset& rules, const CharacterSheet& sheet, const ClassDef& cls);

// New level-1 hero at full HP. Throws std::invalid_argument when invalid.
CharacterSheet create_character(const Ruleset& rules, std::string id, std::string name,
                                const ClassDef& cls, const AttributeScores& attributes,
                                std::vector<std::string> skills);

// Raises the level by one. Damage taken is kept; the HP gained is added.
void level_up(const Ruleset& rules, CharacterSheet& sheet, const ClassDef& cls);

}  // namespace rpg::character
