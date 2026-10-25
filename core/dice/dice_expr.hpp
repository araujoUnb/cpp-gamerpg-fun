#pragma once

#include "dice/rng.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rpg::dice {

// A damage or effect expression such as "2d6+3", "d8", "1d4-1" or "5".
struct DiceExpr {
    int count = 0;
    int sides = 0;
    int bonus = 0;

    friend bool operator==(const DiceExpr&, const DiceExpr&) = default;
};

// Returns nullopt for malformed input. Used when loading content from JSON.
std::optional<DiceExpr> parse_dice(std::string_view text);

std::string to_string(const DiceExpr& expr);

struct DamageRoll {
    std::vector<int> dice;
    int bonus = 0;
    int total = 0;  // never below 0
};

// On a critical hit the number of dice is doubled; the flat bonus is not.
DamageRoll roll_damage(Rng& rng, const DiceExpr& expr, bool critical = false);

}  // namespace rpg::dice
