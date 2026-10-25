#pragma once

#include "dice/rng.hpp"

namespace rpg::dice {

enum class RollMode { Normal, Advantage, Disadvantage };

// Advantage and disadvantage cancel each other out, regardless of how many
// sources of each there are.
RollMode combine_mode(bool advantage, bool disadvantage);

struct D20Roll {
    RollMode mode = RollMode::Normal;
    int first = 0;   // first die
    int second = 0;  // second die, 0 when mode is Normal
    int kept = 0;    // the die that counts

    bool natural_20() const { return kept == 20; }
    bool natural_1() const { return kept == 1; }
};

D20Roll roll_d20(Rng& rng, RollMode mode = RollMode::Normal);

// Builds a D20Roll from known dice, applying the mode. Useful for replays and tests.
D20Roll make_d20(RollMode mode, int first, int second = 0);

struct CheckResult {
    D20Roll roll;
    int modifier = 0;  // attribute modifier + proficiency, already summed
    int total = 0;
    int target = 0;    // DC or armor class
    bool success = false;
    bool critical = false;  // natural 20: automatic success, doubles damage dice
    bool fumble = false;    // natural 1: automatic failure
};

// Pure rule: success if total >= target, natural 20 always succeeds, natural 1 always fails.
CheckResult resolve_check(const D20Roll& roll, int modifier, int target);

CheckResult roll_check(Rng& rng, int modifier, int target, RollMode mode = RollMode::Normal);

// Exact odds of a check, counted out of 400 equally likely outcomes (two d20s,
// so advantage and disadvantage are exact too). Integers keep every decision
// built on them identical across platforms.
struct CheckOdds {
    static constexpr int kTotal = 400;
    int success = 0;   // outcomes that succeed, criticals included
    int critical = 0;  // outcomes with a natural 20
};
CheckOdds check_odds(int modifier, int target, RollMode mode = RollMode::Normal);

}  // namespace rpg::dice
