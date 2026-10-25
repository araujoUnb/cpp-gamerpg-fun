// Save and load of a Combat. Kept apart from the rules in combat.cpp.
#include "combat/combat.hpp"

#include "cards/deck.hpp"
#include "data/card_io.hpp"
#include "data/json_helpers.hpp"
#include "data/json_io.hpp"

#include <algorithm>
#include <array>

namespace rpg::combat {

using namespace data::detail;
using json = nlohmann::json;

namespace {

constexpr int kSaveVersion = 2;

constexpr std::array kEventKinds{
    Event::Kind::RoundStarted,   Event::Kind::CardsSelected,     Event::Kind::MonsterCardDrawn,
    Event::Kind::TurnStarted,    Event::Kind::TurnSkipped,       Event::Kind::HalfUsed,
    Event::Kind::NoTarget,       Event::Kind::Moved,             Event::Kind::DoorOpened,
    Event::Kind::TrapTriggered,  Event::Kind::AttackRolled,
    Event::Kind::Damaged,        Event::Kind::Healed,            Event::Kind::ConditionApplied,
    Event::Kind::ConditionResisted, Event::Kind::ConditionEnded, Event::Kind::MonsterDied,
    Event::Kind::HeroDowned,     Event::Kind::DeathSave,         Event::Kind::HeroStabilized,
    Event::Kind::HeroRevived,    Event::Kind::HeroDied,          Event::Kind::HeroExhausted,
    Event::Kind::CardLost,       Event::Kind::LongRested,        Event::Kind::ShortRested,
    Event::Kind::RoundEnded,     Event::Kind::Victory,           Event::Kind::Defeat};

// Maps an enum to the strings used in the save file.
template <typename E, std::size_t N>
struct Names {
    std::array<std::pair<E, const char*>, N> items;

    const char* to(E e) const {
        for (const auto& [v, s] : items) {
            if (v == e) return s;
        }
        return "?";
    }
    E from(const json& j, const char* key, const std::string& where) const {
        const auto s = get_string(j, key, where);
        for (const auto& [v, name] : items) {
            if (s == name) return v;
        }
        fail(where, "unknown value '" + s + "' for " + quote_key(key));
    }
};

json hex_json(Hex h) { return json::array({h.q, h.r}); }

Hex hex_from(const json& j, const std::string& where) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number_integer() || !j[1].is_number_integer()) {
        fail(where, "a hex is written as [q, r]");
    }
    return {j[0].get<int>(), j[1].get<int>()};
}

json conditions_json(const std::vector<ActiveCondition>& conds) {
    json out = json::array();
    for (const auto& c : conds) out.push_back({{"id", c.id}, {"rounds", c.rounds}, {"fresh", c.fresh}});
    return out;
}

std::vector<ActiveCondition> conditions_from(const json& arr, const character::Ruleset& rules, const std::string& where) {
    std::vector<ActiveCondition> out;
    if (!arr.is_array()) fail(where, "conditions must be an array");
    for (const auto& c : arr) {
        only_keys(c, {"id", "rounds", "fresh"}, where);
        ActiveCondition a{get_string(c, "id", where), get_int(c, "rounds", where), get_bool(c, "fresh", where)};
        if (!rules.find_condition(a.id)) fail(where, "unknown condition '" + a.id + "'");
        if (a.rounds < 1) fail(where, "condition rounds must be at least 1");
        out.push_back(std::move(a));
    }
    return out;
}

json play_json(const cards::TurnPlay& p) {
    return {{"top", {{"card", p.top.card}, {"basic", p.top.basic}}},
            {"bottom", {{"card", p.bottom.card}, {"basic", p.bottom.basic}}},
            {"top_first", p.top_first}};
}

cards::TurnPlay play_from(const json& j, const std::string& where) {
    only_keys(j, {"top", "bottom", "top_first"}, where);
    auto use = [&](const char* key) {
        const auto& u = field(j, key, where);
        only_keys(u, {"card", "basic"}, where);
        return cards::HalfUse{get_string(u, "card", where), get_bool(u, "basic", where)};
    };
    return {use("top"), use("bottom"), get_bool(j, "top_first", where)};
}

