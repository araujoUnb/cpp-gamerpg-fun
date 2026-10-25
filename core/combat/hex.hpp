#pragma once

#include <array>
#include <compare>
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

// The playable area of a scenario: a set of hexes, some of them obstacles.
// Queries use a dense grid over the bounding box, so they are O(1); this is
// the inner loop of movement, AI and training simulations.
class HexMap {
public:
    HexMap() = default;
    explicit HexMap(std::set<Hex> hexes, std::set<Hex> obstacles = {});

    // Every hex within `radius` of the origin.
    static HexMap hexagon(int radius, std::set<Hex> obstacles = {});

    bool contains(Hex h) const { return cell(h) != kOutside; }
    bool is_obstacle(Hex h) const { return cell(h) == kObstacle; }
    // Inside the map and not an obstacle.
    bool walkable(Hex h) const { return cell(h) == kOpen; }

    const std::set<Hex>& hexes() const { return hexes_; }
    const std::set<Hex>& obstacles() const { return obstacles_; }

    // Dense indexing over the bounding box, in Hex order. -1 outside the box.
    int index_of(Hex h) const {
        const int q = h.q - min_q_, r = h.r - min_r_;
        if (q < 0 || r < 0 || q >= width_ || r >= height_) return -1;
        return q * height_ + r;
    }
    Hex hex_at(int index) const { return {min_q_ + index / height_, min_r_ + index % height_}; }
    int cell_count() const { return width_ * height_; }

private:
    static constexpr unsigned char kOutside = 0, kOpen = 1, kObstacle = 2;

    unsigned char cell(Hex h) const {
        const int i = index_of(h);
        return i < 0 ? kOutside : cells_[static_cast<std::size_t>(i)];
    }

    std::set<Hex> hexes_;
    std::set<Hex> obstacles_;
    int min_q_ = 0, min_r_ = 0, width_ = 0, height_ = 0;
    std::vector<unsigned char> cells_;
};

// Breadth-first movement. `can_pass(h)` says whether a walkable hex can be
// crossed (allies yes, enemies no); `can_stop(h)` whether the move may end there.
// Returns every hex where a move of at most `steps` can end, sorted, including `from`.
std::vector<Hex> reachable(const HexMap& map, Hex from, int steps, const std::function<bool(Hex)>& can_pass,
                           const std::function<bool(Hex)>& can_stop);

// Shortest walking distance from `from` to every hex it can reach within
// `max_steps`, crossing only hexes where can_pass is true. Sorted by hex.
std::vector<std::pair<Hex, int>> path_distances(const HexMap& map, Hex from, const std::function<bool(Hex)>& can_pass,
                                                int max_steps = std::numeric_limits<int>::max());

}  // namespace rpg::combat
