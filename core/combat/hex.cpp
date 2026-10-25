#include "combat/hex.hpp"

#include <algorithm>
#include <stdexcept>

namespace rpg::combat {

HexMap::HexMap(std::set<Hex> hexes, std::set<Hex> obstacles) : hexes_(std::move(hexes)), obstacles_(std::move(obstacles)) {
    for (const auto& o : obstacles_) {
        if (!hexes_.count(o)) throw std::invalid_argument("obstacle outside the map");
    }
    if (hexes_.empty()) return;
    int max_q = hexes_.begin()->q, max_r = hexes_.begin()->r;
    min_q_ = max_q;
    min_r_ = max_r;
    for (Hex h : hexes_) {
        min_q_ = std::min(min_q_, h.q);
        min_r_ = std::min(min_r_, h.r);
        max_q = std::max(max_q, h.q);
        max_r = std::max(max_r, h.r);
    }
    width_ = max_q - min_q_ + 1;
    height_ = max_r - min_r_ + 1;
    cells_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), kOutside);
    for (Hex h : hexes_) cells_[static_cast<std::size_t>(index_of(h))] = kOpen;
    for (Hex h : obstacles_) cells_[static_cast<std::size_t>(index_of(h))] = kObstacle;
}

HexMap HexMap::hexagon(int radius, std::set<Hex> obstacles) {
    if (radius < 0) throw std::invalid_argument("radius must not be negative");
    std::set<Hex> hexes;
    for (int q = -radius; q <= radius; ++q) {
        for (int r = std::max(-radius, -q - radius); r <= std::min(radius, -q + radius); ++r) {
            hexes.insert({q, r});
        }
    }
    return HexMap(std::move(hexes), std::move(obstacles));
}

std::vector<std::pair<Hex, int>> path_distances(const HexMap& map, Hex from, const std::function<bool(Hex)>& can_pass,
                                                int max_steps) {
    const int start = map.index_of(from);
    if (start < 0) return {{from, 0}};
    // Flat arrays over the map's bounding box; the queue is a plain vector.
    std::vector<int> dist(static_cast<std::size_t>(map.cell_count()), -1);
    std::vector<int> queue;
    queue.reserve(dist.size());
    dist[static_cast<std::size_t>(start)] = 0;
    queue.push_back(start);
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int i = queue[head];
        const int d = dist[static_cast<std::size_t>(i)];
        if (d >= max_steps) continue;
        const Hex h = map.hex_at(i);
        for (Hex n : neighbors(h)) {
            const int k = map.index_of(n);
            if (k < 0 || dist[static_cast<std::size_t>(k)] >= 0 || !map.walkable(n) || !can_pass(n)) continue;
            dist[static_cast<std::size_t>(k)] = d + 1;
            queue.push_back(k);
        }
    }
    std::vector<std::pair<Hex, int>> out;
    out.reserve(queue.size());
    for (int i = 0; i < map.cell_count(); ++i) {  // index order is Hex order
        if (dist[static_cast<std::size_t>(i)] >= 0) out.emplace_back(map.hex_at(i), dist[static_cast<std::size_t>(i)]);
    }
    return out;
}

std::vector<Hex> reachable(const HexMap& map, Hex from, int steps, const std::function<bool(Hex)>& can_pass,
                           const std::function<bool(Hex)>& can_stop) {
    std::vector<Hex> out;
    for (const auto& [h, d] : path_distances(map, from, can_pass, steps)) {
        if (h == from || can_stop(h)) out.push_back(h);
    }
    return out;  // already sorted: std::map order
}

}  // namespace rpg::combat
