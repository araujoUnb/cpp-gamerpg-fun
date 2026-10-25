#include "cards/hero_cards.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace rpg::cards {

namespace {

bool contains(const std::vector<std::string>& pile, const std::string& id) {
    return std::find(pile.begin(), pile.end(), id) != pile.end();
}

void erase_one(std::vector<std::string>& pile, const std::string& id) {
    pile.erase(std::find(pile.begin(), pile.end(), id));
}

void move_all(std::vector<std::string>& from, std::vector<std::string>& to) {
    to.insert(to.end(), from.begin(), from.end());
    from.clear();
}

ActionDef basic_action(Half half, const character::CardRules& rules) {
    ActionDef a;
    if (half == Half::Top) {
        a.type = ActionType::Attack;
        a.value = rules.basic_attack;
        a.range = 1;
        a.target = TargetKind::Enemy;
    } else {
        a.type = ActionType::Move;
        a.value = rules.basic_move;
    }
    return a;
}

}  // namespace

HeroCards HeroCards::from_deck(std::vector<std::string> deck) {
    const std::set<std::string> unique(deck.begin(), deck.end());
    if (unique.size() != deck.size()) throw std::invalid_argument("deck has duplicate cards");
    HeroCards h;
    h.hand_ = std::move(deck);
    return h;
}

HeroCards HeroCards::from_piles(const std::vector<std::string>& deck, std::vector<std::string> hand,
                                std::vector<std::string> discard, std::vector<std::string> lost,
                                std::optional<RoundSelection> selection) {
    std::multiset<std::string> held;
    held.insert(hand.begin(), hand.end());
    held.insert(discard.begin(), discard.end());
    held.insert(lost.begin(), lost.end());
    if (selection && !selection->long_rest) {
        if (selection->lead == selection->other) throw std::invalid_argument("selection uses the same card twice");
        held.insert(selection->lead);
        held.insert(selection->other);
    }
    const std::multiset<std::string> expected(deck.begin(), deck.end());
    if (held != expected) throw std::invalid_argument("card piles do not match the deck");

    auto h = from_deck(deck);
    h.hand_ = std::move(hand);
    h.discard_ = std::move(discard);
    h.lost_ = std::move(lost);
    h.selection_ = std::move(selection);
    return h;
}

RoundStatus HeroCards::status(const character::CardRules& rules) const {
    if (hand_.size() >= 2) return RoundStatus::CanPlay;
    if (static_cast<int>(discard_.size()) >= rules.rest_min_discard) return RoundStatus::MustRest;
    return RoundStatus::Exhausted;
}

std::vector<RoundSelection> HeroCards::legal_selections(const character::CardRules& rules) const {
    std::vector<RoundSelection> out;
    if (selection_) return out;
    for (const auto& a : hand_) {
        for (const auto& b : hand_) {
            if (a != b) out.push_back(RoundSelection::play(a, b));
        }
    }
    if (static_cast<int>(discard_.size()) >= rules.rest_min_discard) out.push_back(RoundSelection::rest());
    return out;
}

void HeroCards::select(const RoundSelection& choice, const character::CardRules& rules) {
    if (selection_) throw std::logic_error("cards already selected this round");
    if (choice.long_rest) {
        if (static_cast<int>(discard_.size()) < rules.rest_min_discard) {
            throw std::invalid_argument("not enough discarded cards to rest");
        }
        selection_ = RoundSelection::rest();
        return;
    }
    if (choice.lead == choice.other) throw std::invalid_argument("must select two different cards");
    if (!contains(hand_, choice.lead) || !contains(hand_, choice.other)) {
        throw std::invalid_argument("selected cards must be in hand");
    }
    erase_one(hand_, choice.lead);
    erase_one(hand_, choice.other);
    selection_ = choice;
}

int HeroCards::initiative(const CardRegistry& registry, const character::CardRules& rules) const {
    if (!selection_) throw std::logic_error("no selection this round");
    if (selection_->long_rest) return rules.long_rest_initiative;
    return registry.get(selection_->lead).initiative;
}

