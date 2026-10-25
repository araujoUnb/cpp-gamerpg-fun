#pragma once

#include "cards/card_def.hpp"
#include "cards/card_registry.hpp"
#include "character/ruleset.hpp"
#include "dice/rng.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rpg::cards {

// What a hero commits to at the start of a round.
struct RoundSelection {
    bool long_rest = false;
    std::string lead;   // its initiative is the hero's initiative this round
    std::string other;

    static RoundSelection play(std::string lead, std::string other) {
        return {false, std::move(lead), std::move(other)};
    }
    static RoundSelection rest() { return {true, {}, {}}; }

    friend bool operator==(const RoundSelection&, const RoundSelection&) = default;
};

struct HalfUse {
    std::string card;
    bool basic = false;  // use the basic action instead of the printed half

    friend bool operator==(const HalfUse&, const HalfUse&) = default;
};

// How the two selected cards are used on the hero's turn: the top half of one
// and the bottom half of the other, in either order.
struct TurnPlay {
    HalfUse top;
    HalfUse bottom;
    bool top_first = true;

    friend bool operator==(const TurnPlay&, const TurnPlay&) = default;
};

// One half of a turn, ready for the combat module to execute.
struct PlannedStep {
    std::string card;
    Half half = Half::Top;
    bool basic = false;
    std::vector<ActionDef> actions;
};

enum class RoundStatus {
    CanPlay,   // at least two cards in hand (resting is also allowed when possible)
    MustRest,  // fewer than two cards in hand, but enough in the discard pile to rest
    Exhausted  // cannot play two cards and cannot rest: the hero leaves the scenario
};

// The card piles of one hero during a scenario.
//
// Every operation either applies fully or throws (std::invalid_argument for an
// illegal choice, std::logic_error for a call in the wrong phase) and leaves
// the state unchanged. The legal_* functions list every valid choice, so a
// frontend or an AI can pick from them without guessing.
class HeroCards {
public:
    // All cards start in hand. The deck must have no duplicates.
    static HeroCards from_deck(std::vector<std::string> deck);

    // Rebuilds saved piles and checks that they hold exactly the deck's cards.
    static HeroCards from_piles(const std::vector<std::string>& deck, std::vector<std::string> hand,
                                std::vector<std::string> discard, std::vector<std::string> lost,
                                std::optional<RoundSelection> selection);

    const std::vector<std::string>& hand() const { return hand_; }
    const std::vector<std::string>& discard() const { return discard_; }
    const std::vector<std::string>& lost() const { return lost_; }
    const std::optional<RoundSelection>& selection() const { return selection_; }

    // --- Start of round -----------------------------------------------------
    RoundStatus status(const character::CardRules& rules) const;
    std::vector<RoundSelection> legal_selections(const character::CardRules& rules) const;
    void select(const RoundSelection& choice, const character::CardRules& rules);
    int initiative(const CardRegistry& registry, const character::CardRules& rules) const;

    // --- Hero's turn ----------------------------------------------------------
    std::vector<TurnPlay> legal_plays() const;
    std::vector<PlannedStep> plan(const TurnPlay& play, const CardRegistry& registry,
                                  const character::CardRules& rules) const;
    // Moves the two cards to the discard pile, or to the lost pile when the half used is a loss half.
    void finish_turn(const TurnPlay& play, const CardRegistry& registry);

    // Long rest, on the hero's turn after selecting RoundSelection::rest():
    // loses the chosen discarded card and returns the rest of the discard pile to hand.
    // The caller heals the hero by rules.long_rest_heal.
    std::vector<std::string> legal_long_rest_losses() const;
    void long_rest(const std::string& card_to_lose, const character::CardRules& rules);

    // Short rest, at the end of a round: loses a random discarded card and
    // returns the rest to hand. Returns the card lost.
    bool can_short_rest(const character::CardRules& rules) const;
    std::string short_rest(dice::Rng& rng, const character::CardRules& rules);

    // The hero loses this turn (stunned, or knocked out before acting):
    // selected cards go to the discard pile; a chosen rest is cancelled.
    void skip_turn();

    bool is_exhausted(const character::CardRules& rules) const { return status(rules) == RoundStatus::Exhausted; }

private:
    void require_turn(const TurnPlay& play) const;

    std::vector<std::string> hand_;
    std::vector<std::string> discard_;
    std::vector<std::string> lost_;
    std::optional<RoundSelection> selection_;
};

}  // namespace rpg::cards
