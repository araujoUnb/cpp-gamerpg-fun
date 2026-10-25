#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
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

using HexPredicate = std::function<bool(Hex)>;

// Cheapest movement cost from `from` to every hex it can reach within
// `max_cost` (Dijkstra; difficult terrain costs 2), entering only walkable
// hexes where can_pass is true. Sorted by hex; includes `from` at cost 0.
std::vector<std::pair<Hex, int>> path_distances(const HexMap& map, Hex from, const HexPredicate& can_pass,
                                                int max_cost = std::numeric_limits<int>::max());

// Every hex where a move of at most `budget` can end (can_stop), plus `from`. Sorted.
std::vector<Hex> reachable(const HexMap& map, Hex from, int budget, const HexPredicate& can_pass,
                           const HexPredicate& can_stop);

// A* shortest path by movement cost; among equally cheap paths, the one with
// the smallest total `penalty` (e.g. traps on the way). Returns the hexes
// after `from` up to and including `to`; empty when `to` cannot be reached
// (or is `from`). `to` itself only has to be walkable.
std::vector<Hex> find_path(const HexMap& map, Hex from, Hex to, const HexPredicate& can_pass,
                           const std::function<int(Hex)>& penalty = nullptr);

// Hexes on the straight line from a to b, both included.
// `nudge` picks the side when the line runs exactly along hex edges.
std::vector<Hex> hex_line(Hex a, Hex b, double nudge = 1e-6);

// True when no hex strictly between a and b blocks sight. Lines along hex
// edges are clear if either side is clear.
bool line_of_sight(const HexMap& map, Hex a, Hex b);

}  // namespace rpg::combat