std::vector<TurnPlay> HeroCards::legal_plays() const {
    std::vector<TurnPlay> out;
    if (!selection_ || selection_->long_rest) return out;
    const auto& s = *selection_;
    for (const auto& [top_card, bottom_card] : {std::pair{s.lead, s.other}, std::pair{s.other, s.lead}}) {
        for (bool top_basic : {false, true}) {
            for (bool bottom_basic : {false, true}) {
                for (bool top_first : {true, false}) {
                    out.push_back({{top_card, top_basic}, {bottom_card, bottom_basic}, top_first});
                }
            }
        }
    }
    return out;
}

void HeroCards::require_turn(const TurnPlay& play) const {
    if (!selection_) throw std::logic_error("no selection this round");
    if (selection_->long_rest) throw std::logic_error("the hero is resting this round");
    const auto& s = *selection_;
    const bool same = (play.top.card == s.lead && play.bottom.card == s.other) ||
                      (play.top.card == s.other && play.bottom.card == s.lead);
    if (!same) throw std::invalid_argument("the play must use the two selected cards, one half each");
}

std::vector<PlannedStep> HeroCards::plan(const TurnPlay& play, const CardRegistry& registry,
                                         const character::CardRules& rules) const {
    require_turn(play);
    auto step = [&](const HalfUse& use, Half half) {
        PlannedStep st;
        st.card = use.card;
        st.half = half;
        st.basic = use.basic;
        st.actions = use.basic ? std::vector{basic_action(half, rules)} : registry.get(use.card).half(half).actions;
        return st;
    };
    auto top = step(play.top, Half::Top);
    auto bottom = step(play.bottom, Half::Bottom);
    if (play.top_first) return {std::move(top), std::move(bottom)};
    return {std::move(bottom), std::move(top)};
}

void HeroCards::finish_turn(const TurnPlay& play, const CardRegistry& registry) {
    require_turn(play);
    auto loses = [&](const HalfUse& use, Half half) { return !use.basic && registry.get(use.card).half(half).lose; };
    const bool top_lost = loses(play.top, Half::Top);
    const bool bottom_lost = loses(play.bottom, Half::Bottom);
    (top_lost ? lost_ : discard_).push_back(play.top.card);
    (bottom_lost ? lost_ : discard_).push_back(play.bottom.card);
    selection_.reset();
}

std::vector<std::string> HeroCards::legal_long_rest_losses() const {
    if (!selection_ || !selection_->long_rest) return {};
    return discard_;
}

void HeroCards::long_rest(const std::string& card_to_lose, const character::CardRules& rules) {
    if (!selection_ || !selection_->long_rest) throw std::logic_error("the hero did not choose to rest");
    if (static_cast<int>(discard_.size()) < rules.rest_min_discard) {
        throw std::logic_error("not enough discarded cards to rest");
    }
    if (!contains(discard_, card_to_lose)) throw std::invalid_argument("the card to lose must be in the discard pile");
    erase_one(discard_, card_to_lose);
    lost_.push_back(card_to_lose);
    move_all(discard_, hand_);
    selection_.reset();
}

bool HeroCards::can_short_rest(const character::CardRules& rules) const {
    return !selection_ && static_cast<int>(discard_.size()) >= rules.rest_min_discard;
}

std::string HeroCards::short_rest(dice::Rng& rng, const character::CardRules& rules) {
    if (!can_short_rest(rules)) throw std::logic_error("cannot short rest now");
    const auto index = rng.below(static_cast<std::uint32_t>(discard_.size()));
    auto lost_card = discard_[index];
    discard_.erase(discard_.begin() + static_cast<std::ptrdiff_t>(index));
    lost_.push_back(lost_card);
    move_all(discard_, hand_);
    return lost_card;
}

}  // namespace rpg::cards
