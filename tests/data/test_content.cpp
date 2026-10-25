// Validates the real files in data/. A broken content file fails the build's
// tests instead of failing at runtime.
#include "character/character_sheet.hpp"
#include "data/class_registry.hpp"
#include "data/json_io.hpp"
#include "data/text_table.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace rpg;
using character::Attribute;

namespace {

const std::filesystem::path kData = GAMERPG_DATA_DIR;

character::Ruleset load_rules() {
    return data::ruleset_from_json(data::read_json_file(kData / "rules" / "ruleset.json"), "ruleset.json");
}

std::vector<std::string> first_skills(const character::Ruleset& rules, const character::ClassDef& cls) {
    const auto& pool = cls.skill_choice.any ? std::vector<std::string>{} : cls.skill_choice.options;
    std::vector<std::string> out;
    for (int i = 0; i < cls.skill_choice.count; ++i) {
        out.push_back(cls.skill_choice.any ? rules.skills[static_cast<std::size_t>(i)].id
                                           : pool[static_cast<std::size_t>(i)]);
    }
    return out;
}

}  // namespace

TEST_CASE("The ruleset file loads") {
    const auto rules = load_rules();
    CHECK(rules.max_level() == 9);
    CHECK(rules.skills.size() == 18);
}

TEST_CASE("All twelve classes load") {
    const auto rules = load_rules();
    const auto classes = data::load_classes(kData / "classes", rules);
    CHECK(classes.ids() == std::vector<std::string>{"barbarian", "bard", "cleric", "druid", "fighter", "monk",
                                                    "paladin", "ranger", "rogue", "sorcerer", "warlock",
                                                    "wizard"});
}

TEST_CASE("Every text key used by the content exists in pt-BR") {
    const auto rules = load_rules();
    const auto classes = data::load_classes(kData / "classes", rules);
    const auto text = data::load_text_table(kData / "lang" / "pt-BR.json");

    for (const auto& s : rules.skills) {
        CAPTURE(s.name_key);
        CHECK(text.contains(s.name_key));
    }
    for (const auto& id : classes.ids()) {
        const auto& cls = classes.get(id);
        CAPTURE(id);
        CHECK(text.contains(cls.name_key));
        CHECK(text.contains(cls.description_key));
    }
    for (auto a : character::kAllAttributes) {
        CHECK(text.contains("attribute." + std::string(character::attribute_id(a)) + ".name"));
    }
}

TEST_CASE("Every class can build a valid hero and level to the maximum") {
    const auto rules = load_rules();
    const auto classes = data::load_classes(kData / "classes", rules);
    for (const auto& id : classes.ids()) {
        CAPTURE(id);
        const auto& cls = classes.get(id);
        auto hero = character::create_character(rules, "h_" + id, "Teste", cls, cls.suggested_attributes,
                                                first_skills(rules, cls));
        CHECK(character::max_hp(hero, cls) >= 6);
        CHECK(character::armor_class(hero, cls) >= 10);
        CHECK(cls.saving_throws.size() == 2);
        while (hero.level < rules.max_level()) character::level_up(rules, hero, cls);
        CHECK(hero.current_hp == character::max_hp(hero, cls));
    }
}

TEST_CASE("Known values from the SRD-style tables") {
    const auto rules = load_rules();
    const auto classes = data::load_classes(kData / "classes", rules);

    // Barbarian: d12 hit die, unarmored defense 10 + DEX + CON.
    const auto& barb = classes.get("barbarian");
    const auto b = character::create_character(rules, "b", "B", barb, barb.suggested_attributes,
                                               {"athletics", "survival"});
    CHECK(character::max_hp(b, barb) == 12 + 2);
    CHECK(character::armor_class(b, barb) == 10 + 1 + 2);

    // Wizard: d6 hit die, no armor.
    const auto& wiz = classes.get("wizard");
    const auto w = character::create_character(rules, "w", "W", wiz, wiz.suggested_attributes,
                                               {"arcana", "history"});
    CHECK(character::max_hp(w, wiz) == 6 + 2);
    CHECK(character::armor_class(w, wiz) == 10 + 1);
    CHECK(character::check_modifier(rules, w, wiz, character::CheckSpec::of_skill("arcana")) == 2 + 2);
}
