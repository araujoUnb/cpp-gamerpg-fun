#include "combat/hex.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <stdexcept>

using namespace rpg::combat;

TEST_CASE("Hex distance") {
    CHECK(distance({0, 0}, {0, 0}) == 0);
    for (Hex n : neighbors({0, 0})) CHECK(distance({0, 0}, n) == 1);
    CHECK(distance({0, 0}, {2, -1}) == 2);
    CHECK(distance({-2, 3}, {2, -1}) == 4);
    CHECK(distance({1, 2}, {-3, 0}) == distance({-3, 0}, {1, 2}));
}

TEST_CASE("Hexagon maps have 3r(r+1)+1 hexes") {
    for (int r = 0; r <= 5; ++r) {
        const auto map = HexMap::hexagon(r);
        CHECK(static_cast<int>(map.hexes().size()) == 3 * r * (r + 1) + 1);
        for (Hex h : map.hexes()) CHECK(distance(h, {0, 0}) <= r);
    }
    CHECK_THROWS_AS(HexMap::hexagon(-1), std::invalid_argument);
    CHECK_THROWS_AS(HexMap::hexagon(1, {{5, 5}}), std::invalid_argument);
}

TEST_CASE("Obstacles are not walkable") {
    const auto map = HexMap::hexagon(2, {{1, 0}});
    CHECK(map.contains({1, 0}));
    CHECK_FALSE(map.walkable({1, 0}));
    CHECK(map.walkable({0, 0}));
    CHECK_FALSE(map.walkable({3, 0}));
}

TEST_CASE("Movement goes around obstacles and blockers") {
    // A wall of obstacles east and north-east of the origin.
    const auto map = HexMap::hexagon(3, {{1, -1}, {1, 0}, {1, -2}});
    auto all = [](Hex) { return true; };

    const auto d = path_distances(map, {0, 0}, all);
    auto dist_to = [&](Hex h) {
        const auto it = std::find_if(d.begin(), d.end(), [&](const auto& p) { return p.first == h; });
        return it == d.end() ? -1 : it->second;
    };
    CHECK(dist_to({0, 0}) == 0);
    CHECK(dist_to({2, -1}) == 4);  // (0,1) (1,1) (2,0) (2,-1), around the wall
    CHECK(dist_to({1, 0}) == -1);  // obstacle

    const auto r1 = reachable(map, {0, 0}, 1, all, all);
    CHECK(r1.size() == 7 - 2);  // origin + 6 neighbors - 2 obstacles
    CHECK(std::is_sorted(r1.begin(), r1.end()));

    // A creature at (0, 1) can be passed but not stopped on.
    auto not_ally = [](Hex h) { return h != Hex{0, 1}; };
    const auto r2 = reachable(map, {0, 0}, 2, all, not_ally);
    CHECK(std::find(r2.begin(), r2.end(), Hex{0, 1}) == r2.end());
    CHECK(std::find(r2.begin(), r2.end(), Hex{0, 2}) != r2.end());

    // An enemy at (0, 1) cannot even be passed.
    const auto r3 = reachable(map, {0, 0}, 1, not_ally, not_ally);
    CHECK(std::find(r3.begin(), r3.end(), Hex{0, 1}) == r3.end());
}
