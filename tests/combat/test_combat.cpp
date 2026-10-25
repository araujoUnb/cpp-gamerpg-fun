#include "combat/auto_player.hpp"
#include "combat/combat.hpp"

#include "cards/deck.hpp"
#include "data/content.hpp"
#include "data/data_error.hpp"
#include "data/default_hero.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

using namespace rpg;
using namespace rpg::combat;
using cards::ActionDef;
using cards::ActionType;
using cards::TargetKind;
using character::Attribute;

namespace {

ActionDef attack(int v, int range = 1, std::vector<cards::ConditionApply> conds = {}) {
    ActionDef a;
    a.type = ActionType::Attack;
    a.value = v;
    a.range = range;
    a.target = TargetKind::Enemy;
    a.conditions = std::move(conds);
    return a;
}
ActionDef move(int v) {
    ActionDef a;
    a.type = ActionType::Move;
    a.value = v;
    return a;
}
ActionDef heal(int v, TargetKind target = TargetKind::Self, int range = 0) {
    ActionDef a;
    a.type = ActionType::Heal;
    a.value = v;
    a.target = target;
    a.range = range;
    return a;
}

cards::CardDef card(const std::string& id, int initiative, std::vector<ActionDef> top, std::vector<ActionDef> bottom,
                    bool top_lose = false) {
    cards::CardDef c;
    c.id = "tester." + id;
    c.class_id = "tester";
    c.initiative = initiative;
    c.name_key = c.id;
    c.top = {std::move(top), top_lose};
    c.bottom = {std::move(bottom), false};
    return c;
}

monsters::MonsterDef monster(const std::string& id, int ac, int hp, int atk, int dmg, monsters::MonsterCard c) {
    monsters::MonsterDef m;
    m.id = id;
    m.name_key = id;
    m.armor_class = ac;
    m.hit_points = hp;
    m.attack_bonus = atk;
    m.damage_bonus = dmg;
    m.deck = {std::move(c)};
    return m;
}

// A tiny, fully controlled content set.
data::Content test_content() {
    data::Content c;
    auto cond = [](const std::string& id) { return character::ConditionDef{id, id, "", {}}; };
    auto stunned = cond("stunned");
    stunned.effects.skip_turn = true;
    auto prot = cond("protected");
    prot.effects.damage_reduction = 1;
    auto poison = cond("poisoned");
    poison.effects.damage_taken_bonus = 1;
    c.rules.conditions = {stunned, prot, poison};

    character::ClassDef cls;
    cls.id = "tester";
    cls.primary = Attribute::Strength;
    cls.hand_size = 4;
    cls.hp_base = 10;
    cls.armor = {10, std::nullopt, std::nullopt};
    c.classes.add(cls);

    c.cards.add(card("hit", 10, {attack(3)}, {move(2)}));
    c.cards.add(card("stun", 20, {attack(1, 1, {{"stunned", 1}})}, {move(1)}));
    c.cards.add(card("heal", 30, {heal(3, TargetKind::Ally, 3)}, {move(1)}));
    c.cards.add(card("big", 40, {attack(5)}, {move(3)}, true));
    c.cards.add(card("shot", 15, {attack(2, 4)}, {move(2)}));

    // Never moves, never hurts: heals itself at full HP.
    c.monsters.add(monster("dummy", 1, 40, 0, 0, {"wait", "wait", 99, {heal(1)}, false}));
    // Strikes first, almost always hits, almost never gets hit.
    c.monsters.add(monster("biter", 30, 5, 30, 0, {"bite", "bite", 1, {attack(6)}, false}));
    // Walks toward the heroes, hits for nothing.
    c.monsters.add(monster("walker", 30, 5, 0, 0, {"walk", "walk", 50, {move(3), attack(1)}, false}));
    // Shoots from up to 3 hexes away, after moving 2.
    c.monsters.add(monster("archer", 30, 5, 0, 0, {"shoot", "shoot", 1, {move(2), attack(1, 3)}, false}));

    monsters::GameMasterDef gm;
    gm.id = "sharp";
    gm.name_key = "sharp";
    gm.tactical = true;
    gm.weights.expected_damage = 10;
    gm.weights.down_chance = 50;
    gm.weights.exposure_ranged = -50;
    gm.weights.approach = -20;
    gm.weights.steps = -1;
    c.game_masters.add(gm);
    return c;
}

character::CharacterSheet hero(const data::Content& c, const std::string& id) {
    character::AttributeScores a;
    a.set(Attribute::Strength, 16);  // +3
    auto s = character::create_character(c.rules, id, id, c.classes.get("tester"), a, {});
    s.deck = {"tester.hit", "tester.stun", "tester.heal", "tester.big"};
    return s;
}

character::CharacterSheet hero_with(const data::Content& c, const std::string& id, std::vector<std::string> deck) {
    auto s = hero(c, id);
    s.deck = std::move(deck);
    return s;
}

scenario::ScenarioDef arena(std::vector<scenario::MonsterSpawn> monsters, std::vector<Hex> starts = {{0, 0}}) {
    scenario::ScenarioDef s;
    s.id = "t";
    s.map = HexMap::hexagon(3);
    s.hero_starts = std::move(starts);
    s.monsters = std::move(monsters);
    return s;
}

// Picks the first option matching `pred`; fails the test if there is none.
std::size_t pick(const Decision& d, const std::function<bool(std::size_t)>& pred) {
    for (std::size_t i = 0; i < d.option_count(); ++i) {
        if (pred(i)) return i;
    }
    FAIL("no option matches");
    return 0;
}

void select(Combat& c, const std::string& lead, const std::string& other) {
    const auto* d = c.pending();
    REQUIRE(d);
    REQUIRE(d->kind == Decision::Kind::SelectCards);
    c.decide(pick(*d, [&](std::size_t i) {
        return !d->selections[i].long_rest && d->selections[i].lead == lead && d->selections[i].other == other;
    }));
}

void play(Combat& c, const std::string& top, const std::string& bottom) {
    const auto* d = c.pending();
    REQUIRE(d);
    REQUIRE(d->kind == Decision::Kind::PlayTurn);
    c.decide(pick(*d, [&](std::size_t i) {
        const auto& p = d->plays[i];
        return p.top.card == top && p.bottom.card == bottom && !p.top.basic && !p.bottom.basic && p.top_first;
    }));
}

std::vector<Event> of_kind(const Combat& c, Event::Kind k) {
    std::vector<Event> out;
    for (const auto& e : c.events()) {
        if (e.kind == k) out.push_back(e);
    }
    return out;
}

}  // namespace

