#include "data/class_registry.hpp"
#include "data/data_error.hpp"
#include "data/json_io.hpp"
#include "data/text_table.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace rpg;
using namespace rpg::data;
using character::Attribute;

namespace {

character::Ruleset rules_with_skills() {
    character::Ruleset r;
    r.skills = {{"athletics", Attribute::Strength, "k1"},
                {"stealth", Attribute::Dexterity, "k2"},
                {"arcana", Attribute::Intelligence, "k3"}};
    return r;
}

json valid_class() {
    return json::parse(R"({
        "id": "fighter",
        "name": "class.fighter.name",
        "primary_attribute": "str",
        "hand_size": 10,
        "hit_points": { "base": 10, "per_level": 6 },
        "armor_class": { "base": 16, "max_dex_bonus": 0 },
        "saving_throws": ["str", "con"],
        "skill_choice": { "count": 2, "options": ["athletics", "stealth"] },
        "suggested_attributes": { "str": 15, "dex": 13, "con": 14, "int": 8, "wis": 12, "cha": 10 },
        "visual": { "color": "#8a6d4b" }
    })");
}

// Expects class_from_json to fail with a message containing `needle`.
void expect_class_error(const json& j, const std::string& needle) {
    try {
        class_from_json(j, rules_with_skills(), "test.json");
        FAIL("expected DataError containing: " << needle);
    } catch (const DataError& e) {
        const std::string msg = e.what();
        CAPTURE(msg);
        CHECK(msg.find("test.json") != std::string::npos);
        CHECK(msg.find(needle) != std::string::npos);
    }
}

}  // namespace

TEST_CASE("A valid class loads every field") {
    const auto c = class_from_json(valid_class(), rules_with_skills());
    CHECK(c.id == "fighter");
    CHECK(c.name_key == "class.fighter.name");
    CHECK(c.description_key.empty());
    CHECK(c.primary == Attribute::Strength);
    CHECK(c.hand_size == 10);
    CHECK(c.hp_base == 10);
    CHECK(c.hp_per_level == 6);
    CHECK(c.armor.base == 16);
    CHECK(c.armor.max_dex_bonus == 0);
    CHECK_FALSE(c.armor.extra_attribute.has_value());
    CHECK(c.saving_throws == std::vector{Attribute::Strength, Attribute::Constitution});
    CHECK(c.skill_choice.count == 2);
    CHECK_FALSE(c.skill_choice.any);
    CHECK(c.suggested_attributes.score(Attribute::Strength) == 15);
    CHECK(c.visual.color == "#8a6d4b");
}

TEST_CASE("Skill options accept \"any\"") {
    auto j = valid_class();
    j["skill_choice"] = {{"count", 3}, {"options", "any"}};
    const auto c = class_from_json(j, rules_with_skills());
    CHECK(c.skill_choice.any);
    CHECK(c.skill_choice.count == 3);
}

TEST_CASE("Unarmored defense with an extra attribute") {
    auto j = valid_class();
    j["armor_class"] = {{"base", 10}, {"extra_attribute", "con"}};
    const auto c = class_from_json(j, rules_with_skills());
    CHECK(c.armor.extra_attribute == Attribute::Constitution);
    CHECK_FALSE(c.armor.max_dex_bonus.has_value());
}

TEST_CASE("Invalid classes report file and reason") {
    {
        auto j = valid_class();
        j.erase("hand_size");
        expect_class_error(j, "hand_size");
    }
    {
        auto j = valid_class();
        j["hand_size"] = 13;
        expect_class_error(j, "hand_size");
    }
    {
        auto j = valid_class();
        j["hand_size"] = "10";
        expect_class_error(j, "integer");
    }
    {
        auto j = valid_class();
        j["primary_attribute"] = "luck";
        expect_class_error(j, "luck");
    }
    {
        auto j = valid_class();
        j["hit_points"]["base"] = 0;
        expect_class_error(j, "hit_points.base");
    }
    {
        auto j = valid_class();
        j["skill_choice"]["options"] = {"athletics", "flying"};
        expect_class_error(j, "flying");
    }
    {
        auto j = valid_class();
        j["skill_choice"]["count"] = 3;
        expect_class_error(j, "count");
    }
    {
        auto j = valid_class();
        j["skill_choice"]["options"] = {"stealth", "stealth"};
        expect_class_error(j, "twice");
    }
    {
        auto j = valid_class();
        j["suggested_attributes"]["luck"] = 10;
        expect_class_error(j, "luck");
    }
    {
        auto j = valid_class();
        j["suggested_attributes"]["dex"] = 21;
        expect_class_error(j, "dex");
    }
    {
        auto j = valid_class();
        j["id"] = "";
        expect_class_error(j, "id");
    }
    expect_class_error(json::array(), "object");
}

