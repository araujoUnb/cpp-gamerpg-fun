#pragma once

#include "combat/combat.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

namespace rpg::narrator {

// A situation is what the model reads: scene, party, enemies, what just
// happened and which actions it may ask for. Keys are in Portuguese because
// the model reads them. It only holds what the player can see on the table
// (honest master): no hands, no chosen cards, no future rolls.

// Which text describes the event: its kind id plus a variant, such as
// "attack_rolled.critical", "healed.self" or "condition_applied.beneficial".
std::string event_key(const combat::Combat& c, const combat::Event& e);

// Replaces {ator}, {alvo}, {carta}, {condicao}, {valor}, {valor2}, {total} and {ca}.
std::string fill_event(const combat::Combat& c, const combat::Event& e, std::string text);

// One line about the event, from the "narrator.event.<kind>[.<variant>]" text.
// Empty when the language file has no text for it, which is how events are left out.
std::string describe_event(const combat::Combat& c, const combat::Event& e);

// Before the first round.
nlohmann::json opening_situation(const combat::Combat& c);

// The events in [first_event, end_event), normally one round. Becomes the
// closing situation ("fim") when they include the victory or defeat.
nlohmann::json round_situation(const combat::Combat& c, std::size_t first_event, std::size_t end_event);

}  // namespace rpg::narrator
