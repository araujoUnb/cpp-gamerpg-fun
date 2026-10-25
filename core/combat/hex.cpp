#include "combat/hex.hpp"

#include <algorithm>
#include <cmath>
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
