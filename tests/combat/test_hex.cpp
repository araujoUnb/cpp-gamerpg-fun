#include "combat/hex.hpp"
#include "dice/rng.hpp"

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

TEST_CASE("Difficult terrain costs two movement") {
    auto map = HexMap::hexagon(3);
    map.set_terrain({1, 0}, Terrain::Difficult);
    CHECK(map.move_cost({1, 0}) == 2);
    CHECK(map.walkable({1, 0}));
    auto all = [](Hex) { return true; };
    const auto d = path_distances(map, {0, 0}, all);
    auto cost = [&](Hex h) {
        for (const auto& [x, c] : d) {
            if (x == h) return c;
        }
        return -1;
    };
    CHECK(cost({1, 0}) == 2);
    CHECK(cost({2, 0}) == 3);  // through the difficult hex or around it: both 3
    // With a budget of 1, the difficult neighbor cannot be entered.
    const auto r = reachable(map, {0, 0}, 1, all, all);
    CHECK(std::find(r.begin(), r.end(), Hex{1, 0}) == r.end());
    CHECK(r.size() == 6);
}

TEST_CASE("A* finds the cheapest path and prefers fewer penalties") {
    auto map = HexMap::hexagon(4, {{1, -1}, {1, 0}, {1, -2}});
    auto all = [](Hex) { return true; };

    const auto p = find_path(map, {0, 0}, {2, -1}, all);
    REQUIRE(p.size() == 4);
    CHECK(p.back() == Hex{2, -1});
    Hex prev{0, 0};
    for (Hex h : p) {
        CHECK(distance(prev, h) == 1);
        CHECK(map.walkable(h));
        prev = h;
    }

    // Two equally short paths to (1, 1): through (1, 0) or (0, 1). Penalize (0, 1).
    const auto open = HexMap::hexagon(3);
    const auto a = find_path(open, {0, 0}, {1, 1}, all, [](Hex h) { return h == Hex{0, 1} ? 1 : 0; });
    CHECK(a == std::vector<Hex>{{1, 0}, {1, 1}});
    const auto b = find_path(open, {0, 0}, {1, 1}, all, [](Hex h) { return h == Hex{1, 0} ? 1 : 0; });
    CHECK(b == std::vector<Hex>{{0, 1}, {1, 1}});

    // Penalties never make a path longer.
    const auto c = find_path(open, {0, 0}, {2, 0}, all, [](Hex h) { return h == Hex{1, 0} ? 500 : 0; });
    CHECK(c.size() == 2);

    CHECK(find_path(open, {0, 0}, {0, 0}, all).empty());
    CHECK(find_path(open, {0, 0}, {9, 9}, all).empty());
    // Fully walled in.
    auto boxed = HexMap::hexagon(3);
    for (Hex n : neighbors({0, 0})) boxed.set_terrain(n, Terrain::Obstacle);
    CHECK(find_path(boxed, {0, 0}, {2, 0}, all).empty());
}

TEST_CASE("A* agrees with Dijkstra on random maps") {
    rpg::dice::Rng rng(11);
    for (int trial = 0; trial < 50; ++trial) {
        auto map = HexMap::hexagon(5);
        for (Hex h : map.hexes()) {
            if (h == Hex{0, 0}) continue;
            const auto roll = rng.below(10);
            if (roll == 0) map.set_terrain(h, Terrain::Obstacle);
            if (roll == 1) map.set_terrain(h, Terrain::Difficult);
        }
        auto all = [](Hex) { return true; };
        for (const auto& [h, d] : path_distances(map, {0, 0}, all)) {
            if (h == Hex{0, 0}) continue;
            const auto p = find_path(map, {0, 0}, h, all);
            int cost = 0;
            for (Hex x : p) cost += map.move_cost(x);
            REQUIRE(cost == d);
        }
    }
}

TEST_CASE("Hex lines and line of sight") {
    const auto line = hex_line({0, 0}, {3, -1});
    REQUIRE(line.size() == 4);
    CHECK(line.front() == Hex{0, 0});
    CHECK(line.back() == Hex{3, -1});
    for (std::size_t i = 1; i < line.size(); ++i) CHECK(distance(line[i - 1], line[i]) == 1);

    auto map = HexMap::hexagon(4);
    CHECK(line_of_sight(map, {0, 0}, {3, 0}));
    map.set_terrain({2, 0}, Terrain::Obstacle);
    CHECK_FALSE(line_of_sight(map, {0, 0}, {3, 0}));
    CHECK_FALSE(line_of_sight(map, {3, 0}, {0, 0}));
    CHECK(line_of_sight(map, {0, 0}, {1, 0}));  // adjacent always sees
    CHECK(line_of_sight(map, {0, 0}, {0, 3}));

    // A line running along the edge between two hexes is blocked only if both are.
    // From (0,0) to (2,-1): passes between (1,0) and (1,-1).
    auto edge = HexMap::hexagon(3);
    edge.set_terrain({1, 0}, Terrain::Obstacle);
    CHECK(line_of_sight(edge, {0, 0}, {2, -1}));
    edge.set_terrain({1, -1}, Terrain::Obstacle);
    CHECK_FALSE(line_of_sight(edge, {0, 0}, {2, -1}));

    // Closed doors block sight; difficult terrain and traps do not.
    auto doors = HexMap::hexagon(3);
    doors.set_terrain({1, 0}, Terrain::Door);
    CHECK_FALSE(line_of_sight(doors, {0, 0}, {2, 0}));
    doors.set_terrain({1, 0}, Terrain::Difficult);
    CHECK(line_of_sight(doors, {0, 0}, {2, 0}));
    doors.set_terrain({1, 0}, Terrain::Trap);
    CHECK(line_of_sight(doors, {0, 0}, {2, 0}));
}

TEST_CASE("Line of sight is symmetric on random maps") {
    rpg::dice::Rng rng(3);
    auto map = HexMap::hexagon(5);
    for (Hex h : map.hexes()) {
        if (rng.below(6) == 0) map.set_terrain(h, Terrain::Obstacle);
    }
    for (Hex a : map.hexes()) {
        for (Hex b : map.hexes()) REQUIRE(line_of_sight(map, a, b) == line_of_sight(map, b, a));
    }
}

TEST_CASE("Terrain bookkeeping") {
    auto map = HexMap::hexagon(2);
    CHECK_THROWS_AS(map.set_terrain({5, 5}, Terrain::Open), std::invalid_argument);
    map.set_terrain({1, 0}, Terrain::Door);
    map.set_terrain({0, 1}, Terrain::Door);
    CHECK(map.with_terrain(Terrain::Door) == std::vector<Hex>{{0, 1}, {1, 0}});
    CHECK(map.terrain({9, 9}) == Terrain::Obstacle);  // outside reads as blocked
    CHECK_FALSE(map.blocks_sight({0, 0}));
}
