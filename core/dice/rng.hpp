#pragma once

#include <cstdint>
#include <span>
#include <utility>

namespace rpg::dice {

// Seeded PCG32 generator. Every random decision in the core goes through an
// instance of this class, injected by the caller.
//
// The STL distributions are deliberately not used: their output differs
// between standard library implementations, which would break replays and
// saved games across platforms.
class Rng {
public:
    struct State {
        std::uint64_t state = 0;
        std::uint64_t inc = 0;
        friend bool operator==(const State&, const State&) = default;
    };

    explicit Rng(std::uint64_t seed, std::uint64_t stream = 0);

    static Rng from_state(State s);
    State state() const { return {state_, inc_}; }

    std::uint32_t next_u32();

    // Uniform integer in [0, bound). bound must be > 0.
    std::uint32_t below(std::uint32_t bound);

    // Uniform integer in [lo, hi], inclusive. Requires lo <= hi.
    int uniform(int lo, int hi);

    // One die with the given number of sides: result in [1, sides].
    int roll(int sides) { return uniform(1, sides); }

    // Fisher-Yates shuffle, deterministic for a given state.
    template <typename T>
    void shuffle(std::span<T> items) {
        for (std::size_t i = items.size(); i > 1; --i) {
            const auto j = below(static_cast<std::uint32_t>(i));
            using std::swap;
            swap(items[i - 1], items[j]);
        }
    }

private:
    Rng() = default;

    std::uint64_t state_ = 0;
    std::uint64_t inc_ = 0;
};

}  // namespace rpg::dice