TEST_CASE("Ruleset loads and validates") {
    const auto j = json::parse(R"({
        "attribute_score": { "min": 3, "max": 18 },
        "hand_size": { "min": 6, "max": 14 },
        "proficiency_by_level": [1, 2, 3],
        "skills": [ { "id": "stealth", "attribute": "dex", "name": "skill.stealth.name" } ]
    })");
    const auto r = ruleset_from_json(j);
    CHECK(r.attribute_score.min == 3);
    CHECK(r.hand_size.max == 14);
    CHECK(r.max_level() == 3);
    REQUIRE(r.find_skill("stealth") != nullptr);
    CHECK(r.find_skill("stealth")->attribute == Attribute::Dexterity);

    auto bad = j;
    bad["proficiency_by_level"] = json::array();
    CHECK_THROWS_AS(ruleset_from_json(bad), DataError);

    bad = j;
    bad["attribute_score"] = {{"min", 20}, {"max", 3}};
    CHECK_THROWS_AS(ruleset_from_json(bad), DataError);

    bad = j;
    bad["skills"].push_back(j["skills"][0]);
    CHECK_THROWS_AS(ruleset_from_json(bad), DataError);
}

TEST_CASE("Character sheets round-trip through JSON") {
    const auto rules = rules_with_skills();
    const auto cls = class_from_json(valid_class(), rules);
    auto hero = character::create_character(rules, "h1", "Aria", cls, cls.suggested_attributes,
                                            {"athletics", "stealth"});
    character::level_up(rules, hero, cls);
    hero.current_hp -= 3;

    const auto j = to_json(hero);
    CHECK(sheet_from_json(j, rules, cls) == hero);
    CHECK(sheet_from_json(json::parse(j.dump()), rules, cls) == hero);
}

TEST_CASE("Loading a sheet that breaks the rules fails") {
    const auto rules = rules_with_skills();
    const auto cls = class_from_json(valid_class(), rules);
    const auto hero = character::create_character(rules, "h1", "Aria", cls, cls.suggested_attributes,
                                                  {"athletics", "stealth"});
    auto j = to_json(hero);
    j["current_hp"] = 999;
    CHECK_THROWS_AS(sheet_from_json(j, rules, cls), DataError);

    j = to_json(hero);
    j["attributes"]["str"] = 25;
    CHECK_THROWS_AS(sheet_from_json(j, rules, cls), DataError);

    j = to_json(hero);
    j.erase("skills");
    CHECK_THROWS_AS(sheet_from_json(j, rules, cls), DataError);
}

TEST_CASE("Rng state round-trips through JSON and resumes the sequence") {
    dice::Rng rng(31337);
    rng.next_u32();
    const auto saved = rng.state();
    const auto text = to_json(saved).dump();

    auto restored = dice::Rng::from_state(rng_state_from_json(json::parse(text)));
    for (int i = 0; i < 20; ++i) REQUIRE(restored.roll(20) == rng.roll(20));

    CHECK_THROWS_AS(rng_state_from_json(json{{"state", 1}, {"inc", 2}}), DataError);
    CHECK_THROWS_AS(rng_state_from_json(json{{"state", -1}, {"inc", 3}}), DataError);
}

TEST_CASE("ClassRegistry rejects duplicates and unknown ids") {
    ClassRegistry reg;
    character::ClassDef c;
    c.id = "a";
    reg.add(c);
    CHECK_THROWS_AS(reg.add(c), DataError);
    CHECK(reg.find("a") != nullptr);
    CHECK(reg.find("b") == nullptr);
    CHECK_THROWS_AS(reg.get("b"), DataError);
}

TEST_CASE("TextTable falls back to the key") {
    TextTable t;
    t.set("hello", "olá");
    CHECK(t.get("hello") == "olá");
    CHECK(t.get("missing.key") == "missing.key");
    CHECK_FALSE(t.contains("missing.key"));
}

TEST_CASE("Missing files raise DataError") {
    CHECK_THROWS_AS(read_json_file("/nonexistent/file.json"), DataError);
    CHECK_THROWS_AS(load_classes("/nonexistent/dir", character::Ruleset{}), DataError);
}