json event_json(const Event& e) {
    json j{{"kind", event_kind_id(e.kind)}};
    if (!e.actor.empty()) j["actor"] = e.actor;
    if (!e.target.empty()) j["target"] = e.target;
    if (!e.detail.empty()) j["detail"] = e.detail;
    if (e.value) j["value"] = e.value;
    if (e.value2) j["value2"] = e.value2;
    if (e.roll) j["roll"] = e.roll;
    if (e.total) j["total"] = e.total;
    if (e.target_number) j["target_number"] = e.target_number;
    if (e.flag) j["flag"] = true;
    if (e.critical) j["critical"] = true;
    if (e.from != Hex{} || e.to != Hex{}) {
        j["from"] = hex_json(e.from);
        j["to"] = hex_json(e.to);
    }
    if (!e.path.empty()) {
        json path = json::array();
        for (Hex h : e.path) path.push_back(hex_json(h));
        j["path"] = path;
    }
    return j;
}

Event event_from(const json& j, const std::string& where) {
    only_keys(j, {"kind", "actor", "target", "detail", "value", "value2", "roll", "total", "target_number", "flag",
                  "critical", "from", "to", "path"}, where);
    Event e;
    const auto kind = get_string(j, "kind", where);
    const auto it = std::find_if(kEventKinds.begin(), kEventKinds.end(),
                                 [&](Event::Kind k) { return event_kind_id(k) == kind; });
    if (it == kEventKinds.end()) fail(where, "unknown event kind '" + kind + "'");
    e.kind = *it;
    e.actor = get_string_or(j, "actor", where);
    e.target = get_string_or(j, "target", where);
    e.detail = get_string_or(j, "detail", where);
    e.value = get_int_or(j, "value", 0, where);
    e.value2 = get_int_or(j, "value2", 0, where);
    e.roll = get_int_or(j, "roll", 0, where);
    e.total = get_int_or(j, "total", 0, where);
    e.target_number = get_int_or(j, "target_number", 0, where);
    e.flag = get_bool_or(j, "flag", false, where);
    e.critical = get_bool_or(j, "critical", false, where);
    if (j.contains("from")) e.from = hex_from(j["from"], where);
    if (j.contains("to")) e.to = hex_from(j["to"], where);
    if (j.contains("path")) {
        for (const auto& h : get_array(j, "path", where)) e.path.push_back(hex_from(h, where));
    }
    return e;
}

const Names<HeroStatus, 5> kHeroStatus{{{{HeroStatus::Active, "active"},
                                         {HeroStatus::Dying, "dying"},
                                         {HeroStatus::Stable, "stable"},
                                         {HeroStatus::Dead, "dead"},
                                         {HeroStatus::Exhausted, "exhausted"}}}};
const Names<Outcome, 3> kOutcome{{{{Outcome::Ongoing, "ongoing"}, {Outcome::Victory, "victory"}, {Outcome::Defeat, "defeat"}}}};

}  // namespace

