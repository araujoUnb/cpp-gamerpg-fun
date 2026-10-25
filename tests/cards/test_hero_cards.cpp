#include "cards/deck.hpp"
#include "cards/hero_cards.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <stdexcept>

using namespace rpg::cards;
using rpg::character::CardRules;
using rpg::dice::Rng;

namespace {

ActionDef attack(int v) {
    ActionDef a;
    a.type = ActionType::Attack;
    a.value = v;
    a.range = 1;
    a.target = TargetKind::Enemy;
    return a;
}

ActionDef move(int v) {
    ActionDef a;
    a.type = ActionType::Move;
    a.value = v;
    return a;
}

// Card "t.N" with initiative 10 * N. Cards 1 and 2 lose on the top half,
// card 3 loses on the bottom half.
CardRegistry registry(int count = 6) {
    CardRegistry r;
    for (int i = 1; i <= count; ++i) {
        CardDef c;
        c.id = "t." + std::to_string(i);
        c.class_id = "t";
        c.level = i <= 5 ? 1 : 2;
        c.initiative = 10 * i;
        c.top = {{attack(i + 1)}, i == 1 || i == 2};
        c.bottom = {{move(i)}, i == 3};
        r.add(c);
    }
    return r;
}

std::vector<std::string> deck(int count) {
    std::vector<std::string> d;
    for (int i = 1; i <= count; ++i) d.push_back("t." + std::to_string(i));
    return d;
}

std::size_t total(const HeroCards& h) {
    std::size_t n = h.hand().size() + h.discard().size() + h.lost().size();
    if (h.selection() && !h.selection()->long_rest) n += 2;
    return n;
}

bool has(const std::vector<std::string>& pile, const std::string& id) {
    return std::find(pile.begin(), pile.end(), id) != pile.end();
}

}  // namespace

TEST_CASE("Deck validation") {
    const auto reg = registry();
    rpg::character::ClassDef cls;
    cls.id = "t";
    cls.hand_size = 4;

    CHECK_NOTHROW(validate_deck(reg, cls, 1, deck(4)));
    CHECK_THROWS_AS(validate_deck(reg, cls, 1, deck(3)), std::invalid_argument);
    CHECK_THROWS_AS(validate_deck(reg, cls, 1, {"t.1", "t.1", "t.2", "t.3"}), std::invalid_argument);
    CHECK_THROWS_AS(validate_deck(reg, cls, 1, {"t.1", "t.2", "t.3", "t.6"}), std::invalid_argument);  // level 2
    CHECK_NOTHROW(validate_deck(reg, cls, 2, {"t.1", "t.2", "t.3", "t.6"}));
    CHECK_THROWS_AS(validate_deck(reg, cls, 1, {"t.1", "t.2", "t.3", "x.9"}), std::invalid_argument);

    auto other = cls;
    other.id = "u";
    CHECK_THROWS_AS(validate_deck(reg, other, 1, deck(4)), std::invalid_argument);

    CHECK(reg.available("t", 1).size() == 5);
    CHECK(reg.available("t", 2).size() == 6);
    CHECK(reg.available("u", 9).empty());
}

TEST_CASE("A new hero has the whole deck in hand") {
    const auto h = HeroCards::from_deck(deck(5));
    CHECK(h.hand().size() == 5);
    CHECK(h.discard().empty());
    CHECK(h.lost().empty());
    CHECK_FALSE(h.selection().has_value());
    CHECK(h.status(CardRules{}) == RoundStatus::CanPlay);
    CHECK_THROWS_AS(HeroCards::from_deck({"t.1", "t.1"}), std::invalid_argument);
}

TEST_CASE("Selecting two cards takes them from hand and sets initiative from the lead card") {
    const auto reg = registry();
    const CardRules rules;
    auto h = HeroCards::from_deck(deck(5));

    const auto options = h.legal_selections(rules);
    CHECK(options.size() == 5 * 4);  // ordered pairs; no rest with an empty discard pile

    h.select(RoundSelection::play("t.4", "t.2"), rules);
    CHECK(h.hand().size() == 3);
    CHECK_FALSE(has(h.hand(), "t.4"));
    CHECK(h.initiative(reg, rules) == 40);
    CHECK(h.legal_selections(rules).empty());
    CHECK_THROWS_AS(h.select(RoundSelection::play("t.1", "t.3"), rules), std::logic_error);
}

TEST_CASE("Illegal selections are rejected and change nothing") {
    const CardRules rules;
    auto h = HeroCards::from_deck(deck(5));
    CHECK_THROWS_AS(h.select(RoundSelection::play("t.1", "t.1"), rules), std::invalid_argument);
    CHECK_THROWS_AS(h.select(RoundSelection::play("t.1", "t.9"), rules), std::invalid_argument);
    CHECK_THROWS_AS(h.select(RoundSelection::rest(), rules), std::invalid_argument);  // empty discard
    CHECK(h.hand().size() == 5);
    CHECK_FALSE(h.selection().has_value());
}

