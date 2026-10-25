#pragma once

#include "combat/combat.hpp"
#include "dice/rng.hpp"
#include "narrator/narration.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rpg::narrator {

// Phrases written ahead of time by the model (tools/narrator_bank.cpp on
// lab10), so the game narrates without one: instantly, offline, on weak
// hardware. Phrases use the same marks as the event texts ({ator}, {alvo}...).
struct PhraseBank {
    int events_per_round = 3;  // most striking events narrated each round
    std::vector<std::string> openings;  // with {cenario}
    std::vector<std::pair<std::string, std::vector<std::string>>> events;  // event_key -> variants

    const std::vector<std::string>* find(std::string_view key) const;
};

// Throws data::DataError when malformed.
PhraseBank bank_from_json(const nlohmann::json& j);
PhraseBank load_bank(const std::filesystem::path& file);
nlohmann::json to_json(const PhraseBank& bank);

// A phrase the bank accepts: has every mark in `marks` (such as "{alvo}"),
// no other "{...}", no digits, and a sensible length.
bool valid_phrase(std::string_view phrase, const std::vector<std::string>& marks);

// How striking an event is; 0 = never narrated from the bank.
int salience(const combat::Event& e);

// Whether the model's narration leaves the moment untold: none arrived, or its
// text came out empty (only lines or actions, or every sentence a repeat).
bool needs_cover(const std::optional<Narration>& model);

// The model's narration with the bank's covering what it missed: the bank's
// when the model gave none, the bank's text with the model's lines and actions
// when the model's text is empty. Unchanged when the bank has nothing.
std::optional<Narration> cover(std::optional<Narration> model, std::optional<Narration> bank);

// Narrates from a PhraseBank. Draws from its own Rng, so the battle and its
// replays do not change, and the same seed gives the same narration.
class BankNarrator {
public:
    // `bank` must outlive the narrator.
    BankNarrator(const PhraseBank& bank, std::uint64_t seed);

    std::optional<Narration> opening(const combat::Combat& c);
    // Events in [first_event, end_event); nullopt when none has phrases.
    std::optional<Narration> round(const combat::Combat& c, std::size_t first_event, std::size_t end_event);

private:
    // A variant of `key`, never the one used last time when there are others.
    const std::string& pick(const std::string& key, const std::vector<std::string>& variants);

    const PhraseBank* bank_;
    dice::Rng rng_;
    std::vector<std::pair<std::string, std::size_t>> last_;
};

}  // namespace rpg::narrator
