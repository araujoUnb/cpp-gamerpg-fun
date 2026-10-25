#pragma once

#include "character/character_sheet.hpp"
#include "data/content.hpp"

#include <string>

namespace rpg::data {

// A ready-to-play level-1 hero: the class's suggested attributes, its first
// skill options and the first hand_size level-1 cards as the deck.
// For quick starts, tests and tools; the campaign lets the player choose.
character::CharacterSheet default_hero(const Content& content, const std::string& id, const std::string& name,
                                       const std::string& class_id);

}  // namespace rpg::data
