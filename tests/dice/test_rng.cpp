#include "dice/rng.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <numeric>
#include <stdexcept>
#include <vector>

using rpg::dice::Rng;

TEST_CASE("Rng matches the PCG32 reference output") {
    // Values from the reference pcg32-demo with seed 42, stream 54.
    Rng rng(42, 54);
    const std::array<std::uint32_t, 6> expected{
        0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (auto e : expected) {
        CHECK(rng.next_u32() == e);
    }
}

TEST_CASE("Same seed gives the same sequence") {
    Rng a(1234);
    Rng b(1234);
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(a.roll(20) == b.roll(20));
    }
}

TEST_CASE("Different seeds give different sequences") {
    Rng a(1);
    Rng b(2);
    int differences = 0;
    for (int i = 0; i < 100; ++i) {
        if (a.next_u32() != b.next_u32()) ++differences;
    }
    CHECK(differences > 90);
}

TEST_CASE("State can be saved and restored for replays") {
    Rng rng(99);
    for (int i = 0; i < 17; ++i) rng.next_u32();

    const auto saved = rng.state();
    std::vector<int> original;
    for (int i = 0; i < 50; ++i) original.push_back(rng.roll(20));

    auto restored = Rng::from_state(saved);
    std::vector<int> replay;
    for (int i = 0; i < 50; ++i) replay.push_back(restored.roll(20));

    CHECK(original == replay);
    CHECK(restored.state() == rng.state());
}

TEST_CASE("from_state rejects an invalid increment") {
    CHECK_THROWS_AS(Rng::from_state({123, 2}), std::invalid_argument);
}

TEST_CASE("uniform stays inside its inclusive range and hits every value") {
    Rng rng(7);
    std::array<int, 6> counts{};
    for (int i = 0; i < 6000; ++i) {
        const int v = rng.uniform(1, 6);
        REQUIRE(v >= 1);
        REQUIRE(v <= 6);
        ++counts[static_cast<std::size_t>(v - 1)];
    }
    for (int c : counts) {
        CHECK(c > 800);
        CHECK(c < 1200);
    }

    CHECK(rng.uniform(5, 5) == 5);
    const int neg = rng.uniform(-3, -1);
    CHECK(neg >= -3);
    CHECK(neg <= -1);
}

TEST_CASE("d20 is roughly uniform") {
    Rng rng(2024);
    std::array<int, 20> counts{};
    constexpr int kRolls = 200000;
    for (int i = 0; i < kRolls; ++i) {
        ++counts[static_cast<std::size_t>(rng.roll(20) - 1)];
    }
    for (int c : counts) {
        CHECK(c > kRolls / 20 * 95 / 100);
        CHECK(c < kRolls / 20 * 105 / 100);
    }
}

TEST_CASE("Invalid ranges throw") {
    Rng rng(1);
    CHECK_THROWS_AS(rng.uniform(3, 2), std::invalid_argument);
    CHECK_THROWS_AS(rng.below(0), std::invalid_argument);
}

TEST_CASE("shuffle is a deterministic permutation") {
    std::vector<int> a(12);
    std::iota(a.begin(), a.end(), 0);
    auto b = a;

    Rng ra(5);
    Rng rb(5);
    ra.shuffle(std::span<int>(a));
    rb.shuffle(std::span<int>(b));
    CHECK(a == b);

    auto sorted = a;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> identity(12);
    std::iota(identity.begin(), identity.end(), 0);
    CHECK(sorted == identity);
    CHECK(a != identity);
}