TEST_CASE("Setup rejects invalid parties") {
    const auto content = test_content();
    const auto s = arena({{"dummy", {1, 0}}});
    CHECK_THROWS_AS(Combat(content, s, {}, 1), std::invalid_argument);
    CHECK_THROWS_AS(Combat(content, s, {hero(content, "a"), hero(content, "b")}, 1), std::invalid_argument);
    auto bad_deck = hero(content, "a");
    bad_deck.deck.pop_back();
    CHECK_THROWS_AS(Combat(content, s, {bad_deck}, 1), std::invalid_argument);
    CHECK_THROWS_AS(Combat(content, s, {hero(content, "x#1")}, 1), std::invalid_argument);
}

TEST_CASE("A hero attack uses d20 + attribute + proficiency and deals card value + attribute") {
    const auto content = test_content();
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        Combat c(content, arena({{"dummy", {1, 0}}}), {hero(content, "a")}, seed);
        select(c, "tester.hit", "tester.big");
        CHECK(c.initiative_of("a") == 10);
        CHECK(c.initiative_of("dummy#1") == 99);
        play(c, "tester.hit", "tester.big");
        // Target choice: the dummy or stop.
        const auto* d = c.pending();
        REQUIRE(d);
        REQUIRE(d->kind == Decision::Kind::Target);
        CHECK(d->targets == std::vector<std::string>{"dummy#1", ""});
        c.decide(0);

        const auto rolls = of_kind(c, Event::Kind::AttackRolled);
        REQUIRE(rolls.size() == 1);
        const auto& r = rolls[0];
        CHECK(r.actor == "a");
        CHECK(r.total == r.roll + 3 + 2);
        CHECK(r.target_number == 1);
        CHECK(r.flag == (r.roll != 1));
        const auto dmg = of_kind(c, Event::Kind::Damaged);
        if (r.flag) {
            REQUIRE(dmg.size() == 1);
            CHECK(dmg[0].value == (r.critical ? 6 : 3) + 3);
            CHECK(dmg[0].value2 == 40 - dmg[0].value);
        } else {
            CHECK(dmg.empty());
        }
    }
}

