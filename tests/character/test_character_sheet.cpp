#include "character/character_sheet.hpp"

#include <doctest/doctest.h>

#include <stdexcept>

using namespace rpg::character;
using rpg::dice::Rng;

namespace {

Ruleset test_rules() {
    Ruleset r;
    r.skills = {
        {"athletics", Attribute::Strength, "skill.athletics.name"},
        {"stealth", Attribute::Dexterity, "skill.stealth.name"},
        {"arcana", Attribute::Intelligence, "skill.arcana.name"},
    };
    return r;
}

ClassDef test_class() {
    ClassDef c;
    c.id = "tester";
    c.hand_size = 10;
    c.hp_base = 10;
    c.hp_per_level = 6;
    c.armor = {16, 0, std::nullopt};
    c.saving_throws = {Attribute::Strength, Attribute::Constitution};
    c.skill_choice = {1, false, {"athletics", "stealth"}};
    return c;
}

AttributeScores scores(int str, int dex, int con, int intl, int wis, int cha) {
    AttributeScores s;
    s.set(Attribute::Strength, str);
    s.set(Attribute::Dexterity, dex);
    s.set(Attribute::Constitution, con);
    s.set(Attribute::Intelligence, intl);
    s.set(Attribute::Wisdom, wis);
    s.set(Attribute::Charisma, cha);
    return s;
}

CharacterSheet make_hero(const Ruleset& rules, const ClassDef& cls) {
    return create_character(rules, "h1", "Aria", cls, scores(15, 13, 14, 8, 12, 10), {"athletics"});
}

}  // namespace

TEST_CASE("Proficiency bonus follows the ruleset table") {
    const Ruleset r;
    CHECK(r.max_level() == 9);
    CHECK(r.proficiency_bonus(1) == 2);
    CHECK(r.proficiency_bonus(4) == 2);
    CHECK(r.proficiency_bonus(5) == 3);
    CHECK(r.proficiency_bonus(8) == 3);
    CHECK(r.proficiency_bonus(9) == 4);
    CHECK_THROWS_AS(r.proficiency_bonus(0), std::invalid_argument);
    CHECK_THROWS_AS(r.proficiency_bonus(10), std::invalid_argument);
}

TEST_CASE("A different ruleset changes the limits without code changes") {
    Ruleset r = test_rules();
    r.proficiency_by_level = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6};
    r.attribute_score = {3, 18};
    const auto cls = test_class();
    auto hero = create_character(r, "h", "Low", cls, scores(3, 10, 10, 10, 10, 10), {"stealth"});
    CHECK(r.max_level() == 12);
    for (int i = 1; i < 12; ++i) level_up(r, hero, cls);
    CHECK(hero.level == 12);
    CHECK(check_modifier(r, hero, cls, CheckSpec::save(Attribute::Strength)) == -4 + 6);
}

TEST_CASE("New hero starts at level 1 with full HP") {
    const auto r = test_rules();
    const auto cls = test_class();
    const auto hero = make_hero(r, cls);
    CHECK(hero.level == 1);
    CHECK(hero.class_id == "tester");
    CHECK(max_hp(hero, cls) == 10 + 2);  // base + CON 14
    CHECK(hero.current_hp == max_hp(hero, cls));
}

TEST_CASE("Max HP adds per-level HP and CON on every level") {
    const auto r = test_rules();
    const auto cls = test_class();
    auto hero = make_hero(r, cls);
    hero.level = 3;
    CHECK(max_hp(hero, cls) == 10 + 6 * 2 + 2 * 3);
}

TEST_CASE("Max HP never drops below one per level") {
    auto cls = test_class();
    cls.hp_base = 1;
    cls.hp_per_level = 0;
    CharacterSheet s;
    s.attributes.set(Attribute::Constitution, 8);
    s.level = 4;
    CHECK(max_hp(s, cls) == 4);
}

TEST_CASE("Armor class handles DEX caps and an extra attribute") {
    CharacterSheet s;
    s.attributes.set(Attribute::Dexterity, 16);    // +3
    s.attributes.set(Attribute::Constitution, 14); // +2

    ClassDef heavy;
    heavy.armor = {16, 0, std::nullopt};
    CHECK(armor_class(s, heavy) == 16);

    ClassDef medium;
    medium.armor = {14, 2, std::nullopt};
    CHECK(armor_class(s, medium) == 16);

    ClassDef light;
    light.armor = {11, std::nullopt, std::nullopt};
    CHECK(armor_class(s, light) == 14);

    ClassDef unarmored;
    unarmored.armor = {10, std::nullopt, Attribute::Constitution};
    CHECK(armor_class(s, unarmored) == 15);

    s.attributes.set(Attribute::Dexterity, 8);  // a negative DEX still counts against heavy armor caps
    CHECK(armor_class(s, heavy) == 15);
}

