#include "combat/hex.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <stdexcept>

namespace rpg::combat {

HexMap::HexMap(std::set<Hex> hexes, std::set<Hex> obstacles) : hexes_(std::move(hexes)) {
    for (const auto& o : obstacles) {
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
    for (Hex h : hexes_) set_terrain(h, Terrain::Open);
    for (Hex h : obstacles) set_terrain(h, Terrain::Obstacle);
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

void HexMap::set_terrain(Hex h, Terrain t) {
    const int i = index_of(h);
    if (i < 0 || !hexes_.count(h)) throw std::invalid_argument("hex outside the map");
    cells_[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(t) + 1);
}

std::vector<Hex> HexMap::with_terrain(Terrain t) const {
    std::vector<Hex> out;
    for (Hex h : hexes_) {
        if (terrain(h) == t) out.push_back(h);
    }
    return out;
}

std::vector<std::pair<Hex, int>> path_distances(const HexMap& map, Hex from, const HexPredicate& can_pass,
                                                int max_cost) {
    const int start = map.index_of(from);
    if (start < 0) return {{from, 0}};
    // Dijkstra over flat arrays; costs are 1 or 2, so the heap stays small.
    std::vector<int> dist(static_cast<std::size_t>(map.cell_count()), -1);
    using Item = std::pair<int, int>;  // cost, index
    std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
    dist[static_cast<std::size_t>(start)] = 0;
    open.push({0, start});
    while (!open.empty()) {
        const auto [d, i] = open.top();
        open.pop();
        if (d != dist[static_cast<std::size_t>(i)]) continue;
        const Hex h = map.hex_at(i);
        for (Hex n : neighbors(h)) {
            const int k = map.index_of(n);
            if (k < 0 || !map.walkable(n)) continue;
            const int nd = d + map.move_cost(n);
            auto& slot = dist[static_cast<std::size_t>(k)];
            if (nd > max_cost || (slot >= 0 && slot <= nd) || !can_pass(n)) continue;
            slot = nd;
            open.push({nd, k});
        }
    }
    std::vector<std::pair<Hex, int>> out;
    for (int i = 0; i < map.cell_count(); ++i) {  // index order is Hex order
        if (dist[static_cast<std::size_t>(i)] >= 0) out.emplace_back(map.hex_at(i), dist[static_cast<std::size_t>(i)]);
    }
    return out;
}

std::vector<Hex> reachable(const HexMap& map, Hex from, int budget, const HexPredicate& can_pass,
                           const HexPredicate& can_stop) {
    std::vector<Hex> out;
    for (const auto& [h, d] : path_distances(map, from, can_pass, budget)) {
        if (h == from || can_stop(h)) out.push_back(h);
    }
    return out;
}

std::vector<Hex> find_path(const HexMap& map, Hex from, Hex to, const HexPredicate& can_pass,
                           const std::function<int(Hex)>& penalty) {
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
            const long step = map.move_cost(n) * kScale + (penalty ? std::min<long>(penalty(n), kScale - 1) : 0);
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

std::vector<Hex> hex_line(Hex a, Hex b, double nudge) {
    const int n = distance(a, b);
    std::vector<Hex> out;
    out.reserve(static_cast<std::size_t>(n) + 1);
    const double aq = a.q + nudge, ar = a.r + nudge, as = a.s() - 2 * nudge;
    const double bq = b.q + nudge, br = b.r + nudge, bs = b.s() - 2 * nudge;
    for (int i = 0; i <= n; ++i) {
        const double t = n == 0 ? 0.0 : static_cast<double>(i) / n;
        const double q = aq + (bq - aq) * t, r = ar + (br - ar) * t, s = as + (bs - as) * t;
        double rq = std::round(q), rr = std::round(r), rs = std::round(s);
        const double dq = std::abs(rq - q), dr = std::abs(rr - r), ds = std::abs(rs - s);
        if (dq > dr && dq > ds) rq = -rr - rs;
        else if (dr > ds) rr = -rq - rs;
        out.push_back({static_cast<int>(rq), static_cast<int>(rr)});
    }
    return out;
}

bool line_of_sight(const HexMap& map, Hex a, Hex b) {
    if (distance(a, b) <= 1) return true;
    // Same walk as hex_line, without allocating, stopping at the first blocker.
    const int n = distance(a, b);
    auto clear = [&](Hex from, Hex to, double nudge) {
        const double fq = from.q + nudge, fr = from.r + nudge, fs = from.s() - 2 * nudge;
        const double dq = to.q - from.q, dr = to.r - from.r, ds = to.s() - from.s();
        for (int i = 1; i < n; ++i) {
            const double t = static_cast<double>(i) / n;
            const double q = fq + dq * t, r = fr + dr * t, s = fs + ds * t;
            double rq = std::round(q), rr = std::round(r), rs = std::round(s);
            const double eq = std::abs(rq - q), er = std::abs(rr - r), es = std::abs(rs - s);
            if (eq > er && eq > es) rq = -rr - rs;
            else if (er > es) rr = -rq - rs;
            if (map.blocks_sight({static_cast<int>(rq), static_cast<int>(rr)})) return false;
        }
        return true;
    };
    // Both nudges in both directions, so sight is always symmetric.
    return clear(a, b, 1e-6) || clear(a, b, -1e-6) || clear(b, a, 1e-6) || clear(b, a, -1e-6);
}

}  // namespace rpg::combat