TEST_CASE("Stunned monsters lose their next turn") {
    const auto content = test_content();
    int checked = 0;
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        Combat c(content, arena({{"dummy", {1, 0}}}), {hero(content, "a")}, seed);
        select(c, "tester.stun", "tester.hit");
        play(c, "tester.stun", "tester.hit");
        c.decide(0);  // target the dummy
        REQUIRE(c.pending());
        REQUIRE(c.pending()->kind == Decision::Kind::Move);
        c.decide(pick(*c.pending(), [&](std::size_t i) { return c.pending()->hexes[i] == Hex{0, 0}; }));
        if (of_kind(c, Event::Kind::ConditionApplied).empty()) continue;  // missed
        ++checked;
        // Same round: the dummy (initiative 99) acts after the hero and is skipped.
        const auto skipped = of_kind(c, Event::Kind::TurnSkipped);
        REQUIRE(skipped.size() == 1);
        CHECK(skipped[0].actor == "dummy#1");
        const auto ended = of_kind(c, Event::Kind::ConditionEnded);
        REQUIRE(ended.size() == 1);
        CHECK(ended[0].detail == "stunned");
    }
    CHECK(checked > 20);
}

TEST_CASE("Played cards go to discard and loss halves to lost") {
    const auto content = test_content();
    Combat c(content, arena({{"dummy", {3, 0}}}), {hero(content, "a")}, 3);
    select(c, "tester.big", "tester.hit");
    play(c, "tester.big", "tester.hit");  // top of big is a loss half; nothing in range, so no target
    // The move from tester.hit's bottom (2 steps) is a decision.
    const auto* d = c.pending();
    REQUIRE(d);
    CHECK(d->kind == Decision::Kind::Move);
    CHECK(std::find(d->hexes.begin(), d->hexes.end(), Hex{0, 0}) != d->hexes.end());
    CHECK(std::find(d->hexes.begin(), d->hexes.end(), Hex{3, 0}) == d->hexes.end());  // occupied
    c.decide(pick(*d, [&](std::size_t i) { return d->hexes[i] == Hex{2, 0}; }));

    const auto& h = c.heroes()[0];
    CHECK(h.hex == Hex{2, 0});
    CHECK(h.cards.lost() == std::vector<std::string>{"tester.big"});
    CHECK(h.cards.discard() == std::vector<std::string>{"tester.hit"});
    CHECK(c.round() == 2);
    const auto moved = of_kind(c, Event::Kind::Moved);
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].from == Hex{0, 0});
    CHECK(moved[0].to == Hex{2, 0});
}

TEST_CASE("A lone hero knocked out means defeat") {
    const auto content = test_content();
    Combat c(content, arena({{"biter", {1, 0}}}), {hero(content, "a")}, 7);
    // 10 HP; the biter hits for 6 at initiative 1 every round.
    int guard = 0;
    while (!c.finished() && ++guard < 100) {
        const auto* d = c.pending();
        REQUIRE(d);
        c.decide(d->option_count() - 1);
    }
    REQUIRE(c.finished());
    CHECK(c.outcome() == Outcome::Defeat);
    CHECK(c.heroes()[0].status == HeroStatus::Dying);
    CHECK(c.heroes()[0].sheet.current_hp == 0);
    CHECK(of_kind(c, Event::Kind::HeroDowned).size() == 1);
    CHECK(c.pending() == nullptr);
    CHECK_THROWS_AS(c.decide(0), std::logic_error);
}

