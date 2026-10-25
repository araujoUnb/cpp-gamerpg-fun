#include "dice/d20.hpp"

#include <algorithm>
#include <stdexcept>

namespace rpg::dice {

namespace {

void require_d20_face(int value) {
    if (value < 1 || value > 20) {
        throw std::invalid_argument("d20 face must be in [1, 20]");
    }
}

}  // namespace

RollMode combine_mode(bool advantage, bool disadvantage) {
    if (advantage == disadvantage) {
        return RollMode::Normal;
    }
    return advantage ? RollMode::Advantage : RollMode::Disadvantage;
}

D20Roll make_d20(RollMode mode, int first, int second) {
    require_d20_face(first);
    D20Roll roll;
    roll.mode = mode;
    roll.first = first;
    switch (mode) {
        case RollMode::Normal:
            roll.kept = first;
            break;
        case RollMode::Advantage:
            require_d20_face(second);
            roll.second = second;
            roll.kept = std::max(first, second);
            break;
        case RollMode::Disadvantage:
            require_d20_face(second);
            roll.second = second;
            roll.kept = std::min(first, second);
            break;
    }
    return roll;
}

D20Roll roll_d20(Rng& rng, RollMode mode) {
    const int first = rng.roll(20);
    const int second = mode == RollMode::Normal ? 0 : rng.roll(20);
    return make_d20(mode, first, second);
}

CheckResult resolve_check(const D20Roll& roll, int modifier, int target) {
    CheckResult r;
    r.roll = roll;
    r.modifier = modifier;
    r.total = roll.kept + modifier;
    r.target = target;
    r.critical = roll.natural_20();
    r.fumble = roll.natural_1();
    if (r.critical) {
        r.success = true;
    } else if (r.fumble) {
        r.success = false;
    } else {
        r.success = r.total >= target;
    }
    return r;
}

CheckOdds check_odds(int modifier, int target, RollMode mode) {
    // Success only grows with the face: 20 always, 1 never, otherwise face + modifier >= target.
    const int faces = 1 + std::clamp(20 - std::max(target - modifier, 2), 0, 18);
    const int fail = 20 - faces;
    switch (mode) {
        case RollMode::Normal: return {faces * 20, 20};
        case RollMode::Advantage: return {400 - fail * fail, 400 - 19 * 19};
        case RollMode::Disadvantage: return {faces * faces, 1};
    }
    return {};
}

CheckResult roll_check(Rng& rng, int modifier, int target, RollMode mode) {
    return resolve_check(roll_d20(rng, mode), modifier, target);
}

}  // namespace rpg::dice
