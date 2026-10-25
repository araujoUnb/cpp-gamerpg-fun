#include "dice/dice_expr.hpp"

#include <doctest/doctest.h>

#include <numeric>

using namespace rpg::dice;

TEST_CASE("parse_dice accepts common forms") {
    CHECK(parse_dice("2d6+3") == DiceExpr{2, 6, 3});
    CHECK(parse_dice("1d4-1") == DiceExpr{1, 4, -1});
    CHECK(parse_dice("d8") == DiceExpr{1, 8, 0});
    CHECK(parse_dice("3D10") == DiceExpr{3, 10, 0});
    CHECK(parse_dice("5") == DiceExpr{0, 0, 5});
    CHECK(parse_dice("-2") == DiceExpr{0, 0, -2});
    CHECK(parse_dice("  1d12  ") == DiceExpr{1, 12, 0});
}

TEST_CASE("parse_dice rejects malformed input") {
    for (const char* bad : {"", "d", "2d", "2d6+", "2d6x", "0d6", "1d1", "1d6+2+3", "abc", "1d6 + 2",
                            "101d6", "1d1001", "+"}) {
        CAPTURE(bad);
        CHECK_FALSE(parse_dice(bad).has_value());
    }
}

TEST_CASE("to_string round-trips") {
    for (const char* text : {"2d6+3", "1d4-1", "1d8", "5", "-2"}) {
        CAPTURE(text);
        const auto expr = parse_dice(text);
        REQUIRE(expr.has_value());
        CHECK(to_string(*expr) == text);
    }
}

TEST_CASE("roll_damage sums dice and bonus") {
    Rng rng(10);
    const DiceExpr expr{2, 6, 3};
    for (int i = 0; i < 500; ++i) {
        const auto r = roll_damage(rng, expr);
        REQUIRE(r.dice.size() == 2);
        for (int d : r.dice) {
            REQUIRE(d >= 1);
            REQUIRE(d <= 6);
        }
        REQUIRE(r.total == std::accumulate(r.dice.begin(), r.dice.end(), 0) + 3);
    }
}

TEST_CASE("Critical doubles the dice but not the bonus") {
    Rng rng(10);
    const auto r = roll_damage(rng, DiceExpr{2, 6, 3}, true);
    CHECK(r.dice.size() == 4);
    CHECK(r.bonus == 3);
    CHECK(r.total == std::accumulate(r.dice.begin(), r.dice.end(), 0) + 3);
}

TEST_CASE("Flat damage ignores criticals") {
    Rng rng(1);
    const auto r = roll_damage(rng, DiceExpr{0, 0, 4}, true);
    CHECK(r.dice.empty());
    CHECK(r.total == 4);
}

TEST_CASE("Damage never goes below zero") {
    Rng rng(1);
    for (int i = 0; i < 100; ++i) {
        REQUIRE(roll_damage(rng, DiceExpr{1, 4, -10}).total == 0);
    }
}