TEST_CASE("Healing an ally at 0 HP brings them back") {
    const auto content = test_content();
    int revived = 0;
    for (std::uint64_t seed = 0; seed < 20 && revived == 0; ++seed) {
        // The biter is next to "a" only; "b" stands two hexes away.
        Combat c(content, arena({{"biter", {1, 0}}}, {{0, 0}, {-2, 0}}), {hero(content, "a"), hero(content, "b")}, seed);
        int guard = 0;
        while (!c.finished() && ++guard < 200 && revived == 0) {
            const auto* d = c.pending();
            REQUIRE(d);
            if (d->kind == Decision::Kind::SelectCards && d->actor == "b") {
                const auto& h = c.heroes()[1];
                const bool has = std::find(h.cards.hand().begin(), h.cards.hand().end(), "tester.heal") != h.cards.hand().end();
                if (has && c.heroes()[0].status == HeroStatus::Dying) {
                    c.decide(pick(*d, [&](std::size_t i) {
                        return !d->selections[i].long_rest && d->selections[i].lead == "tester.heal";
                    }));
                    continue;
                }
            }
            if (d->kind == Decision::Kind::PlayTurn && d->actor == "b" && c.heroes()[0].status == HeroStatus::Dying) {
                const auto i = std::find_if(d->plays.begin(), d->plays.end(), [](const cards::TurnPlay& p) {
                    return p.top.card == "tester.heal" && !p.top.basic;
                });
                if (i != d->plays.end()) {
                    c.decide(static_cast<std::size_t>(i - d->plays.begin()));
                    continue;
                }
            }
            if (d->kind == Decision::Kind::Target && d->actor == "b" && d->action && d->action->type == ActionType::Heal) {
                c.decide(pick(*d, [&](std::size_t i) { return d->targets[i] == "a"; }));
                revived = static_cast<int>(of_kind(c, Event::Kind::HeroRevived).size());
                continue;
            }
            c.decide(d->option_count() - 1);
        }
        if (revived) {
            CHECK(c.heroes()[0].status == HeroStatus::Active);
            CHECK(c.heroes()[0].sheet.current_hp == 3);
        }
    }
    CHECK(revived == 1);
}

TEST_CASE("Invalid decisions are rejected") {
    const auto content = test_content();
    Combat c(content, arena({{"dummy", {1, 0}}}), {hero(content, "a")}, 1);
    REQUIRE(c.pending());
    CHECK_THROWS_AS(c.decide(c.pending()->option_count()), std::out_of_range);
    CHECK(c.pending() != nullptr);  // still waiting for the same decision
}

// --- Real content ---------------------------------------------------------------

namespace {

const std::filesystem::path kData = GAMERPG_DATA_DIR;

character::CharacterSheet real_hero(const data::Content& c, const std::string& id, const std::string& cls_id) {
    return data::default_hero(c, id, id, cls_id);
}

void check_invariants(const Combat& c) {
    std::set<Hex> taken;
    for (const auto& h : c.heroes()) {
        const auto max = character::max_hp(h.sheet, c.content().classes.get(h.sheet.class_id));
        REQUIRE(h.sheet.current_hp >= 0);
        REQUIRE(h.sheet.current_hp <= max);
        REQUIRE((h.status == HeroStatus::Active) == (h.sheet.current_hp > 0 || h.status == HeroStatus::Exhausted
                                                         ? h.status == HeroStatus::Active
                                                         : false));
        auto cards = h.cards.hand();
        cards.insert(cards.end(), h.cards.discard().begin(), h.cards.discard().end());
        cards.insert(cards.end(), h.cards.lost().begin(), h.cards.lost().end());
        if (h.cards.selection() && !h.cards.selection()->long_rest) {
            cards.push_back(h.cards.selection()->lead);
            cards.push_back(h.cards.selection()->other);
        }
        std::sort(cards.begin(), cards.end());
        auto deck = h.sheet.deck;
        std::sort(deck.begin(), deck.end());
        REQUIRE(cards == deck);
        if (h.on_board()) {
            REQUIRE(c.scenario().map.walkable(h.hex));
            REQUIRE(taken.insert(h.hex).second);
        }
    }
    for (const auto& m : c.monsters()) {
        REQUIRE(m.hp >= 0);
        REQUIRE(m.hp <= c.content().monsters.get(m.type).hit_points);
        REQUIRE(m.alive == (m.hp > 0));
        if (m.alive) {
            REQUIRE(c.scenario().map.walkable(m.hex));
            REQUIRE(taken.insert(m.hex).second);
        }
    }
}

}  // namespace