TEST_CASE("Check modifiers: attribute, save and skill") {
    const auto r = test_rules();
    const auto cls = test_class();
    const auto hero = make_hero(r, cls);  // STR 15 (+2), DEX 13 (+1), INT 8 (-1)

    CHECK(check_modifier(r, hero, cls, CheckSpec::of_attribute(Attribute::Strength)) == 2);
    CHECK(check_modifier(r, hero, cls, CheckSpec::save(Attribute::Strength)) == 4);
    CHECK(check_modifier(r, hero, cls, CheckSpec::save(Attribute::Dexterity)) == 1);
    CHECK(check_modifier(r, hero, cls, CheckSpec::of_skill("athletics")) == 4);
    CHECK(check_modifier(r, hero, cls, CheckSpec::of_skill("stealth")) == 1);
    CHECK(check_modifier(r, hero, cls, CheckSpec::of_skill("arcana")) == -1);
    CHECK_THROWS_AS(check_modifier(r, hero, cls, CheckSpec::of_skill("flying")), std::invalid_argument);
}

TEST_CASE("Proficiency grows with level") {
    const auto r = test_rules();
    const auto cls = test_class();
    auto hero = make_hero(r, cls);
    for (int i = 0; i < 4; ++i) level_up(r, hero, cls);
    CHECK(hero.level == 5);
    CHECK(check_modifier(r, hero, cls, CheckSpec::of_skill("athletics")) == 2 + 3);
}

TEST_CASE("roll_check uses the computed modifier and is deterministic") {
    const auto r = test_rules();
    const auto cls = test_class();
    const auto hero = make_hero(r, cls);
    Rng a(5);
    Rng b(5);
    for (int i = 0; i < 50; ++i) {
        const auto ra = roll_check(a, r, hero, cls, CheckSpec::of_skill("athletics"), 12);
        const auto rb = roll_check(b, r, hero, cls, CheckSpec::of_skill("athletics"), 12);
        REQUIRE(ra.modifier == 4);
        REQUIRE(ra.total == ra.roll.kept + 4);
        REQUIRE(ra.total == rb.total);
    }
}

TEST_CASE("Level up keeps damage taken and adds the HP gained") {
    const auto r = test_rules();
    const auto cls = test_class();
    auto hero = make_hero(r, cls);  // 12 max
    hero.current_hp = 5;
    level_up(r, hero, cls);          // +6 +2
    CHECK(hero.level == 2);
    CHECK(hero.current_hp == 13);
    CHECK(max_hp(hero, cls) == 20);
}

TEST_CASE("Level up stops at the maximum level") {
    const auto r = test_rules();
    const auto cls = test_class();
    auto hero = make_hero(r, cls);
    for (int i = 1; i < r.max_level(); ++i) level_up(r, hero, cls);
    CHECK(hero.level == 9);
    CHECK_THROWS_AS(level_up(r, hero, cls), std::invalid_argument);
}

TEST_CASE("create_character rejects invalid input") {
    const auto r = test_rules();
    const auto cls = test_class();
    const auto ok = scores(15, 13, 14, 8, 12, 10);

    CHECK_THROWS_AS(create_character(r, "h", "X", cls, scores(7, 13, 14, 8, 12, 10), {"athletics"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(create_character(r, "h", "X", cls, scores(21, 13, 14, 8, 12, 10), {"athletics"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(create_character(r, "", "X", cls, ok, {"athletics"}), std::invalid_argument);
    CHECK_THROWS_AS(create_character(r, "h", "X", cls, ok, {}), std::invalid_argument);
    CHECK_THROWS_AS(create_character(r, "h", "X", cls, ok, {"arcana"}), std::invalid_argument);
    CHECK_THROWS_AS(create_character(r, "h", "X", cls, ok, {"flying"}), std::invalid_argument);
    CHECK_THROWS_AS(create_character(r, "h", "X", cls, ok, {"athletics", "stealth"}), std::invalid_argument);
}

TEST_CASE("validate rejects duplicates, class mismatch and bad HP") {
    const auto r = test_rules();
    auto cls = test_class();
    cls.skill_choice = {2, true, {}};
    auto hero = create_character(r, "h", "X", cls, scores(15, 13, 14, 8, 12, 10), {"arcana", "stealth"});
    CHECK_NOTHROW(validate(r, hero, cls));

    auto dup = hero;
    dup.skills = {"arcana", "arcana"};
    CHECK_THROWS_AS(validate(r, dup, cls), std::invalid_argument);

    auto other = cls;
    other.id = "other";
    CHECK_THROWS_AS(validate(r, hero, other), std::invalid_argument);

    auto hurt = hero;
    hurt.current_hp = -1;
    CHECK_THROWS_AS(validate(r, hurt, cls), std::invalid_argument);
    hurt.current_hp = max_hp(hero, cls) + 1;
    CHECK_THROWS_AS(validate(r, hurt, cls), std::invalid_argument);

    auto high = hero;
    high.level = 10;
    CHECK_THROWS_AS(validate(r, high, cls), std::invalid_argument);
}