json Combat::to_json() const {
    static const Names<Phase, 4> phases{{{{Phase::Selecting, "selecting"},
                                          {Phase::Acting, "acting"},
                                          {Phase::EndOfRound, "end_of_round"},
                                          {Phase::Finished, "finished"}}}};
    static const Names<Stage, 4> stages{{{{Stage::None, "none"},
                                          {Stage::ChoosePlay, "choose_play"},
                                          {Stage::Executing, "executing"},
                                          {Stage::LongRestLoss, "long_rest_loss"}}}};

    json scenario{{"id", scenario_.id}, {"name", scenario_.name_key}};
    json hexes = json::array();
    for (Hex h : scenario_.map.hexes()) hexes.push_back(hex_json(h));
    auto hex_list = [&](Terrain t) {
        json out = json::array();
        for (Hex h : scenario_.map.with_terrain(t)) out.push_back(hex_json(h));
        return out;
    };
    json traps = json::array();
    for (const auto& t : scenario_.traps) {
        json conds = json::array();
        for (const auto& c : t.conditions) conds.push_back({{"id", c.id}, {"rounds", c.rounds}});
        traps.push_back({{"hex", hex_json(t.hex)}, {"damage", t.damage}, {"conditions", conds}});
    }
    json starts = json::array();
    for (Hex h : scenario_.hero_starts) starts.push_back(hex_json(h));
    json spawns = json::array();
    for (const auto& m : scenario_.monsters) spawns.push_back({{"type", m.type}, {"hex", hex_json(m.hex)}});
    scenario["hexes"] = hexes;
    scenario["obstacles"] = hex_list(Terrain::Obstacle);
    scenario["difficult"] = hex_list(Terrain::Difficult);
    scenario["doors"] = hex_list(Terrain::Door);
    scenario["traps"] = traps;
    scenario["hero_starts"] = starts;
    scenario["monsters"] = spawns;

    json heroes = json::array();
    for (const auto& h : heroes_) {
        heroes.push_back({{"sheet", data::to_json(h.sheet)},
                          {"cards", data::to_json(h.cards)},
                          {"hex", hex_json(h.hex)},
                          {"conditions", conditions_json(h.conditions)},
                          {"status", kHeroStatus.to(h.status)},
                          {"death_successes", h.death_successes},
                          {"death_failures", h.death_failures},
                          {"acted", h.acted},
                          {"long_rested", h.long_rested}});
    }
    json monsters = json::array();
    for (const auto& m : monsters_) {
        monsters.push_back({{"id", m.id}, {"type", m.type}, {"hex", hex_json(m.hex)}, {"hp", m.hp},
                            {"conditions", conditions_json(m.conditions)}, {"alive", m.alive}, {"acted", m.acted}});
    }
    json decks = json::array();
    for (const auto& d : decks_) {
        decks.push_back({{"type", d.type}, {"draw", d.draw}, {"discard", d.discard}, {"current", d.current}});
    }
    json order = json::array();
    for (const auto& o : order_) {
        order.push_back({{"hero", o.hero}, {"index", o.index}, {"initiative", o.initiative}, {"tiebreak", o.tiebreak}});
    }
    json events = json::array();
    for (const auto& e : events_) events.push_back(event_json(e));

    const auto& p = progress_;
    return {
        {"version", kSaveVersion},
        {"rng", data::to_json(rng_.state())},
        {"game_master", gm_.id},
        {"scenario", scenario},
        {"heroes", heroes},
        {"monsters", monsters},
        {"decks", decks},
        {"round", round_},
        {"phase", phases.to(phase_)},
        {"outcome", kOutcome.to(outcome_)},
        {"order", order},
        {"turn", turn_},
        {"cursor", cursor_},
        {"end_resolved", end_resolved_},
        {"progress",
         {{"stage", stages.to(p.stage)},
          {"play", p.play ? play_json(*p.play) : json(nullptr)},
          {"step", p.step},
          {"action", p.action},
          {"action_started", p.action_started},
          {"announced", p.announced},
          {"chosen", p.chosen}}},
        {"events", events},
    };
}