TEST_CASE("The real content and the arena scenario load") {
    const auto content = data::load_content(kData);
    CHECK(content.monsters.size() == 4);
    const auto s = data::load_scenario(kData / "scenarios" / "arena.json", content);
    CHECK(s.monsters.size() == 3);
    CHECK(s.hero_starts.size() == 2);
    const auto ruins = data::load_scenario(kData / "scenarios" / "ruins.json", content);
    CHECK(ruins.traps.size() == 3);
    CHECK(ruins.map.with_terrain(Terrain::Door).size() == 1);
    CHECK_FALSE(ruins.map.with_terrain(Terrain::Difficult).empty());
    CHECK(content.text.contains(ruins.name_key));
    for (const auto& id : content.monsters.ids()) {
        const auto& m = content.monsters.get(id);
        CHECK(content.text.contains(m.name_key));
        for (const auto& card : m.deck) {
            CAPTURE(card.id);
            CHECK(content.text.contains(card.name_key));
            for (const auto& a : card.actions) {
                // The monster AI does not push or pull yet.
                CHECK(a.push == 0);
                CHECK(a.pull == 0);
            }
        }
    }
    CHECK(content.text.contains(s.name_key));
}

TEST_CASE("Random play on every scenario always ends and keeps every invariant") {
    const auto content = data::load_content(kData);
    const scenario::ScenarioDef scenarios[] = {data::load_scenario(kData / "scenarios" / "arena.json", content),
                                               data::load_scenario(kData / "scenarios" / "ruins.json", content)};
    std::map<Outcome, int> outcomes;
    for (std::uint64_t seed = 0; seed < 150; ++seed) {
        CAPTURE(seed);
        const std::vector<std::pair<std::string, std::string>> parties[] = {
            {{"h1", "fighter"}, {"h2", "wizard"}}, {{"h1", "rogue"}, {"h2", "cleric"}}};
        std::vector<character::CharacterSheet> party;
        for (const auto& [id, cls] : parties[seed % 2]) party.push_back(real_hero(content, id, cls));

        Combat c(content, scenarios[(seed / 2) % 2], party, seed, (seed / 4) % 2 ? "tactical" : "classic");
        dice::Rng chooser(seed * 7919 + 1);
        int decisions = 0;
        while (!c.finished()) {
            REQUIRE(++decisions < 5000);
            const auto* d = c.pending();
            REQUIRE(d);
            REQUIRE(d->option_count() >= 2);
            c.decide(chooser.below(static_cast<std::uint32_t>(d->option_count())));
            check_invariants(c);
        }
        ++outcomes[c.outcome()];
        CHECK(c.events().back().kind == (c.outcome() == Outcome::Victory ? Event::Kind::Victory : Event::Kind::Defeat));
    }
    MESSAGE("random play: " << outcomes[Outcome::Victory] << " victories, " << outcomes[Outcome::Defeat] << " defeats");
}

TEST_CASE("Same seed and same choices give the same battle") {
    const auto content = data::load_content(kData);
    const auto s = data::load_scenario(kData / "scenarios" / "arena.json", content);
    auto run = [&](std::uint64_t seed) {
        Combat c(content, s, {real_hero(content, "h1", "fighter"), real_hero(content, "h2", "cleric")}, seed);
        dice::Rng chooser(99);
        while (!c.finished()) c.decide(chooser.below(static_cast<std::uint32_t>(c.pending()->option_count())));
        std::vector<std::string> log;
        for (const auto& e : c.events()) {
            log.push_back(std::string(event_kind_id(e.kind)) + e.actor + e.target + e.detail + std::to_string(e.value) +
                          std::to_string(e.roll));
        }
        return log;
    };
    CHECK(run(5) == run(5));
    CHECK(run(5) != run(6));
}

