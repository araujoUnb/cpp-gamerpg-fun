#pragma once

#include "combat/combat.hpp"

#include <cstddef>

namespace rpg::combat {

// A simple heuristic player for the heroes. It plays reasonably, never
// cheats (it only reads public state), and is deterministic.
// Used for the terminal auto mode, balance tests and as a baseline for
// trained AIs.
class AutoPlayer {
public:
    // Returns an option index for the pending decision.
    std::size_t choose(const Combat& combat, const Decision& decision) const;
};

}  // namespace rpg::combat
