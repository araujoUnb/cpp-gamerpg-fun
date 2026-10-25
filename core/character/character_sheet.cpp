#include "character/character_sheet.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace rpg::character {

namespace {

[[noreturn]] void reject(const CharacterSheet& sheet, const std::string& what) {
    throw std::invalid_argument("character '" + sheet.id + "': " + what);
}

bool skill_allowed(const ClassDef& cls, const std::string& id) {
    if (cls.skill_choice.any) return true;
    const auto& opts = cls.skill_choice.options;
    return std::find(opts.begin(), opts.end(), id) != opts.end();
}

}  // namespace

int max_hp(const CharacterSheet& sheet, const ClassDef& cls) {
    const int hp = cls.hp_base + cls.hp_per_level * (sheet.level - 1) +
                   sheet.attributes.mod(Attribute::Constitution) * sheet.level;
    return std::max(hp, sheet.level);
}

int armor_class(const CharacterSheet& sheet, const ClassDef& cls) {
    int dex = sheet.attributes.mod(Attribute::Dexterity);
    if (cls.armor.max_dex_bonus) dex = std::min(dex, *cls.armor.max_dex_bonus);
    const int extra = cls.armor.extra_attribute ? sheet.attributes.mod(*cls.armor.extra_attribute) : 0;
    return cls.armor.base + dex + extra;
}

bool has_save_proficiency(const ClassDef& cls, Attribute a) {
    return std::find(cls.saving_throws.begin(), cls.saving_throws.end(), a) != cls.saving_throws.end();
}

bool has_skill_proficiency(const CharacterSheet& sheet, const std::string& skill_id) {
    return std::find(sheet.skills.begin(), sheet.skills.end(), skill_id) != sheet.skills.end();
}

int check_modifier(const Ruleset& rules, const CharacterSheet& sheet, const ClassDef& cls,
                   const CheckSpec& check) {
    const int prof = rules.proficiency_bonus(sheet.level);
    switch (check.kind) {
        case CheckSpec::Kind::Attribute:
            return sheet.attributes.mod(check.attribute);
        case CheckSpec::Kind::Save:
            return sheet.attributes.mod(check.attribute) + (has_save_proficiency(cls, check.attribute) ? prof : 0);
        case CheckSpec::Kind::Skill: {
            const auto* skill = rules.find_skill(check.skill);
            if (!skill) throw std::invalid_argument("unknown skill '" + check.skill + "'");
            return sheet.attributes.mod(skill->attribute) + (has_skill_proficiency(sheet, check.skill) ? prof : 0);
        }
    }
    return 0;
}

dice::CheckResult roll_check(dice::Rng& rng, const Ruleset& rules, const CharacterSheet& sheet,
                             const ClassDef& cls, const CheckSpec& check, int dc, dice::RollMode mode) {
    return dice::roll_check(rng, check_modifier(rules, sheet, cls, check), dc, mode);
}

void validate(const Ruleset& rules, const CharacterSheet& sheet, const ClassDef& cls) {
    if (sheet.id.empty()) reject(sheet, "id must not be empty");
    if (sheet.class_id != cls.id) reject(sheet, "class mismatch");
    if (sheet.level < 1 || sheet.level > rules.max_level()) reject(sheet, "level out of range");

    for (auto a : kAllAttributes) {
        if (!rules.attribute_score.contains(sheet.attributes.score(a))) {
            reject(sheet, std::string(attribute_id(a)) + " must be between " +
                              std::to_string(rules.attribute_score.min) + " and " +
                              std::to_string(rules.attribute_score.max));
        }
    }

    if (static_cast<int>(sheet.skills.size()) != cls.skill_choice.count) {
        reject(sheet, "must have exactly " + std::to_string(cls.skill_choice.count) + " skills");
    }
    std::set<std::string> seen;
    for (const auto& s : sheet.skills) {
        if (!rules.find_skill(s)) reject(sheet, "unknown skill '" + s + "'");
        if (!skill_allowed(cls, s)) reject(sheet, "skill '" + s + "' is not a class option");
        if (!seen.insert(s).second) reject(sheet, "skill '" + s + "' chosen twice");
    }

    if (sheet.current_hp < 0 || sheet.current_hp > max_hp(sheet, cls)) reject(sheet, "current HP out of range");
}

CharacterSheet create_character(const Ruleset& rules, std::string id, std::string name,
                                const ClassDef& cls, const AttributeScores& attributes,
                                std::vector<std::string> skills) {
    CharacterSheet sheet;
    sheet.id = std::move(id);
    sheet.name = std::move(name);
    sheet.class_id = cls.id;
    sheet.level = 1;
    sheet.attributes = attributes;
    sheet.skills = std::move(skills);
    sheet.current_hp = 0;
    validate(rules, sheet, cls);
    sheet.current_hp = max_hp(sheet, cls);
    return sheet;
}

void level_up(const Ruleset& rules, CharacterSheet& sheet, const ClassDef& cls) {
    if (sheet.level >= rules.max_level()) reject(sheet, "already at max level");
    const int before = max_hp(sheet, cls);
    ++sheet.level;
    sheet.current_hp = std::max(0, sheet.current_hp + max_hp(sheet, cls) - before);
}

}  // namespace rpg::character