TEST_CASE("Each turn uses the top of one card and the bottom of the other") {
    const auto reg = registry();
    const CardRules rules;
    auto h = HeroCards::from_deck(deck(5));
    h.select(RoundSelection::play("t.4", "t.5"), rules);

    const auto plays = h.legal_plays();
    CHECK(plays.size() == 16);
    for (const auto& p : plays) CHECK(p.top.card != p.bottom.card);

    const TurnPlay play{{"t.5", false}, {"t.4", false}, true};
    const auto steps = h.plan(play, reg, rules);
    REQUIRE(steps.size() == 2);
    CHECK(steps[0].card == "t.5");
    CHECK(steps[0].half == Half::Top);
    CHECK(steps[0].actions == std::vector{attack(6)});
    CHECK(steps[1].half == Half::Bottom);
    CHECK(steps[1].actions == std::vector{move(4)});

    const auto reversed = h.plan({{"t.5", false}, {"t.4", false}, false}, reg, rules);
    CHECK(reversed[0].half == Half::Bottom);

    CHECK_THROWS_AS(h.plan({{"t.5", false}, {"t.5", false}, true}, reg, rules), std::invalid_argument);
    CHECK_THROWS_AS(h.plan({{"t.1", false}, {"t.4", false}, true}, reg, rules), std::invalid_argument);
}

TEST_CASE("Basic actions replace a half with attack 2 or move 2") {
    const auto reg = registry();
    const CardRules rules;
    auto h = HeroCards::from_deck(deck(5));
    h.select(RoundSelection::play("t.1", "t.3"), rules);

    const auto steps = h.plan({{"t.1", true}, {"t.3", true}, true}, reg, rules);
    CHECK(steps[0].basic);
    CHECK(steps[0].actions == std::vector{attack(2)});
    CHECK(steps[1].actions == std::vector{move(2)});
}

TEST_CASE("Cards go to discard, or to lost when the half used is a loss half") {
    const auto reg = registry();
    const CardRules rules;

    SUBCASE("loss half used") {
        auto h = HeroCards::from_deck(deck(5));
        h.select(RoundSelection::play("t.1", "t.4"), rules);
        h.finish_turn({{"t.1", false}, {"t.4", false}, true}, reg);
        CHECK(h.lost() == std::vector<std::string>{"t.1"});
        CHECK(h.discard() == std::vector<std::string>{"t.4"});
        CHECK_FALSE(h.selection().has_value());
    }
    SUBCASE("loss half replaced by a basic action is not lost") {
        auto h = HeroCards::from_deck(deck(5));
        h.select(RoundSelection::play("t.1", "t.4"), rules);
        h.finish_turn({{"t.1", true}, {"t.4", false}, true}, reg);
        CHECK(h.lost().empty());
        CHECK(h.discard().size() == 2);
    }
    SUBCASE("the other half of a card does not count") {
        auto h = HeroCards::from_deck(deck(5));
        h.select(RoundSelection::play("t.1", "t.4"), rules);
        h.finish_turn({{"t.4", false}, {"t.1", false}, true}, reg);  // t.1 bottom is not a loss
        CHECK(h.lost().empty());
    }
    SUBCASE("bottom loss") {
        auto h = HeroCards::from_deck(deck(5));
        h.select(RoundSelection::play("t.3", "t.4"), rules);
        h.finish_turn({{"t.4", false}, {"t.3", false}, true}, reg);
        CHECK(h.lost() == std::vector<std::string>{"t.3"});
    }
}

TEST_CASE("Short rest loses one random discarded card and recovers the rest") {
    const auto reg = registry();
    const CardRules rules;
    auto h = HeroCards::from_deck(deck(5));
    CHECK_FALSE(h.can_short_rest(rules));

    h.select(RoundSelection::play("t.4", "t.5"), rules);
    CHECK_FALSE(h.can_short_rest(rules));  // only at the end of the round
    h.finish_turn({{"t.4", false}, {"t.5", false}, true}, reg);
    REQUIRE(h.can_short_rest(rules));

    Rng rng(1);
    const auto lost = h.short_rest(rng, rules);
    CHECK((lost == "t.4" || lost == "t.5"));
    CHECK(h.lost() == std::vector<std::string>{lost});
    CHECK(h.discard().empty());
    CHECK(h.hand().size() == 4);
}

TEST_CASE("Short rest is deterministic for a seed and picks every card over many seeds") {
    const auto reg = registry();
    const CardRules rules;
    auto make = [&] {
        auto h = HeroCards::from_deck(deck(5));
        h.select(RoundSelection::play("t.4", "t.5"), rules);
        h.finish_turn({{"t.4", false}, {"t.5", false}, true}, reg);
        return h;
    };
    std::set<std::string> seen;
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        auto a = make();
        auto b = make();
        Rng ra(seed);
        Rng rb(seed);
        const auto la = a.short_rest(ra, rules);
        REQUIRE(la == b.short_rest(rb, rules));
        seen.insert(la);
    }
    CHECK(seen.size() == 2);
}

