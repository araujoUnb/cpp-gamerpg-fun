#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <set>
#include <vector>

namespace rpg::combat {

// Axial hex coordinates (pointy-top). The third cube coordinate is s = -q - r.
struct Hex {
    int q = 0;
    int r = 0;

    int s() const { return -q - r; }

    friend Hex operator+(Hex a, Hex b) { return {a.q + b.q, a.r + b.r}; }
    friend Hex operator-(Hex a, Hex b) { return {a.q - b.q, a.r - b.r}; }
    friend auto operator<=>(const Hex&, const Hex&) = default;
};

inline constexpr std::array<Hex, 6> kHexDirections{
    Hex{1, 0}, Hex{1, -1}, Hex{0, -1}, Hex{-1, 0}, Hex{-1, 1}, Hex{0, 1}};

inline int distance(Hex a, Hex b) {
    const Hex d = a - b;
    return (std::abs(d.q) + std::abs(d.r) + std::abs(d.s())) / 2;
}

inline std::array<Hex, 6> neighbors(Hex h) {
    std::array<Hex, 6> out{};
    for (std::size_t i = 0; i < 6; ++i) out[i] = h + kHexDirections[i];
    return out;
}

enum class Terrain : std::uint8_t {
    Open,
    Obstacle,   // blocks movement and sight
    Difficult,  // costs 2 movement to enter
    Trap,       // walkable; triggers when entered (the trap itself lives in the combat state)
    Door        // closed door: blocks sight; only heroes can enter, which opens it
};

// The playable area of a scenario. Queries use a dense grid over the bounding
// box, so they are O(1); this is the inner loop of movement, AI and training.
class HexMap {
public:
    HexMap() = default;
    explicit HexMap(std::set<Hex> hexes, std::set<Hex> obstacles = {});

    // Every hex within `radius` of the origin.
    static HexMap hexagon(int radius, std::set<Hex> obstacles = {});

    bool contains(Hex h) const { return cell(h) != kOutside; }
    Terrain terrain(Hex h) const { return contains(h) ? static_cast<Terrain>(cell(h) - 1) : Terrain::Obstacle; }
    // Throws std::invalid_argument for a hex outside the map.
    void set_terrain(Hex h, Terrain t);

    bool is_obstacle(Hex h) const { return terrain(h) == Terrain::Obstacle; }
    // Inside the map and not an obstacle. Doors and traps are walkable here;
    // who may enter them is decided by the caller.
    bool walkable(Hex h) const { return contains(h) && terrain(h) != Terrain::Obstacle; }
    bool blocks_sight(Hex h) const {
        const auto t = terrain(h);
        return t == Terrain::Obstacle || t == Terrain::Door;
    }
    int move_cost(Hex h) const { return terrain(h) == Terrain::Difficult ? 2 : 1; }

    const std::set<Hex>& hexes() const { return hexes_; }
    // Hexes whose terrain is `t`, sorted.
    std::vector<Hex> with_terrain(Terrain t) const;

    // Dense indexing over the bounding box, in Hex order. -1 outside the box.
    int index_of(Hex h) const {
        const int q = h.q - min_q_, r = h.r - min_r_;
        if (q < 0 || r < 0 || q >= width_ || r >= height_) return -1;
        return q * height_ + r;
    }
    Hex hex_at(int index) const { return {min_q_ + index / height_, min_r_ + index % height_}; }
    int cell_count() const { return width_ * height_; }

private:
    static constexpr std::uint8_t kOutside = 0;  // otherwise Terrain value + 1

    std::uint8_t cell(Hex h) const {
        const int i = index_of(h);
        return i < 0 ? kOutside : cells_[static_cast<std::size_t>(i)];
    }

    std::set<Hex> hexes_;
    int min_q_ = 0, min_r_ = 0, width_ = 0, height_ = 0;
    std::vector<std::uint8_t> cells_;
};

// Cheapest movement cost from one hex to every cell of a map, stored densely
// by HexMap::index_of. Reusable: compute() keeps the buffers' capacity.
class DistanceField {
public:
    // Dijkstra with difficult terrain costing 2, entering only walkable hexes
    // where can_pass is true, up to `max_cost`. Since every step costs 1 or 2,
    // three rotating buckets replace the heap.
    template <class Pass>
    void compute(const HexMap& map, Hex from, Pass&& can_pass, int max_cost = std::numeric_limits<int>::max());