TEST_CASE("The auto player wins the arena often enough") {
    // Balance smoke test: two level-1 heroes played by the heuristic should
    // beat the three arena monsters most of the time, but not always.
    const auto content = data::load_content(kData);
    const auto s = data::load_scenario(kData / "scenarios" / "arena.json", content);
    const AutoPlayer player;
    const std::vector<std::pair<std::string, std::string>> parties[] = {
        {{"h1", "fighter"}, {"h2", "wizard"}}, {{"h1", "rogue"}, {"h2", "cleric"}},
        {{"h1", "fighter"}, {"h2", "cleric"}}, {{"h1", "rogue"}, {"h2", "wizard"}}};
    for (const auto& party_def : parties) {
        int wins = 0, rounds = 0;
        constexpr int kGames = 100;
        for (std::uint64_t seed = 0; seed < kGames; ++seed) {
            std::vector<character::CharacterSheet> party;
            for (const auto& [id, cls] : party_def) party.push_back(real_hero(content, id, cls));
            Combat c(content, s, party, seed);
            while (!c.finished()) c.decide(player.choose(c, *c.pending()));
            wins += c.outcome() == Outcome::Victory;
            rounds += c.round();
        }
        MESSAGE(party_def[0].second << " + " << party_def[1].second << ": " << wins << "/" << kGames
                                    << " wins, " << rounds / kGames << " rounds on average");
        CHECK(wins >= 40);
        CHECK(wins <= 98);
    }
}

TEST_CASE("Saving and loading at any point continues the exact same battle") {
    const auto content = data::load_content(kData);
    const scenario::ScenarioDef scenarios[] = {data::load_scenario(kData / "scenarios" / "arena.json", content),
                                               data::load_scenario(kData / "scenarios" / "ruins.json", content)};
    const AutoPlayer player;
    int saves = 0;
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        CAPTURE(seed);
        Combat c(content, scenarios[seed % 2], {real_hero(content, "h1", "rogue"), real_hero(content, "h2", "cleric")}, seed,
                 seed % 3 == 0 ? "classic" : "tactical");
        dice::Rng chooser(seed + 100);
        // Mix of heuristic and random choices to visit many states.
        auto choose = [&](const Combat& x, dice::Rng& r) {
            const auto* d = x.pending();
            return r.below(3) == 0 ? r.below(static_cast<std::uint32_t>(d->option_count())) : player.choose(x, *d);
        };
        while (!c.finished()) {
            if (chooser.below(6) == 0) {
                ++saves;
                const auto text = c.to_json().dump();
                auto loaded = Combat::from_json(content, nlohmann::json::parse(text));
                REQUIRE(loaded.to_json() == c.to_json());
                REQUIRE(loaded.pending() != nullptr);
                REQUIRE(loaded.pending()->option_count() == c.pending()->option_count());

                auto r1 = chooser;
                auto r2 = chooser;
                auto original = c;
                while (!original.finished()) original.decide(choose(original, r1));
                while (!loaded.finished()) loaded.decide(choose(loaded, r2));
                REQUIRE(loaded.to_json() == original.to_json());
            }
            c.decide(choose(c, chooser));
        }
        // A finished combat also round-trips.
        CHECK(Combat::from_json(content, c.to_json()).to_json() == c.to_json());
    }
    CHECK(saves > 100);
}

TEST_CASE("Broken saves are rejected") {
    const auto content = data::load_content(kData);
    const auto s = data::load_scenario(kData / "scenarios" / "arena.json", content);
    Combat c(content, s, {real_hero(content, "h1", "fighter"), real_hero(content, "h2", "wizard")}, 1);
    const auto good = c.to_json();
    CHECK_NOTHROW(Combat::from_json(content, good));

    auto bad = good;
    bad["version"] = 99;
    CHECK_THROWS_AS(Combat::from_json(content, bad), data::DataError);

    bad = good;
    bad["monsters"][0]["hp"] = 999;
    CHECK_THROWS_AS(Combat::from_json(content, bad), data::DataError);

    bad = good;
    bad["decks"][0]["draw"].push_back(0);
    CHECK_THROWS_AS(Combat::from_json(content, bad), data::DataError);

    bad = good;
    bad["heroes"][0]["cards"]["hand"].push_back("fighter.charge");
    CHECK_THROWS_AS(Combat::from_json(content, bad), data::DataError);

    bad = good;
    bad["heroes"][0]["hex"] = nlohmann::json::array({40, 40});
    CHECK_THROWS_AS(Combat::from_json(content, bad), data::DataError);

    bad = good;
    bad["phase"] = "dancing";
    CHECK_THROWS_AS(Combat::from_json(content, bad), data::DataError);

    bad = good;
    bad["extra"] = 1;
    CHECK_THROWS_AS(Combat::from_json(content, bad), data::DataError);

    bad = good;
    bad["game_master"] = "nobody";
    CHECK_THROWS_AS(Combat::from_json(content, bad), data::DataError);
}