Combat Combat::from_json(const data::Content& content, const json& j) {
    static const Names<Phase, 4> phases{{{{Phase::Selecting, "selecting"},
                                          {Phase::Acting, "acting"},
                                          {Phase::EndOfRound, "end_of_round"},
                                          {Phase::Finished, "finished"}}}};
    static const Names<Stage, 4> stages{{{{Stage::None, "none"},
                                          {Stage::ChoosePlay, "choose_play"},
                                          {Stage::Executing, "executing"},
                                          {Stage::LongRestLoss, "long_rest_loss"}}}};
    const std::string where = "save";
    only_keys(j, {"version", "rng", "game_master", "scenario", "heroes", "monsters", "decks", "round", "phase", "outcome", "order",
                  "turn", "cursor", "end_resolved", "progress", "events"}, where);
    if (get_int(j, "version", where) != kSaveVersion) fail(where, "unsupported save version");

    Combat c(content);
    const auto& rules = content.rules;
    c.rng_ = dice::Rng::from_state(data::rng_state_from_json(field(j, "rng", where), where + ".rng"));
    {
        const auto id = get_string(j, "game_master", where);
        auto gm = find_game_master(content, id);
        if (!gm) fail(where, "unknown game master '" + id + "'");
        c.gm_ = std::move(*gm);
    }

    // Scenario.
    {
        const std::string ctx = where + ".scenario";
        const auto& s = field(j, "scenario", where);
        only_keys(s, {"id", "name", "hexes", "obstacles", "difficult", "doors", "traps", "hero_starts", "monsters"}, ctx);
        c.scenario_.id = get_string(s, "id", ctx);
        c.scenario_.name_key = get_string(s, "name", ctx);
        std::set<Hex> hexes;
        for (const auto& h : get_array(s, "hexes", ctx)) hexes.insert(hex_from(h, ctx));
        if (hexes.empty()) fail(ctx, "the map has no hexes");
        c.scenario_.map = HexMap(std::move(hexes));
        auto& map = c.scenario_.map;
        std::set<Hex> special;
        auto mark = [&](Hex h, Terrain t) {
            if (!map.contains(h)) fail(ctx, "terrain outside the map");
            if (!special.insert(h).second) fail(ctx, "a hex has two terrains");
            map.set_terrain(h, t);
        };
        for (const auto& h : get_array(s, "obstacles", ctx)) mark(hex_from(h, ctx), Terrain::Obstacle);
        for (const auto& h : get_array(s, "difficult", ctx)) mark(hex_from(h, ctx), Terrain::Difficult);
        for (const auto& h : get_array(s, "doors", ctx)) mark(hex_from(h, ctx), Terrain::Door);
        for (const auto& t : get_array(s, "traps", ctx)) {
            only_keys(t, {"hex", "damage", "conditions"}, ctx);
            scenario::TrapDef trap;
            trap.hex = hex_from(field(t, "hex", ctx), ctx);
            trap.damage = get_int(t, "damage", ctx);
            trap.conditions = data::condition_list_from_json(field(t, "conditions", ctx), rules, ctx);
            mark(trap.hex, Terrain::Trap);
            c.scenario_.traps.push_back(std::move(trap));
        }
        for (const auto& h : get_array(s, "hero_starts", ctx)) c.scenario_.hero_starts.push_back(hex_from(h, ctx));
        for (const auto& m : get_array(s, "monsters", ctx)) {
            only_keys(m, {"type", "hex"}, ctx);
            c.scenario_.monsters.push_back({get_string(m, "type", ctx), hex_from(field(m, "hex", ctx), ctx)});
        }
    }
    const auto& map = c.scenario_.map;

    // Heroes.
    for (const auto& h : get_array(j, "heroes", where)) {
        const std::string ctx = where + ".heroes";
        only_keys(h, {"sheet", "cards", "hex", "conditions", "status", "death_successes", "death_failures", "acted",
                      "long_rested"}, ctx);
        const auto& sheet_json = field(h, "sheet", ctx);
        const auto* cls = content.classes.find(get_string(sheet_json, "class", ctx + ".sheet"));
        if (!cls) fail(ctx, "unknown class in hero sheet");
        HeroState hero;
        hero.sheet = data::sheet_from_json(sheet_json, rules, *cls, ctx + ".sheet");
        try {
            cards::validate_deck(content.cards, *cls, hero.sheet.level, hero.sheet.deck);
        } catch (const std::invalid_argument& e) {
            fail(ctx, e.what());
        }
        hero.cards = data::hero_cards_from_json(field(h, "cards", ctx), hero.sheet.deck, ctx + ".cards");
        hero.hex = hex_from(field(h, "hex", ctx), ctx);
        hero.conditions = conditions_from(field(h, "conditions", ctx), rules, ctx);
        hero.status = kHeroStatus.from(h, "status", ctx);
        hero.death_successes = get_int(h, "death_successes", ctx);
        hero.death_failures = get_int(h, "death_failures", ctx);
        hero.acted = get_bool(h, "acted", ctx);
        hero.long_rested = get_bool(h, "long_rested", ctx);
        if (hero.on_board() && !map.walkable(hero.hex)) fail(ctx, "hero '" + hero.sheet.id + "' is off the map");
        if ((hero.status == HeroStatus::Active) != (hero.sheet.current_hp > 0) &&
            hero.status != HeroStatus::Dead && hero.status != HeroStatus::Exhausted) {
            fail(ctx, "hero '" + hero.sheet.id + "' status does not match its HP");
        }
        for (const auto& other : c.heroes_) {
            if (other.sheet.id == hero.sheet.id) fail(ctx, "duplicate hero id '" + hero.sheet.id + "'");
        }
        c.heroes_.push_back(std::move(hero));
    }
    if (c.heroes_.empty()) fail(where, "no heroes");

    // Monsters.
    for (const auto& m : get_array(j, "monsters", where)) {
        const std::string ctx = where + ".monsters";
        only_keys(m, {"id", "type", "hex", "hp", "conditions", "alive", "acted"}, ctx);
        MonsterState mon;
        mon.id = get_string(m, "id", ctx);
        mon.type = get_string(m, "type", ctx);
        const auto* def = content.monsters.find(mon.type);
        if (!def) fail(ctx, "unknown monster type '" + mon.type + "'");
        mon.hex = hex_from(field(m, "hex", ctx), ctx);
        mon.hp = get_int(m, "hp", ctx);
        mon.conditions = conditions_from(field(m, "conditions", ctx), rules, ctx);
        mon.alive = get_bool(m, "alive", ctx);
        mon.acted = get_bool(m, "acted", ctx);
        if (mon.hp < 0 || mon.hp > def->hit_points || mon.alive != (mon.hp > 0)) fail(ctx, "monster '" + mon.id + "' has invalid HP");
        if (mon.alive && !map.walkable(mon.hex)) fail(ctx, "monster '" + mon.id + "' is off the map");
        c.monsters_.push_back(std::move(mon));
    }

    // Monster decks: every card of the type exactly once across draw, discard and current.
    for (const auto& d : get_array(j, "decks", where)) {
        const std::string ctx = where + ".decks";
        only_keys(d, {"type", "draw", "discard", "current"}, ctx);
        MonsterDeckState deck;
        deck.type = get_string(d, "type", ctx);
        const auto* def = content.monsters.find(deck.type);
        if (!def) fail(ctx, "unknown monster type '" + deck.type + "'");
        auto ints = [&](const char* key) {
            std::vector<int> out;
            for (const auto& v : get_array(d, key, ctx)) {
                if (!v.is_number_integer()) fail(ctx, quote_key(key) + " must hold integers");
                out.push_back(v.get<int>());
            }
            return out;
        };
        deck.draw = ints("draw");
        deck.discard = ints("discard");
        deck.current = get_int(d, "current", ctx);
        std::vector<int> all = deck.draw;
        all.insert(all.end(), deck.discard.begin(), deck.discard.end());
        if (deck.current >= 0) all.push_back(deck.current);
        std::sort(all.begin(), all.end());
        std::vector<int> expected(def->deck.size());
        for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<int>(i);
        if (all != expected) fail(ctx, "deck of '" + deck.type + "' does not hold each card exactly once");
        c.decks_.push_back(std::move(deck));
    }

    c.round_ = get_int(j, "round", where);
    c.phase_ = phases.from(j, "phase", where);
    c.outcome_ = kOutcome.from(j, "outcome", where);
    if ((c.phase_ == Phase::Finished) != (c.outcome_ != Outcome::Ongoing)) fail(where, "phase and outcome disagree");

    for (const auto& o : get_array(j, "order", where)) {
        const std::string ctx = where + ".order";
        only_keys(o, {"hero", "index", "initiative", "tiebreak"}, ctx);
        TurnOrderEntry e{get_bool(o, "hero", ctx), static_cast<std::size_t>(get_int(o, "index", ctx)),
                         get_int(o, "initiative", ctx), get_int(o, "tiebreak", ctx)};
        if (e.index >= (e.hero ? c.heroes_.size() : c.monsters_.size())) fail(ctx, "turn order index out of range");
        c.order_.push_back(e);
    }
    c.turn_ = static_cast<std::size_t>(get_int(j, "turn", where));
    c.cursor_ = static_cast<std::size_t>(get_int(j, "cursor", where));
    c.end_resolved_ = get_bool(j, "end_resolved", where);
    if (c.turn_ > c.order_.size() || c.cursor_ > c.heroes_.size()) fail(where, "turn or cursor out of range");

    {
        const std::string ctx = where + ".progress";
        const auto& p = field(j, "progress", where);
        only_keys(p, {"stage", "play", "step", "action", "action_started", "announced", "chosen"}, ctx);
        auto& prog = c.progress_;
        prog.stage = stages.from(p, "stage", ctx);
        if (!field(p, "play", ctx).is_null()) prog.play = play_from(p["play"], ctx + ".play");
        prog.step = static_cast<std::size_t>(get_int(p, "step", ctx));
        prog.action = static_cast<std::size_t>(get_int(p, "action", ctx));
        prog.action_started = get_bool(p, "action_started", ctx);
        prog.announced = get_bool(p, "announced", ctx);
        prog.chosen = string_list(field(p, "chosen", ctx), ctx + ".chosen");
        if (prog.stage == Stage::Executing) {
            if (!prog.play || c.turn_ >= c.order_.size() || !c.order_[c.turn_].hero) fail(ctx, "no hero turn in progress");
            const auto& hero = c.heroes_[c.order_[c.turn_].index];
            try {
                prog.steps = hero.cards.plan(*prog.play, content.cards, rules.cards);
            } catch (const std::exception& e) {
                fail(ctx, e.what());
            }
            if (prog.step > prog.steps.size() ||
                (prog.step < prog.steps.size() && prog.action > prog.steps[prog.step].actions.size())) {
                fail(ctx, "turn progress out of range");
            }
        }
    }

    std::size_t n = 0;
    for (const auto& e : get_array(j, "events", where)) c.events_.push_back(event_from(e, where + ".events[" + std::to_string(n++) + "]"));

    // Regenerates the pending decision. Building a decision has no side effects,
    // so this lands exactly where the saved combat was waiting.
    c.advance();
    return c;
}

}  // namespace rpg::combat