    // Cost to reach `h`, or -1 when it cannot be reached.
    int at(const HexMap& map, Hex h) const {
        const int i = map.index_of(h);
        return i < 0 || i >= size() ? -1 : cost_[static_cast<std::size_t>(i)];
    }
    int at_index(int i) const { return cost_[static_cast<std::size_t>(i)]; }
    int size() const { return static_cast<int>(cost_.size()); }

private:
    std::vector<int> cost_;
    std::array<std::vector<int>, 3> buckets_;
};

template <class Pass>
void DistanceField::compute(const HexMap& map, Hex from, Pass&& can_pass, int max_cost) {
    cost_.assign(static_cast<std::size_t>(map.cell_count()), -1);
    for (auto& b : buckets_) b.clear();
    const int start = map.index_of(from);
    if (start < 0) return;
    cost_[static_cast<std::size_t>(start)] = 0;
    buckets_[0].push_back(start);
    for (int d = 0; !buckets_[0].empty() || !buckets_[1].empty() || !buckets_[2].empty(); ++d) {
        auto& level = buckets_[static_cast<std::size_t>(d % 3)];
        // Entries of this level are only added while processing levels d-1 and d-2.
        for (std::size_t j = 0; j < level.size(); ++j) {
            const int i = level[j];
            if (cost_[static_cast<std::size_t>(i)] != d) continue;
            for (Hex n : neighbors(map.hex_at(i))) {
                const int k = map.index_of(n);
                if (k < 0 || !map.walkable(n)) continue;
                const int step = map.move_cost(n);
                const int nd = d + step;
                auto& slot = cost_[static_cast<std::size_t>(k)];
                if (nd > max_cost || (slot >= 0 && slot <= nd) || !can_pass(n)) continue;
                slot = nd;
                buckets_[static_cast<std::size_t>(nd % 3)].push_back(k);
            }
        }
        level.clear();
    }
}

// Cheapest movement cost from `from` to every hex it can reach within
// `max_cost` (difficult terrain costs 2), entering only walkable hexes where
// can_pass is true. Sorted by hex; includes `from` at cost 0.
template <class Pass>
std::vector<std::pair<Hex, int>> path_distances(const HexMap& map, Hex from, Pass&& can_pass,
                                                int max_cost = std::numeric_limits<int>::max()) {
    if (map.index_of(from) < 0) return {{from, 0}};
    DistanceField field;
    field.compute(map, from, can_pass, max_cost);
    std::vector<std::pair<Hex, int>> out;
    for (int i = 0; i < field.size(); ++i) {  // index order is Hex order
        if (field.at_index(i) >= 0) out.emplace_back(map.hex_at(i), field.at_index(i));
    }
    return out;
}

// Every hex where a move of at most `budget` can end (can_stop), plus `from`. Sorted.
template <class Pass, class Stop>
std::vector<Hex> reachable(const HexMap& map, Hex from, int budget, Pass&& can_pass, Stop&& can_stop) {
    std::vector<Hex> out;
    for (const auto& [h, d] : path_distances(map, from, can_pass, budget)) {
        if (h == from || can_stop(h)) out.push_back(h);
    }
    return out;
}

// A* shortest path by movement cost; among equally cheap paths, the one with
// the smallest total `penalty` (e.g. traps on the way). Returns the hexes
// after `from` up to and including `to`; empty when `to` cannot be reached
// (or is `from`). `to` itself only has to be walkable.
template <class Pass, class Penalty>
std::vector<Hex> find_path(const HexMap& map, Hex from, Hex to, Pass&& can_pass, Penalty&& penalty) {
    const int start = map.index_of(from);
    const int goal = map.index_of(to);
    if (start < 0 || goal < 0 || from == to || !map.walkable(to)) return {};

    // Cost is lexicographic: movement first, penalty second.
    constexpr long kScale = 1000;
    std::vector<long> g(static_cast<std::size_t>(map.cell_count()), -1);
    std::vector<int> came(static_cast<std::size_t>(map.cell_count()), -1);
    using Item = std::pair<long, int>;  // f, index
    std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
    g[static_cast<std::size_t>(start)] = 0;
    open.push({static_cast<long>(distance(from, to)) * kScale, start});
    while (!open.empty()) {
        const auto [f, i] = open.top();
        open.pop();
        if (i == goal) break;
        const Hex h = map.hex_at(i);
        const long gi = g[static_cast<std::size_t>(i)];
        if (f - static_cast<long>(distance(h, to)) * kScale > gi) continue;  // stale entry
        for (Hex n : neighbors(h)) {
            const int k = map.index_of(n);
            if (k < 0 || !map.walkable(n) || (k != goal && !can_pass(n))) continue;
            const long step = map.move_cost(n) * kScale + std::min<long>(penalty(n), kScale - 1);
            const long ng = gi + step;
            auto& slot = g[static_cast<std::size_t>(k)];
            if (slot >= 0 && slot <= ng) continue;
            slot = ng;
            came[static_cast<std::size_t>(k)] = i;
            open.push({ng + static_cast<long>(distance(n, to)) * kScale, k});
        }
    }
    if (g[static_cast<std::size_t>(goal)] < 0) return {};
    std::vector<Hex> path;
    for (int i = goal; i != start; i = came[static_cast<std::size_t>(i)]) path.push_back(map.hex_at(i));
    std::reverse(path.begin(), path.end());
    return path;
}

template <class Pass>
std::vector<Hex> find_path(const HexMap& map, Hex from, Hex to, Pass&& can_pass) {
    return find_path(map, from, to, can_pass, [](Hex) { return 0; });
}

// Hexes on the straight line from a to b, both included.
// `nudge` picks the side when the line runs exactly along hex edges.
std::vector<Hex> hex_line(Hex a, Hex b, double nudge = 1e-6);

// True when no hex strictly between a and b blocks sight. Lines along hex
// edges are clear if either side is clear.
bool line_of_sight(const HexMap& map, Hex a, Hex b);

}  // namespace rpg::combat
