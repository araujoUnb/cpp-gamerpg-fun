#include "data/card_io.hpp"
#include "data/data_error.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace rpg;
using namespace rpg::data;
using cards::ActionType;
using cards::TargetKind;

namespace {

character::Ruleset rules() {
    character::Ruleset r;
    r.conditions = {{"stunned", "c1", ""}, {"poisoned", "c2", ""}};
    return r;
}

json valid_card() {
    return json::parse(R"({
        "id": "fighter.test",
        "level": 1,
        "name": "card.fighter.test.name",
        "initiative": 35,
        "top": { "actions": [ { "type": "attack", "value": 3, "conditions": [ { "id": "stunned", "rounds": 1 } ] } ],
                 "lose": true },
        "bottom": { "actions": [ { "type": "move", "value": 3 } ] }
    })");
}

void expect_action_error(const std::string& text, const std::string& needle) {
    CAPTURE(text);
    try {
        action_from_json(json::parse(text), rules(), "a.json");
        FAIL("expected DataError containing: " << needle);
    } catch (const DataError& e) {
        const std::string msg = e.what();
        CAPTURE(msg);
        CHECK(msg.find(needle) != std::string::npos);
    }
}

}  // namespace

TEST_CASE("A valid card loads") {
    const auto c = card_from_json(valid_card(), rules(), "fighter");
    CHECK(c.id == "fighter.test");
    CHECK(c.class_id == "fighter");
    CHECK(c.initiative == 35);
    CHECK(c.top.lose);
    CHECK_FALSE(c.bottom.lose);
    REQUIRE(c.top.actions.size() == 1);
    const auto& a = c.top.actions[0];
    CHECK(a.type == ActionType::Attack);
    CHECK(a.value == 3);
    CHECK(a.range == 1);
    CHECK(a.targets == 1);
    CHECK(a.target == TargetKind::Enemy);
    REQUIRE(a.conditions.size() == 1);
    CHECK(a.conditions[0].id == "stunned");
}

TEST_CASE("Action defaults") {
    const auto heal = action_from_json(json::parse(R"({"type":"heal","value":2})"), rules());
    CHECK(heal.target == TargetKind::Self);
    CHECK(heal.range == 0);

    const auto ally = action_from_json(json::parse(R"({"type":"heal","value":2,"target":"ally","range":3})"), rules());
    CHECK(ally.target == TargetKind::Ally);
    CHECK(ally.range == 3);

    const auto stun = action_from_json(
        json::parse(R"({"type":"condition","target":"enemy","conditions":[{"id":"stunned","rounds":1}]})"), rules());
    CHECK(stun.range == 1);

    const auto jump = action_from_json(json::parse(R"({"type":"move","value":4,"jump":true})"), rules());
    CHECK(jump.jump);
}

TEST_CASE("Invalid actions are rejected with a reason") {
    expect_action_error(R"({"type":"fly","value":2})", "fly");
    expect_action_error(R"({"type":"move"})", "value");
    expect_action_error(R"({"type":"move","value":0})", "value");
    expect_action_error(R"({"type":"move","value":2,"range":3})", "range");
    expect_action_error(R"({"type":"attack","value":2,"rnage":3})", "rnage");
    expect_action_error(R"({"type":"attack","value":2,"range":0})", "range");
    expect_action_error(R"({"type":"attack","value":2,"push":1,"pull":1})", "push");
    expect_action_error(R"({"type":"attack","value":2,"conditions":[{"id":"burning","rounds":1}]})", "burning");
    expect_action_error(R"({"type":"attack","value":2,"conditions":[{"id":"stunned","rounds":0}]})", "rounds");
    expect_action_error(R"({"type":"attack","value":2,"advantage":"yes"})", "advantage");
    expect_action_error(R"({"type":"heal","value":2,"target":"enemy"})", "enemies");
    expect_action_error(R"({"type":"heal","value":2,"range":2})", "self");
    expect_action_error(R"({"type":"condition","target":"ally","conditions":[]})", "condition");
    expect_action_error(R"({"type":"condition","conditions":[{"id":"stunned","rounds":1}]})", "target");
    expect_action_error(R"([1,2])", "object");
}

TEST_CASE("Invalid cards are rejected") {
    auto expect = [](json j, const std::string& needle) {
        try {
            card_from_json(j, rules(), "fighter", "f.json");
            FAIL("expected DataError containing: " << needle);
        } catch (const DataError& e) {
            const std::string msg = e.what();
            CAPTURE(msg);
            CHECK(msg.find(needle) != std::string::npos);
        }
    };
    auto j = valid_card();
    j["id"] = "rogue.test";
    expect(j, "must start with");

    j = valid_card();
    j["id"] = "fighter.";
    expect(j, "must start with");

    j = valid_card();
    j["initiative"] = 0;
    expect(j, "initiative");

    j = valid_card();
    j["initiative"] = 100;
    expect(j, "initiative");

    j = valid_card();
    j["level"] = 10;
    expect(j, "level");

    j = valid_card();
    j["top"]["actions"] = json::array();
    expect(j, "at least one action");

    j = valid_card();
    j["bottom"]["perdivel"] = true;
    expect(j, "perdivel");

    j = valid_card();
    j.erase("bottom");
    expect(j, "bottom");
}

TEST_CASE("Hero card piles round-trip through JSON") {
    const std::vector<std::string> deck{"t.1", "t.2", "t.3", "t.4", "t.5"};
    const auto h = cards::HeroCards::from_piles(deck, {"t.1"}, {"t.2"}, {"t.3"},
                                                cards::RoundSelection::play("t.5", "t.4"));
    const auto j = json::parse(to_json(h).dump());
    const auto back = hero_cards_from_json(j, deck);
    CHECK(back.hand() == h.hand());
    CHECK(back.discard() == h.discard());
    CHECK(back.lost() == h.lost());
    CHECK(back.selection() == h.selection());

    const auto resting = cards::HeroCards::from_piles(deck, {"t.1"}, {"t.2", "t.3", "t.4"}, {"t.5"},
                                                      cards::RoundSelection::rest());
    CHECK(hero_cards_from_json(to_json(resting), deck).selection() == cards::RoundSelection::rest());

    auto bad = j;
    bad["lost"].push_back("t.9");
    CHECK_THROWS_AS(hero_cards_from_json(bad, deck), DataError);
}
