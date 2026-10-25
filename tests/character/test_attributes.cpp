#include "character/attributes.hpp"

#include <doctest/doctest.h>

using namespace rpg::character;

TEST_CASE("Modifier rounds down, also below 10") {
    CHECK(modifier(1) == -5);
    CHECK(modifier(7) == -2);
    CHECK(modifier(8) == -1);
    CHECK(modifier(9) == -1);
    CHECK(modifier(10) == 0);
    CHECK(modifier(11) == 0);
    CHECK(modifier(12) == 1);
    CHECK(modifier(15) == 2);
    CHECK(modifier(20) == 5);
    CHECK(modifier(30) == 10);
}

TEST_CASE("Attribute ids round-trip") {
    for (auto a : kAllAttributes) {
        CHECK(parse_attribute(attribute_id(a)) == a);
    }
    CHECK_FALSE(parse_attribute("STR").has_value());
    CHECK_FALSE(parse_attribute("").has_value());
}

TEST_CASE("AttributeScores default to 10 and expose modifiers") {
    AttributeScores s;
    for (auto a : kAllAttributes) CHECK(s.score(a) == 10);
    s.set(Attribute::Dexterity, 16);
    CHECK(s.mod(Attribute::Dexterity) == 3);
    CHECK(s.mod(Attribute::Strength) == 0);
}