// --- Game master -----------------------------------------------------------------

TEST_CASE("The game master is chosen at setup and kept by saves") {
    const auto content = test_content();
    const auto s = arena({{"dummy", {3, 0}}});
    CHECK(Combat(content, s, {hero(content, "a")}, 1).game_master().id == "classic");
    const Combat c(content, s, {hero(content, "a")}, 1, "sharp");
    CHECK(c.game_master().tactical);
    CHECK(c.to_json()["game_master"] == "sharp");
    CHECK(Combat::from_json(content, c.to_json()).game_master().id == "sharp");
    CHECK_THROWS_AS(Combat(content, s, {hero(content, "a")}, 1, "nobody"), std::invalid_argument);
}

TEST_CASE("The tactical game master goes for the hero it can drop") {
    const auto content = test_content();
    for (const std::string gm : {"classic", "sharp"}) {
        CAPTURE(gm);
        auto weak = hero(content, "b");
        weak.current_hp = 4;
        // Both heroes stand next to the biter; the classic rule takes the first one.
        Combat c(content, arena({{"biter", {0, 0}}}, {{1, 0}, {-1, 0}}), {hero(content, "a"), weak}, 3, gm);
        select(c, "tester.hit", "tester.stun");
        select(c, "tester.hit", "tester.stun");
        const auto hits = of_kind(c, Event::Kind::AttackRolled);
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].target == (gm == "sharp" ? "b" : "a"));
    }
}

TEST_CASE("A tactical ranged monster steps away before shooting") {
    const auto content = test_content();
    for (const std::string gm : {"classic", "sharp"}) {
        CAPTURE(gm);
        Combat c(content, arena({{"archer", {1, 0}}}), {hero(content, "a")}, 1, gm);
        select(c, "tester.hit", "tester.stun");
        REQUIRE(of_kind(c, Event::Kind::AttackRolled).size() == 1);
        const int d = distance(c.monsters()[0].hex, c.heroes()[0].hex);
        if (gm == "sharp") {
            CHECK(d >= 2);
            CHECK(d <= 3);
        } else {
            CHECK(d == 1);  // already in range: the classic rule does not move
        }
    }
}

TEST_CASE("The tactical game master plays every scenario within the rules") {
    const auto content = data::load_content(kData);
    REQUIRE(content.game_masters.find("tactical"));
    const auto s = data::load_scenario(kData / "scenarios" / "ruins.json", content);
    const AutoPlayer player;
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        CAPTURE(seed);
        Combat c(content, s, {real_hero(content, "h1", "fighter"), real_hero(content, "h2", "wizard")}, seed, "tactical");
        while (!c.finished()) {
            c.decide(player.choose(c, *c.pending()));
            check_invariants(c);
        }
    }
}

// --- Terrain -------------------------------------------------------------------

TEST_CASE("Ranged attacks need line of sight") {
    const auto content = test_content();
    const auto deck = std::vector<std::string>{"tester.shot", "tester.hit", "tester.stun", "tester.heal"};
    for (bool wall : {false, true}) {
        CAPTURE(wall);
        auto s = arena({{"dummy", {3, 0}}});
        if (wall) s.map.set_terrain({2, 0}, Terrain::Obstacle);
        Combat c(content, s, {hero_with(content, "a", deck)}, 1);
        select(c, "tester.shot", "tester.hit");
        play(c, "tester.shot", "tester.hit");
        const auto* d = c.pending();
        REQUIRE(d);
        if (wall) {
            CHECK(d->kind == Decision::Kind::Move);  // the attack had nothing to shoot at
            CHECK(of_kind(c, Event::Kind::NoTarget).size() == 1);
        } else {
            CHECK(d->kind == Decision::Kind::Target);
            CHECK(d->targets.front() == "dummy#1");
        }
    }
}

