#include "dice/d20.hpp"

#include <doctest/doctest.h>

#include <stdexcept>

using namespace rpg::dice;

TEST_CASE("Advantage and disadvantage cancel each other") {
    CHECK(combine_mode(false, false) == RollMode::Normal);
    CHECK(combine_mode(true, false) == RollMode::Advantage);
    CHECK(combine_mode(false, true) == RollMode::Disadvantage);
    CHECK(combine_mode(true, true) == RollMode::Normal);
}

TEST_CASE("make_d20 keeps the right die") {
    CHECK(make_d20(RollMode::Normal, 7).kept == 7);
    CHECK(make_d20(RollMode::Advantage, 4, 15).kept == 15);
    CHECK(make_d20(RollMode::Advantage, 15, 4).kept == 15);
    CHECK(make_d20(RollMode::Disadvantage, 4, 15).kept == 4);
    CHECK(make_d20(RollMode::Disadvantage, 15, 4).kept == 4);
}

TEST_CASE("make_d20 rejects impossible faces") {
    CHECK_THROWS_AS(make_d20(RollMode::Normal, 0), std::invalid_argument);
    CHECK_THROWS_AS(make_d20(RollMode::Normal, 21), std::invalid_argument);
    CHECK_THROWS_AS(make_d20(RollMode::Advantage, 10), std::invalid_argument);
}

TEST_CASE("roll_d20 uses one die for normal rolls and two otherwise") {
    Rng rng(11);
    for (int i = 0; i < 500; ++i) {
        const auto n = roll_d20(rng, RollMode::Normal);
        REQUIRE(n.second == 0);
        REQUIRE(n.kept == n.first);

        const auto adv = roll_d20(rng, RollMode::Advantage);
        REQUIRE(adv.second >= 1);
        REQUIRE(adv.kept == std::max(adv.first, adv.second));

        const auto dis = roll_d20(rng, RollMode::Disadvantage);
        REQUIRE(dis.second >= 1);
        REQUIRE(dis.kept == std::min(dis.first, dis.second));
    }
}

TEST_CASE("Advantage raises and disadvantage lowers the average") {
    Rng rng(3);
    constexpr int kRolls = 20000;
    long normal = 0, adv = 0, dis = 0;
    for (int i = 0; i < kRolls; ++i) {
        normal += roll_d20(rng, RollMode::Normal).kept;
        adv += roll_d20(rng, RollMode::Advantage).kept;
        dis += roll_d20(rng, RollMode::Disadvantage).kept;
    }
    // Expected means: 10.5, 13.825, 7.175.
    CHECK(static_cast<double>(normal) / kRolls == doctest::Approx(10.5).epsilon(0.03));
    CHECK(static_cast<double>(adv) / kRolls == doctest::Approx(13.825).epsilon(0.03));
    CHECK(static_cast<double>(dis) / kRolls == doctest::Approx(7.175).epsilon(0.03));
}

TEST_CASE("Check succeeds when total meets or beats the target") {
    const auto meets = resolve_check(make_d20(RollMode::Normal, 10), 3, 13);
    CHECK(meets.total == 13);
    CHECK(meets.success);

    const auto misses = resolve_check(make_d20(RollMode::Normal, 9), 3, 13);
    CHECK(misses.total == 12);
    CHECK_FALSE(misses.success);

    CHECK_FALSE(meets.critical);
    CHECK_FALSE(meets.fumble);
}

TEST_CASE("Natural 20 is a critical and always succeeds") {
    const auto r = resolve_check(make_d20(RollMode::Normal, 20), -5, 30);
    CHECK(r.critical);
    CHECK(r.success);
    CHECK_FALSE(r.fumble);
}

TEST_CASE("Natural 1 always fails") {
    const auto r = resolve_check(make_d20(RollMode::Normal, 1), 50, 10);
    CHECK(r.fumble);
    CHECK_FALSE(r.success);
    CHECK_FALSE(r.critical);
}

TEST_CASE("Critical and fumble follow the kept die, not the discarded one") {
    CHECK_FALSE(resolve_check(make_d20(RollMode::Disadvantage, 20, 5), 0, 10).critical);
    CHECK(resolve_check(make_d20(RollMode::Advantage, 20, 5), 0, 10).critical);
    CHECK_FALSE(resolve_check(make_d20(RollMode::Advantage, 1, 12), 0, 10).fumble);
    CHECK(resolve_check(make_d20(RollMode::Disadvantage, 1, 12), 0, 10).fumble);
}

TEST_CASE("roll_check is deterministic for a seed") {
    Rng a(77);
    Rng b(77);
    for (int i = 0; i < 100; ++i) {
        const auto ra = roll_check(a, 4, 15, RollMode::Advantage);
        const auto rb = roll_check(b, 4, 15, RollMode::Advantage);
        REQUIRE(ra.roll.kept == rb.roll.kept);
        REQUIRE(ra.success == rb.success);
    }
}