TEST_CASE("Long rest: initiative 99, the player chooses the card to lose") {
    const auto reg = registry();
    const CardRules rules;
    auto h = HeroCards::from_deck(deck(5));
    h.select(RoundSelection::play("t.4", "t.5"), rules);
    h.finish_turn({{"t.4", false}, {"t.5", false}, true}, reg);

    const auto options = h.legal_selections(rules);
    CHECK(std::count(options.begin(), options.end(), RoundSelection::rest()) == 1);

    h.select(RoundSelection::rest(), rules);
    CHECK(h.initiative(reg, rules) == 99);
    CHECK(h.legal_plays().empty());
    CHECK(h.legal_long_rest_losses() == std::vector<std::string>{"t.4", "t.5"});
    CHECK_THROWS_AS(h.finish_turn({{"t.4", false}, {"t.5", false}, true}, reg), std::logic_error);
    CHECK_THROWS_AS(h.long_rest("t.1", rules), std::invalid_argument);

    h.long_rest("t.5", rules);
    CHECK(h.lost() == std::vector<std::string>{"t.5"});
    CHECK(h.hand().size() == 4);
    CHECK(h.discard().empty());
    CHECK_FALSE(h.selection().has_value());
}

TEST_CASE("Long rest without choosing it is a phase error") {
    auto h = HeroCards::from_deck(deck(5));
    CHECK_THROWS_AS(h.long_rest("t.1", CardRules{}), std::logic_error);
}

TEST_CASE("Status: must rest with one card in hand, exhausted with nothing to rest") {
    const CardRules rules;
    const auto d = deck(5);
    CHECK(HeroCards::from_piles(d, {"t.1"}, {"t.2", "t.3"}, {"t.4", "t.5"}, std::nullopt).status(rules) ==
          RoundStatus::MustRest);
    const auto exhausted = HeroCards::from_piles(d, {"t.1"}, {"t.2"}, {"t.3", "t.4", "t.5"}, std::nullopt);
    CHECK(exhausted.status(rules) == RoundStatus::Exhausted);
    CHECK(exhausted.is_exhausted(rules));
    CHECK(exhausted.legal_selections(rules).empty());
}

TEST_CASE("from_piles rejects piles that do not match the deck") {
    const auto d = deck(4);
    CHECK_NOTHROW(HeroCards::from_piles(d, {"t.1", "t.2"}, {"t.3"}, {"t.4"}, std::nullopt));
    CHECK_NOTHROW(HeroCards::from_piles(d, {"t.1"}, {"t.2"}, {}, RoundSelection::play("t.3", "t.4")));
    CHECK_THROWS_AS(HeroCards::from_piles(d, {"t.1", "t.2"}, {"t.3"}, {}, std::nullopt), std::invalid_argument);
    CHECK_THROWS_AS(HeroCards::from_piles(d, {"t.1", "t.2"}, {"t.3", "t.3"}, {"t.4"}, std::nullopt),
                    std::invalid_argument);
    CHECK_THROWS_AS(HeroCards::from_piles(d, {"t.1", "t.2"}, {"t.3"}, {"t.9"}, std::nullopt), std::invalid_argument);
    CHECK_THROWS_AS(HeroCards::from_piles(d, {"t.1", "t.2"}, {}, {}, RoundSelection::play("t.3", "t.3")),
                    std::invalid_argument);
}

TEST_CASE("Random legal play always conserves cards and ends in exhaustion") {
    // Plays whole scenarios choosing uniformly among the legal options, the
    // way an AI or a fuzzer would, and checks the invariants every step.
    const auto reg = registry(6);
    const CardRules rules;
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        CAPTURE(seed);
        Rng rng(seed);
        auto h = HeroCards::from_deck(deck(6));
        std::size_t lost_before = 0;
        int rounds = 0;
        while (!h.is_exhausted(rules)) {
            REQUIRE(++rounds < 200);
            const auto options = h.legal_selections(rules);
            REQUIRE_FALSE(options.empty());
            const auto& choice = options[rng.below(static_cast<std::uint32_t>(options.size()))];
            h.select(choice, rules);
            if (choice.long_rest) {
                const auto losses = h.legal_long_rest_losses();
                h.long_rest(losses[rng.below(static_cast<std::uint32_t>(losses.size()))], rules);
            } else {
                const auto plays = h.legal_plays();
                const auto& play = plays[rng.below(static_cast<std::uint32_t>(plays.size()))];
                REQUIRE(h.plan(play, reg, rules).size() == 2);
                h.finish_turn(play, reg);
                if (h.can_short_rest(rules) && rng.below(2) == 0) h.short_rest(rng, rules);
            }
            REQUIRE(total(h) == 6);
            REQUIRE(h.lost().size() >= lost_before);
            lost_before = h.lost().size();
            REQUIRE_NOTHROW(HeroCards::from_piles(deck(6), h.hand(), h.discard(), h.lost(), h.selection()));
        }
    }
}
