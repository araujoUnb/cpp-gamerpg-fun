#include "dice/rng.hpp"

#include <stdexcept>

namespace rpg::dice {

Rng::Rng(std::uint64_t seed, std::uint64_t stream) {
    // Standard PCG32 seeding procedure.
    inc_ = (stream << 1u) | 1u;
    state_ = 0;
    next_u32();
    state_ += seed;
    next_u32();
}

Rng Rng::from_state(State s) {
    if ((s.inc & 1u) == 0) {
        throw std::invalid_argument("Rng::from_state: inc must be odd");
    }
    Rng rng;
    rng.state_ = s.state;
    rng.inc_ = s.inc;
    return rng;
}

std::uint32_t Rng::next_u32() {
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + inc_;
    const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<std::uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

std::uint32_t Rng::below(std::uint32_t bound) {
    if (bound == 0) {
        throw std::invalid_argument("Rng::below: bound must be > 0");
    }
    // Rejection sampling to avoid modulo bias.
    const std::uint32_t threshold = (0u - bound) % bound;
    for (;;) {
        const std::uint32_t r = next_u32();
        if (r >= threshold) {
            return r % bound;
        }
    }
}

int Rng::uniform(int lo, int hi) {
    if (lo > hi) {
        throw std::invalid_argument("Rng::uniform: lo must be <= hi");
    }
    const auto span = static_cast<std::uint64_t>(static_cast<std::int64_t>(hi) - lo) + 1;
    if (span > 0xFFFFFFFFull) {
        throw std::invalid_argument("Rng::uniform: range too large");
    }
    return static_cast<int>(lo + static_cast<std::int64_t>(below(static_cast<std::uint32_t>(span))));
}

}  // namespace rpg::dice
