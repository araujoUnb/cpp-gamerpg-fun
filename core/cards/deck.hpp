#pragma once

#include "cards/card_registry.hpp"
#include "character/class_def.hpp"

#include <string>
#include <vector>

namespace rpg::cards {

// A deck is the hand the hero brings into a scenario: exactly hand_size
// distinct cards of the hero's class, each unlocked at or below the hero's level.
// Throws std::invalid_argument naming the problem.
void validate_deck(const CardRegistry& registry, const character::ClassDef& cls, int level,
                   const std::vector<std::string>& deck);

}  // namespace rpg::cards