TEST_CASE("A trap fires once, then is gone") {
    const auto content = test_content();
    auto s = arena({{"dummy", {0, -3}}});
    s.map.set_terrain({1, 0}, Terrain::Trap);
    s.traps.push_back({{1, 0}, 3, {}});
    Combat c(content, s, {hero(content, "a")}, 2);
    select(c, "tester.hit", "tester.big");
    play(c, "tester.big", "tester.hit");  // big's attack has no target; hit's bottom moves 2
    const auto* d = c.pending();
    REQUIRE(d);
    REQUIRE(d->kind == Decision::Kind::Move);
    c.decide(pick(*d, [&](std::size_t i) { return d->hexes[i] == Hex{1, 0}; }));

    const auto traps = of_kind(c, Event::Kind::TrapTriggered);
    REQUIRE(traps.size() == 1);
    CHECK(traps[0].target == "a");
    CHECK(traps[0].to == Hex{1, 0});
    CHECK(c.heroes()[0].sheet.current_hp == 10 - 3);
    CHECK(c.scenario().traps.empty());
    CHECK(c.scenario().map.terrain({1, 0}) == Terrain::Open);
}

TEST_CASE("Movement goes around traps when it costs nothing more") {
    const auto content = test_content();
    auto s = arena({{"dummy", {0, -3}}});
    s.map.set_terrain({1, 0}, Terrain::Trap);
    s.traps.push_back({{1, 0}, 3, {}});
    Combat c(content, s, {hero(content, "a")}, 2);
    select(c, "tester.hit", "tester.big");
    play(c, "tester.big", "tester.hit");
    const auto* d = c.pending();
    REQUIRE(d);
    c.decide(pick(*d, [&](std::size_t i) { return d->hexes[i] == Hex{1, 1}; }));
    const auto moved = of_kind(c, Event::Kind::Moved);
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].path == std::vector<Hex>{{0, 1}, {1, 1}});
    CHECK(of_kind(c, Event::Kind::TrapTriggered).empty());
}

TEST_CASE("Doors stop monsters and open when a hero walks in") {
    const auto content = test_content();
    auto s = arena({{"walker", {3, -1}}}, {{-1, 0}});
    for (int r = -3; r <= 2; ++r) s.map.set_terrain({1, r}, Terrain::Obstacle);
    s.map.set_terrain({1, 0}, Terrain::Door);
    Combat c(content, s, {hero(content, "a")}, 4);

    // Round 1: step next to the door. The walker cannot get through.
    select(c, "tester.hit", "tester.big");
    play(c, "tester.hit", "tester.big");  // no target for hit; big's bottom moves 3
    REQUIRE(c.pending()->kind == Decision::Kind::Move);
    c.decide(pick(*c.pending(), [&](std::size_t i) { return c.pending()->hexes[i] == Hex{0, 0}; }));
    REQUIRE(c.pending()->kind == Decision::Kind::ShortRest);
    c.decide(0);  // no rest
    REQUIRE(c.round() == 2);
    CHECK(c.monsters()[0].hex.q >= 2);
    CHECK_FALSE(line_of_sight(c.scenario().map, {0, 0}, {2, 0}));

    // Round 2: walk into the door.
    select(c, "tester.stun", "tester.heal");
    play(c, "tester.stun", "tester.heal");  // the stun attack has no target; heal's bottom moves 1
    REQUIRE(c.pending()->kind == Decision::Kind::Move);
    c.decide(pick(*c.pending(), [&](std::size_t i) { return c.pending()->hexes[i] == Hex{1, 0}; }));
    const auto opened = of_kind(c, Event::Kind::DoorOpened);
    REQUIRE(opened.size() == 1);
    CHECK(opened[0].actor == "a");
    CHECK(opened[0].to == Hex{1, 0});
    CHECK(c.scenario().map.terrain({1, 0}) == Terrain::Open);
}
