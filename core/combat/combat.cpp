#include "combat/combat.hpp"

#include "cards/deck.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <tuple>

namespace rpg::combat {

using cards::ActionType;
using cards::TargetKind;
using character::ConditionEffects;

namespace {

constexpr int kFar = std::numeric_limits<int>::max() / 4;

}  // namespace

std::size_t Decision::option_count() const {
    switch (kind) {
        case Kind::SelectCards: return selections.size();
        case Kind::PlayTurn: return plays.size();
        case Kind::Move: return hexes.size();
        case Kind::Target: return targets.size();
        case Kind::LongRestLoss: return cards.size();
        case Kind::ShortRest: return 2;
    }
    return 0;
}

// --- Setup ------------------------------------------------------------------

Combat::Combat(const data::Content& content, scenario::ScenarioDef scenario,
               std::vector<character::CharacterSheet> party, std::uint64_t seed)
    : content_(&content), scenario_(std::move(scenario)), rng_(seed) {
    if (party.empty()) throw std::invalid_argument("the party is empty");
    if (party.size() > scenario_.hero_starts.size()) throw std::invalid_argument("more heroes than start hexes");

    for (std::size_t i = 0; i < party.size(); ++i) {
        auto& sheet = party[i];
        const auto& cls = content.classes.get(sheet.class_id);
        character::validate(content.rules, sheet, cls);
        cards::validate_deck(content.cards, cls, sheet.level, sheet.deck);
        if (sheet.id.find('#') != std::string::npos) throw std::invalid_argument("hero ids cannot contain '#'");
        for (const auto& other : heroes_) {
            if (other.sheet.id == sheet.id) throw std::invalid_argument("duplicate hero id '" + sheet.id + "'");
        }
        if (sheet.current_hp == 0) throw std::invalid_argument("hero '" + sheet.id + "' starts at 0 HP");
        HeroState h;
        h.sheet = std::move(sheet);
        h.cards = cards::HeroCards::from_deck(h.sheet.deck);
        h.hex = scenario_.hero_starts[i];
        heroes_.push_back(std::move(h));
    }

    std::map<std::string, int> count;
    for (const auto& spawn : scenario_.monsters) {
        const auto& def = content.monsters.get(spawn.type);
        MonsterState m;
        m.type = spawn.type;
        m.id = spawn.type + "#" + std::to_string(++count[spawn.type]);
        m.hex = spawn.hex;
        m.hp = def.hit_points;
        monsters_.push_back(std::move(m));
    }
    for (const auto& [type, _] : count) {
        MonsterDeckState deck;
        deck.type = type;
        for (std::size_t i = 0; i < content.monsters.get(type).deck.size(); ++i) deck.draw.push_back(static_cast<int>(i));
        rng_.shuffle(std::span<int>(deck.draw));
        decks_.push_back(std::move(deck));
    }

    begin_round();
    advance();
}

// --- Engine loop --------------------------------------------------------------

void Combat::decide(std::size_t option) {
    if (!pending_) throw std::logic_error("no decision is pending");
    if (option >= pending_->option_count()) throw std::out_of_range("decision option out of range");
    const Decision d = std::move(*pending_);
    pending_.reset();
    apply(d, option);
    advance();
}

void Combat::advance() {
    while (!finished() && !pending_) {
        bool waiting = false;
        switch (phase_) {
            case Phase::Selecting: waiting = step_selecting(); break;
            case Phase::Acting: waiting = step_acting(); break;
            case Phase::EndOfRound: waiting = step_end_of_round(); break;
            case Phase::Finished: return;
        }
        if (waiting) return;
    }
}

bool Combat::set_decision(Decision d) {
    const auto n = d.option_count();
    if (n == 0) throw std::logic_error("internal error: decision without options");
    if (n == 1) {
        apply(d, 0);
        return false;
    }
    pending_ = std::move(d);
    return true;
}

void Combat::apply(const Decision& d, std::size_t option) {
    const auto& rules = content_->rules;
    switch (d.kind) {
        case Decision::Kind::SelectCards: {
            auto& h = heroes_[*hero_index(d.actor)];
            const auto& sel = d.selections[option];
            h.cards.select(sel, rules.cards);
            emit({.kind = Event::Kind::CardsSelected, .actor = h.sheet.id,
                  .value = h.cards.initiative(content_->cards, rules.cards), .flag = sel.long_rest});
            ++cursor_;
            break;
        }
        case Decision::Kind::PlayTurn: {
            const auto& h = heroes_[*hero_index(d.actor)];
            progress_.play = d.plays[option];
            progress_.steps = h.cards.plan(*progress_.play, content_->cards, rules.cards);
            progress_.stage = Stage::Executing;
            break;
        }
        case Decision::Kind::Move: {
            const auto i = *hero_index(d.actor);
            const Hex from = heroes_[i].hex;
            const Hex to = d.hexes[option];
            if (to != from) {
                const auto& map = scenario_.map;
                const bool jump = d.action && d.action->jump;
                // Same cost as the options were computed with; avoids traps when it can.
                const auto path = jump ? std::vector<Hex>{to}
                                       : find_path(map, from, to, [&](Hex x) { return !monster_occupies(x); },
                                                   [&](Hex x) { return map.terrain(x) == Terrain::Trap ? 1 : 0; });
                walk(true, i, path, jump, false);
            }
            complete_action();
            break;
        }
        case Decision::Kind::Target: {
            const auto& id = d.targets[option];
            if (id.empty()) {
                complete_action();
                break;
            }
            const auto hero = *hero_index(d.actor);
            const auto& a = *d.action;
            progress_.chosen.push_back(id);
            if (a.type == ActionType::Attack) {
                hero_attacks_monster(hero, *monster_index(id), a);
            } else if (a.type == ActionType::Heal) {
                const auto t = *hero_index(id);
                heal_hero(d.actor, t, a.value);
                apply_conditions(id, heroes_[t].conditions, a.conditions, t == hero, nullptr);
            } else if (const auto t = hero_index(id)) {
                apply_conditions(id, heroes_[*t].conditions, a.conditions, *t == hero, nullptr);
            } else {
                auto& m = monsters_[*monster_index(id)];
                apply_conditions(id, m.conditions, a.conditions, false, &def_of(m));
            }
            if (static_cast<int>(progress_.chosen.size()) >= a.targets) complete_action();
            break;
        }
        case Decision::Kind::LongRestLoss: {
            const auto i = *hero_index(d.actor);
            auto& h = heroes_[i];
            const auto& card = d.cards[option];
            h.cards.long_rest(card, rules.cards);
            h.long_rested = true;
            emit({.kind = Event::Kind::LongRested, .actor = h.sheet.id});
            emit({.kind = Event::Kind::CardLost, .actor = h.sheet.id, .detail = card});
            heal_hero(h.sheet.id, i, rules.cards.long_rest_heal);
            finish_hero_turn(i);
            break;
        }
        case Decision::Kind::ShortRest: {
            if (option == 1) {
                auto& h = heroes_[*hero_index(d.actor)];
                const auto lost = h.cards.short_rest(rng_, rules.cards);
                emit({.kind = Event::Kind::ShortRested, .actor = h.sheet.id});
                emit({.kind = Event::Kind::CardLost, .actor = h.sheet.id, .detail = lost});
            }
            ++cursor_;
            break;
        }
    }
}

// --- Phases -------------------------------------------------------------------

void Combat::begin_round() {
    ++round_;
    for (auto& h : heroes_) h.acted = h.long_rested = false;
    for (auto& m : monsters_) m.acted = false;
    order_.clear();
    turn_ = 0;
    cursor_ = 0;
    end_resolved_ = false;
    progress_ = {};
    phase_ = Phase::Selecting;
    emit({.kind = Event::Kind::RoundStarted, .value = round_});
}

bool Combat::step_selecting() {
    const auto& rules = content_->rules.cards;
    while (cursor_ < heroes_.size()) {
        auto& h = heroes_[cursor_];
        if (h.status != HeroStatus::Active) {
            ++cursor_;
            continue;
        }
        if (h.cards.status(rules) == cards::RoundStatus::Exhausted) {
            h.status = HeroStatus::Exhausted;
            emit({.kind = Event::Kind::HeroExhausted, .target = h.sheet.id});
            check_outcome();
            if (finished()) return false;
            ++cursor_;
            continue;
        }
        Decision d;
        d.kind = Decision::Kind::SelectCards;
        d.actor = h.sheet.id;
        d.selections = h.cards.legal_selections(rules);
        return set_decision(std::move(d));
    }
    start_acting();
    return false;
}

void Combat::start_acting() {
    for (auto& deck : decks_) {
        const bool present = std::any_of(monsters_.begin(), monsters_.end(),
                                         [&](const MonsterState& m) { return m.alive && m.type == deck.type; });
        if (!present) continue;
        if (deck.draw.empty()) {
            deck.draw.swap(deck.discard);
            rng_.shuffle(std::span<int>(deck.draw));
        }
        deck.current = deck.draw.back();
        deck.draw.pop_back();
        const auto& card = content_->monsters.get(deck.type).deck[static_cast<std::size_t>(deck.current)];
        emit({.kind = Event::Kind::MonsterCardDrawn, .actor = deck.type, .detail = card.id, .value = card.initiative});
    }

    order_.clear();
    const auto& rules = content_->rules.cards;
    for (std::size_t i = 0; i < heroes_.size(); ++i) {
        const auto& h = heroes_[i];
        if (h.status != HeroStatus::Active || !h.cards.selection()) continue;
        const auto& sel = *h.cards.selection();
        const int tiebreak = sel.long_rest ? rules.initiative.max + 1 : content_->cards.get(sel.other).initiative;
        order_.push_back({true, i, h.cards.initiative(content_->cards, rules), tiebreak});
    }
    for (std::size_t i = 0; i < monsters_.size(); ++i) {
        const auto& m = monsters_[i];
        if (!m.alive) continue;
        for (const auto& deck : decks_) {
            if (deck.type != m.type) continue;
            const auto& card = content_->monsters.get(m.type).deck[static_cast<std::size_t>(deck.current)];
            order_.push_back({false, i, card.initiative, 0});
        }
    }
    // Lower initiative first; heroes win ties; then the hero's second card; then table order.
    std::stable_sort(order_.begin(), order_.end(), [](const TurnOrderEntry& a, const TurnOrderEntry& b) {
        if (a.initiative != b.initiative) return a.initiative < b.initiative;
        if (a.hero != b.hero) return a.hero;
        if (a.tiebreak != b.tiebreak) return a.tiebreak < b.tiebreak;
        return a.index < b.index;
    });
    phase_ = Phase::Acting;
    turn_ = 0;
    progress_ = {};
}

bool Combat::step_acting() {
    if (turn_ >= order_.size()) {
        phase_ = Phase::EndOfRound;
        cursor_ = 0;
        return false;
    }
    const auto entry = order_[turn_];
    if (entry.hero) return step_hero_turn(entry.index);
    run_monster_turn(entry.index);
    ++turn_;
    return false;
}

bool Combat::step_hero_turn(std::size_t i) {
    auto& h = heroes_[i];
    if (progress_.stage == Stage::None) {
        if (h.status != HeroStatus::Active || !h.cards.selection()) {  // knocked out before acting
            ++turn_;
            return false;
        }
        h.acted = true;
        emit({.kind = Event::Kind::TurnStarted, .actor = h.sheet.id});
        if (h.cards.selection()->long_rest) {
            progress_.stage = Stage::LongRestLoss;
        } else if (has_effect(h.conditions, &ConditionEffects::skip_turn)) {
            emit({.kind = Event::Kind::TurnSkipped, .actor = h.sheet.id, .detail = "stunned"});
            h.cards.skip_turn();
            finish_hero_turn(i);
            return false;
        } else {
            progress_.stage = Stage::ChoosePlay;
        }
    }

    Decision d;
    d.actor = h.sheet.id;
    switch (progress_.stage) {
        case Stage::LongRestLoss:
            d.kind = Decision::Kind::LongRestLoss;
            d.cards = h.cards.legal_long_rest_losses();
            return set_decision(std::move(d));
        case Stage::ChoosePlay:
            d.kind = Decision::Kind::PlayTurn;
            d.plays = h.cards.legal_plays();
            return set_decision(std::move(d));
        case Stage::Executing:
            return step_hero_action(i);
        case Stage::None:
            break;
    }
    return false;
}

void Combat::complete_action() {
    ++progress_.action;
    progress_.action_started = false;
    progress_.chosen.clear();
}

bool Combat::step_hero_action(std::size_t i) {
    auto& p = progress_;
    auto& h = heroes_[i];
    if (p.step >= p.steps.size() || h.status != HeroStatus::Active) {  // done, or fell into a trap
        finish_hero_turn(i);
        return false;
    }
    const auto& st = p.steps[p.step];
    if (!p.announced) {
        p.announced = true;
        emit({.kind = Event::Kind::HalfUsed, .actor = h.sheet.id, .detail = st.card,
              .value = st.half == cards::Half::Top ? 0 : 1, .flag = st.basic});
    }
    if (p.action >= st.actions.size()) {
        ++p.step;
        p.action = 0;
        p.announced = false;
        return false;
    }
    const auto& a = st.actions[p.action];
    if (!p.action_started) {
        p.action_started = true;
        p.chosen.clear();
    }

    Decision d;
    d.actor = h.sheet.id;
    d.action = a;

    if (a.type == ActionType::Move) {
        if (has_effect(h.conditions, &ConditionEffects::no_move)) {
            complete_action();
            return false;
        }
        d.kind = Decision::Kind::Move;
        d.hexes = hero_move_options(i, a);
        return set_decision(std::move(d));
    }

    if (a.target == TargetKind::Self) {
        if (a.type == ActionType::Heal) heal_hero(h.sheet.id, i, a.value);
        apply_conditions(h.sheet.id, h.conditions, a.conditions, true, nullptr);
        complete_action();
        return false;
    }

    auto options = hero_target_options(i, a);
    if (options.empty() || static_cast<int>(p.chosen.size()) >= a.targets) {
        if (p.chosen.empty()) {
            emit({.kind = Event::Kind::NoTarget, .actor = h.sheet.id, .detail = std::string(cards::action_type_id(a.type))});
        }
        complete_action();
        return false;
    }
    options.emplace_back();  // stop choosing
    d.kind = Decision::Kind::Target;
    d.targets = std::move(options);
    return set_decision(std::move(d));
}

void Combat::finish_hero_turn(std::size_t i) {
    auto& h = heroes_[i];
    if (progress_.play && h.cards.selection()) h.cards.finish_turn(*progress_.play, content_->cards);
    tick_conditions(h.sheet.id, h.conditions);
    progress_ = {};
    ++turn_;
}

bool Combat::step_end_of_round() {
    const auto& rules = content_->rules;
    if (!end_resolved_) {
        end_resolved_ = true;
        for (auto& h : heroes_) {
            if (h.status != HeroStatus::Dying) continue;
            const auto roll = dice::roll_d20(rng_);
            if (roll.natural_20()) {
                h.status = HeroStatus::Active;
                h.death_successes = h.death_failures = 0;
                h.sheet.current_hp = 1;
                emit({.kind = Event::Kind::DeathSave, .target = h.sheet.id, .roll = roll.kept, .flag = true});
                emit({.kind = Event::Kind::HeroRevived, .target = h.sheet.id});
                continue;
            }
            const bool success = roll.kept >= rules.death_saves.dc;
            if (success) ++h.death_successes;
            else h.death_failures += roll.natural_1() ? 2 : 1;
            emit({.kind = Event::Kind::DeathSave, .target = h.sheet.id, .value = h.death_successes,
                  .value2 = h.death_failures, .roll = roll.kept, .flag = success});
            if (h.death_failures >= rules.death_saves.failures_to_die) {
                h.status = HeroStatus::Dead;
                emit({.kind = Event::Kind::HeroDied, .target = h.sheet.id});
            } else if (h.death_successes >= rules.death_saves.successes_to_stabilize) {
                h.status = HeroStatus::Stable;
                emit({.kind = Event::Kind::HeroStabilized, .target = h.sheet.id});
            }
        }
        for (auto& h : heroes_) {
            if (h.on_board() && !h.acted) tick_conditions(h.sheet.id, h.conditions);
        }
        for (auto& m : monsters_) {
            if (m.alive && !m.acted) tick_conditions(m.id, m.conditions);
        }
        for (auto& deck : decks_) {
            if (deck.current < 0) continue;
            const auto& card = content_->monsters.get(deck.type).deck[static_cast<std::size_t>(deck.current)];
            deck.discard.push_back(deck.current);
            deck.current = -1;
            if (card.reshuffle) {
                deck.draw.insert(deck.draw.end(), deck.discard.begin(), deck.discard.end());
                deck.discard.clear();
                rng_.shuffle(std::span<int>(deck.draw));
            }
        }
        cursor_ = 0;
        check_outcome();
        if (finished()) return false;
    }

    while (cursor_ < heroes_.size()) {
        const auto& h = heroes_[cursor_];
        if (h.status == HeroStatus::Active && !h.long_rested && h.cards.can_short_rest(rules.cards)) {
            Decision d;
            d.kind = Decision::Kind::ShortRest;
            d.actor = h.sheet.id;
            return set_decision(std::move(d));
        }
        ++cursor_;
    }
    emit({.kind = Event::Kind::RoundEnded, .value = round_});
    begin_round();
    return false;
}

// --- Monsters -----------------------------------------------------------------

std::optional<std::size_t> Combat::monster_focus(std::size_t mi, int range) const {
    const auto& m = monsters_[mi];
    const auto& map = scenario_.map;
    DistanceField dist;
    dist.compute(map, m.hex, [&](Hex h) { return monster_can_enter(h); });
    std::optional<std::size_t> best;
    std::tuple<int, int, std::size_t> best_key{kFar, kFar, 0};
    for (std::size_t i = 0; i < heroes_.size(); ++i) {
        const auto& h = heroes_[i];
        if (h.status != HeroStatus::Active) continue;
        int cost = kFar;
        for (int k = 0; k < dist.size(); ++k) {
            const int d = dist.at_index(k);
            if (d < 0 || d >= cost) continue;
            const Hex x = map.hex_at(k);
            if (distance(x, h.hex) <= range && (x == m.hex || !occupied(x)) && line_of_sight(map, x, h.hex)) cost = d;
        }
        const std::tuple<int, int, std::size_t> key{cost, distance(m.hex, h.hex), i};
        if (!best || key < best_key) {
            best = i;
            best_key = key;
        }
    }
    return best;
}

Hex Combat::monster_destination(std::size_t mi, std::size_t target, int steps, int range, bool jump) const {
    const auto& m = monsters_[mi];
    const auto& map = scenario_.map;
    const Hex goal = heroes_[target].hex;
    auto can_enter = [&](Hex h) { return monster_can_enter(h); };
    DistanceField to_goal;
    to_goal.compute(map, goal, can_enter);

    Hex best = m.hex;
    std::tuple<int, int, int> best_key{kFar, kFar, kFar};
    auto consider = [&](Hex h, int cost) {
        if (h != m.hex && occupied(h)) return;
        const bool in_range = in_reach(h, goal, range);
        // In range: fewest steps. Otherwise: closest walking distance to the hero.
        const int walk = to_goal.at(map, h);
        const std::tuple<int, int, int> key = in_range ? std::tuple{0, cost, 0}
                                                       : std::tuple{1, walk < 0 ? kFar : walk, distance(h, goal)};
        if (key < best_key || (key == best_key && h < best)) {
            best = h;
            best_key = key;
        }
    };
    if (jump) {
        for (Hex h : map.hexes()) {
            if (map.walkable(h) && monster_can_enter(h) && distance(m.hex, h) <= steps) consider(h, distance(m.hex, h));
        }
    } else {
        DistanceField from_monster;
        from_monster.compute(map, m.hex, can_enter, steps);
        for (int k = 0; k < from_monster.size(); ++k) {
            if (from_monster.at_index(k) >= 0) consider(map.hex_at(k), from_monster.at_index(k));
        }
    }
    return best;
}

void Combat::run_monster_turn(std::size_t mi) {
    auto& m = monsters_[mi];
    if (!m.alive) return;
    m.acted = true;
    emit({.kind = Event::Kind::TurnStarted, .actor = m.id});
    if (has_effect(m.conditions, &ConditionEffects::skip_turn)) {
        emit({.kind = Event::Kind::TurnSkipped, .actor = m.id, .detail = "stunned"});
        tick_conditions(m.id, m.conditions);
        return;
    }

    const auto& def = def_of(m);
    int current = -1;
    for (const auto& deck : decks_) {
        if (deck.type == m.type) current = deck.current;
    }
    const auto& card = def.deck[static_cast<std::size_t>(current)];

    for (std::size_t k = 0; k < card.actions.size() && !finished(); ++k) {
        const auto& a = card.actions[k];
        switch (a.type) {
            case ActionType::Move: {
                if (has_effect(m.conditions, &ConditionEffects::no_move)) break;
                int range = 1;
                for (std::size_t n = k + 1; n < card.actions.size(); ++n) {
                    if (card.actions[n].type == ActionType::Attack) {
                        range = card.actions[n].range;
                        break;
                    }
                }
                const auto target = monster_focus(mi, range);
                if (!target) break;
                const Hex to = monster_destination(mi, *target, a.value, range, a.jump);
                if (to != m.hex) {
                    const auto path = a.jump ? std::vector<Hex>{to}
                                             : find_path(scenario_.map, m.hex, to, [&](Hex x) { return monster_can_enter(x); });
                    walk(false, mi, path, a.jump, false);
                }
                break;
            }
            case ActionType::Attack: {
                std::vector<std::pair<int, std::size_t>> in_range;
                for (std::size_t i = 0; i < heroes_.size(); ++i) {
                    const auto& h = heroes_[i];
                    if (h.status == HeroStatus::Active && in_reach(m.hex, h.hex, a.range)) {
                        in_range.emplace_back(distance(m.hex, h.hex), i);
                    }
                }
                std::sort(in_range.begin(), in_range.end());
                for (std::size_t n = 0; n < in_range.size() && static_cast<int>(n) < a.targets && !finished(); ++n) {
                    monster_attacks_hero(mi, in_range[n].second, a);
                }
                break;
            }
            case ActionType::Heal:
                heal_monster(mi, a.value);
                apply_conditions(m.id, m.conditions, a.conditions, true, &def);
                break;
            case ActionType::Condition:
                if (a.target == TargetKind::Enemy) {
                    std::optional<std::size_t> pick;
                    for (std::size_t i = 0; i < heroes_.size(); ++i) {
                        const auto& h = heroes_[i];
                        if (h.status != HeroStatus::Active || !in_reach(m.hex, h.hex, a.range)) continue;
                        if (!pick || distance(m.hex, h.hex) < distance(m.hex, heroes_[*pick].hex)) pick = i;
                    }
                    if (pick) apply_conditions(heroes_[*pick].sheet.id, heroes_[*pick].conditions, a.conditions, false, nullptr);
                } else {
                    apply_conditions(m.id, m.conditions, a.conditions, true, &def);
                }
                break;
        }
    }
    if (m.alive && !finished()) tick_conditions(m.id, m.conditions);
}

// --- Rules helpers --------------------------------------------------------------

const character::ClassDef& Combat::class_of(const HeroState& h) const {
    return content_->classes.get(h.sheet.class_id);
}

const monsters::MonsterDef& Combat::def_of(const MonsterState& m) const {
    return content_->monsters.get(m.type);
}

bool Combat::has_effect(const std::vector<ActiveCondition>& conds, bool ConditionEffects::*flag) const {
    for (const auto& c : conds) {
        const auto* def = content_->rules.find_condition(c.id);
        if (def && def->effects.*flag) return true;
    }
    return false;
}

int Combat::sum_effect(const std::vector<ActiveCondition>& conds, int ConditionEffects::*field) const {
    int total = 0;
    for (const auto& c : conds) {
        if (const auto* def = content_->rules.find_condition(c.id)) total += def->effects.*field;
    }
    return total;
}

bool Combat::hero_occupies(Hex h) const {
    return std::any_of(heroes_.begin(), heroes_.end(), [&](const HeroState& x) { return x.on_board() && x.hex == h; });
}

bool Combat::monster_occupies(Hex h) const {
    return std::any_of(monsters_.begin(), monsters_.end(), [&](const MonsterState& x) { return x.alive && x.hex == h; });
}

std::vector<Hex> Combat::hero_move_options(std::size_t i, const cards::ActionDef& a) const {
    const Hex from = heroes_[i].hex;
    if (a.jump) {
        std::vector<Hex> out;
        for (Hex h : scenario_.map.hexes()) {
            if (scenario_.map.walkable(h) && distance(from, h) <= a.value && (h == from || !occupied(h))) out.push_back(h);
        }
        return out;
    }
    return reachable(
        scenario_.map, from, a.value, [&](Hex h) { return !monster_occupies(h); }, [&](Hex h) { return !occupied(h); });
}

std::vector<std::string> Combat::hero_target_options(std::size_t i, const cards::ActionDef& a) const {
    const Hex from = heroes_[i].hex;
    const auto& chosen = progress_.chosen;
    auto taken = [&](const std::string& id) { return std::find(chosen.begin(), chosen.end(), id) != chosen.end(); };
    std::vector<std::string> out;
    if (a.target == TargetKind::Enemy) {
        for (const auto& m : monsters_) {
            if (m.alive && in_reach(from, m.hex, a.range) && !taken(m.id)) out.push_back(m.id);
        }
    } else {
        for (const auto& h : heroes_) {
            if (h.on_board() && in_reach(from, h.hex, a.range) && !taken(h.sheet.id)) out.push_back(h.sheet.id);
        }
    }
    return out;
}

void Combat::hero_attacks_monster(std::size_t hi, std::size_t mi, const cards::ActionDef& a) {
    const auto& h = heroes_[hi];
    const auto& cls = class_of(h);
    auto& m = monsters_[mi];
    const auto& def = def_of(m);
    const int attr = h.sheet.attributes.mod(cls.primary);
    const int modifier = attr + content_->rules.proficiency_bonus(h.sheet.level);
    const auto mode = dice::combine_mode(a.advantage || has_effect(h.conditions, &ConditionEffects::attack_advantage),
                                         has_effect(h.conditions, &ConditionEffects::attack_disadvantage));
    const auto check = dice::roll_check(rng_, modifier, def.armor_class, mode);
    emit({.kind = Event::Kind::AttackRolled, .actor = h.sheet.id, .target = m.id,
          .value = mode == dice::RollMode::Advantage ? 1 : mode == dice::RollMode::Disadvantage ? -1 : 0,
          .roll = check.roll.kept, .total = check.total, .target_number = check.target, .flag = check.success,
          .critical = check.critical});
    if (!check.success) return;

    const int base = check.critical ? a.value * 2 : a.value;
    const int dmg = std::max(0, base + attr + sum_effect(m.conditions, &ConditionEffects::damage_taken_bonus) -
                                    sum_effect(m.conditions, &ConditionEffects::damage_reduction));
    damage_monster(mi, dmg);
    if (!m.alive) return;
    apply_conditions(m.id, m.conditions, a.conditions, false, &def);
    if (a.push > 0) force_move(mi, h.hex, a.push, true);
    if (a.pull > 0) force_move(mi, h.hex, a.pull, false);
}

void Combat::monster_attacks_hero(std::size_t mi, std::size_t hi, const cards::ActionDef& a) {
    const auto& m = monsters_[mi];
    const auto& def = def_of(m);
    auto& h = heroes_[hi];
    const int ac = character::armor_class(h.sheet, class_of(h));
    const auto mode = dice::combine_mode(a.advantage || has_effect(m.conditions, &ConditionEffects::attack_advantage),
                                         has_effect(m.conditions, &ConditionEffects::attack_disadvantage));
    const auto check = dice::roll_check(rng_, def.attack_bonus, ac, mode);
    emit({.kind = Event::Kind::AttackRolled, .actor = m.id, .target = h.sheet.id,
          .value = mode == dice::RollMode::Advantage ? 1 : mode == dice::RollMode::Disadvantage ? -1 : 0,
          .roll = check.roll.kept, .total = check.total, .target_number = check.target, .flag = check.success,
          .critical = check.critical});
    if (!check.success) return;

    const int base = check.critical ? a.value * 2 : a.value;
    const int dmg = std::max(0, base + def.damage_bonus + sum_effect(h.conditions, &ConditionEffects::damage_taken_bonus) -
                                    sum_effect(h.conditions, &ConditionEffects::damage_reduction));
    damage_hero(hi, dmg, check.critical);
    if (h.status == HeroStatus::Active) apply_conditions(h.sheet.id, h.conditions, a.conditions, false, nullptr);
}

void Combat::damage_monster(std::size_t mi, int amount) {
    auto& m = monsters_[mi];
    m.hp = std::max(0, m.hp - amount);
    emit({.kind = Event::Kind::Damaged, .target = m.id, .value = amount, .value2 = m.hp});
    if (m.hp == 0) {
        m.alive = false;
        m.conditions.clear();
        emit({.kind = Event::Kind::MonsterDied, .target = m.id});
        check_outcome();
    }
}

void Combat::damage_hero(std::size_t hi, int amount, bool /*critical*/) {
    auto& h = heroes_[hi];
    if (h.status != HeroStatus::Active) return;
    h.sheet.current_hp = std::max(0, h.sheet.current_hp - amount);
    emit({.kind = Event::Kind::Damaged, .target = h.sheet.id, .value = amount, .value2 = h.sheet.current_hp});
    if (h.sheet.current_hp == 0) {
        h.status = HeroStatus::Dying;
        h.death_successes = h.death_failures = 0;
        h.conditions.clear();
        emit({.kind = Event::Kind::HeroDowned, .target = h.sheet.id});
        knock_out_selection(hi);
        check_outcome();
    }
}

void Combat::heal_hero(const std::string& healer, std::size_t hi, int amount) {
    auto& h = heroes_[hi];
    if (!h.on_board()) return;
    if (h.status == HeroStatus::Dying || h.status == HeroStatus::Stable) {
        h.status = HeroStatus::Active;
        h.death_successes = h.death_failures = 0;
        emit({.kind = Event::Kind::HeroRevived, .target = h.sheet.id});
    }
    const int before = h.sheet.current_hp;
    h.sheet.current_hp = std::min(character::max_hp(h.sheet, class_of(h)), before + amount);
    emit({.kind = Event::Kind::Healed, .actor = healer, .target = h.sheet.id, .value = h.sheet.current_hp - before,
          .value2 = h.sheet.current_hp});
}

void Combat::heal_monster(std::size_t mi, int amount) {
    auto& m = monsters_[mi];
    const int before = m.hp;
    m.hp = std::min(def_of(m).hit_points, m.hp + amount);
    emit({.kind = Event::Kind::Healed, .actor = m.id, .target = m.id, .value = m.hp - before, .value2 = m.hp});
}

void Combat::apply_conditions(const std::string& target, std::vector<ActiveCondition>& conds,
                              const std::vector<cards::ConditionApply>& list, bool own_turn,
                              const monsters::MonsterDef* immune) {
    for (const auto& c : list) {
        if (immune && immune->immune_to(c.id)) {
            emit({.kind = Event::Kind::ConditionResisted, .target = target, .detail = c.id});
            continue;
        }
        const auto it = std::find_if(conds.begin(), conds.end(), [&](const ActiveCondition& x) { return x.id == c.id; });
        if (it != conds.end()) {
            it->rounds = std::max(it->rounds, c.rounds);
            it->fresh = it->fresh || own_turn;
        } else {
            conds.push_back({c.id, c.rounds, own_turn});
        }
        emit({.kind = Event::Kind::ConditionApplied, .target = target, .detail = c.id, .value = c.rounds});
    }
}

void Combat::tick_conditions(const std::string& who, std::vector<ActiveCondition>& conds) {
    for (auto it = conds.begin(); it != conds.end();) {
        if (it->fresh) {
            it->fresh = false;
            ++it;
            continue;
        }
        if (--it->rounds <= 0) {
            emit({.kind = Event::Kind::ConditionEnded, .target = who, .detail = it->id});
            it = conds.erase(it);
        } else {
            ++it;
        }
    }
}

void Combat::force_move(std::size_t mi, Hex attacker, int steps, bool push) {
    const auto& map = scenario_.map;
    Hex cur = monsters_[mi].hex;
    std::vector<Hex> path;
    for (int s = 0; s < steps; ++s) {
        const int here = distance(attacker, cur);
        std::optional<Hex> best;
        for (Hex n : neighbors(cur)) {
            // Can be pushed into traps, but not through closed doors.
            if (!map.walkable(n) || map.terrain(n) == Terrain::Door || occupied(n) || n == attacker) continue;
            const int d = distance(attacker, n);
            if (push ? d <= here : d >= here) continue;
            if (!best || n < *best) best = n;
        }
        if (!best) break;
        path.push_back(*best);
        cur = *best;
    }
    walk(false, mi, path, false, true);
}

void Combat::walk(bool hero, std::size_t index, const std::vector<Hex>& path, bool jump, bool forced) {
    auto& map = scenario_.map;
    const std::string id = hero ? heroes_[index].sheet.id : monsters_[index].id;
    auto pos = [&]() -> Hex& { return hero ? heroes_[index].hex : monsters_[index].hex; };
    auto standing = [&] { return hero ? heroes_[index].status == HeroStatus::Active : monsters_[index].alive; };

    // Moved events are split at doors and traps so a frontend can animate each part.
    Hex segment_from = pos();
    std::vector<Hex> segment;
    auto flush = [&] {
        if (segment.empty()) return;
        emit({.kind = Event::Kind::Moved, .actor = id, .flag = forced, .critical = jump, .from = segment_from,
              .to = segment.back(), .path = segment});
        segment_from = segment.back();
        segment.clear();
    };
    for (std::size_t k = 0; k < path.size(); ++k) {
        const Hex h = path[k];
        const bool landing = k + 1 == path.size();
        pos() = h;
        segment.push_back(h);
        const auto t = map.terrain(h);
        if (t == Terrain::Door && hero) {
            flush();
            map.set_terrain(h, Terrain::Open);
            emit({.kind = Event::Kind::DoorOpened, .actor = id, .to = h});
        } else if (t == Terrain::Trap && (!jump || landing)) {
            flush();
            trigger_trap(h, hero, index);
            if (!standing() || finished()) return;
        }
    }
    flush();
}

void Combat::trigger_trap(Hex h, bool hero, std::size_t index) {
    const auto* def = scenario_.trap_at(h);
    if (!def) return;
    const scenario::TrapDef trap = *def;
    scenario_.traps.erase(std::find_if(scenario_.traps.begin(), scenario_.traps.end(),
                                       [&](const scenario::TrapDef& t) { return t.hex == h; }));
    scenario_.map.set_terrain(h, Terrain::Open);
    if (hero) {
        auto& target = heroes_[index];
        emit({.kind = Event::Kind::TrapTriggered, .target = target.sheet.id, .value = trap.damage, .to = h});
        if (trap.damage > 0) damage_hero(index, trap.damage, false);
        if (target.status == HeroStatus::Active) apply_conditions(target.sheet.id, target.conditions, trap.conditions, false, nullptr);
    } else {
        auto& target = monsters_[index];
        emit({.kind = Event::Kind::TrapTriggered, .target = target.id, .value = trap.damage, .to = h});
        if (trap.damage > 0) damage_monster(index, trap.damage);
        if (target.alive) apply_conditions(target.id, target.conditions, trap.conditions, false, &def_of(target));
    }
}

bool Combat::monster_can_enter(Hex h) const {
    const auto t = scenario_.map.terrain(h);
    return t != Terrain::Door && t != Terrain::Trap && !hero_occupies(h);
}

bool Combat::in_reach(Hex from, Hex to, int range) const {
    return distance(from, to) <= range && line_of_sight(scenario_.map, from, to);
}

void Combat::knock_out_selection(std::size_t hi) {
    auto& h = heroes_[hi];
    if (h.cards.selection()) h.cards.skip_turn();
}

void Combat::check_outcome() {
    if (finished()) return;
    if (std::none_of(monsters_.begin(), monsters_.end(), [](const MonsterState& m) { return m.alive; })) {
        outcome_ = Outcome::Victory;
        emit({.kind = Event::Kind::Victory});
    } else if (std::none_of(heroes_.begin(), heroes_.end(),
                            [](const HeroState& h) { return h.status == HeroStatus::Active; })) {
        outcome_ = Outcome::Defeat;
        emit({.kind = Event::Kind::Defeat});
    }
    if (finished()) {
        phase_ = Phase::Finished;
        pending_.reset();
    }
}

std::optional<std::size_t> Combat::hero_index(const std::string& id) const {
    for (std::size_t i = 0; i < heroes_.size(); ++i) {
        if (heroes_[i].sheet.id == id) return i;
    }
    return std::nullopt;
}

std::optional<std::size_t> Combat::monster_index(const std::string& id) const {
    for (std::size_t i = 0; i < monsters_.size(); ++i) {
        if (monsters_[i].id == id) return i;
    }
    return std::nullopt;
}

std::optional<int> Combat::initiative_of(const std::string& id) const {
    for (const auto& e : order_) {
        if (e.hero ? heroes_[e.index].sheet.id == id : monsters_[e.index].id == id) return e.initiative;
    }
    return std::nullopt;
}

std::string_view event_kind_id(Event::Kind k) {
    switch (k) {
        case Event::Kind::RoundStarted: return "round_started";
        case Event::Kind::CardsSelected: return "cards_selected";
        case Event::Kind::MonsterCardDrawn: return "monster_card_drawn";
        case Event::Kind::TurnStarted: return "turn_started";
        case Event::Kind::TurnSkipped: return "turn_skipped";
        case Event::Kind::HalfUsed: return "half_used";
        case Event::Kind::NoTarget: return "no_target";
        case Event::Kind::Moved: return "moved";
        case Event::Kind::DoorOpened: return "door_opened";
        case Event::Kind::TrapTriggered: return "trap_triggered";
        case Event::Kind::AttackRolled: return "attack_rolled";
        case Event::Kind::Damaged: return "damaged";
        case Event::Kind::Healed: return "healed";
        case Event::Kind::ConditionApplied: return "condition_applied";
        case Event::Kind::ConditionResisted: return "condition_resisted";
        case Event::Kind::ConditionEnded: return "condition_ended";
        case Event::Kind::MonsterDied: return "monster_died";
        case Event::Kind::HeroDowned: return "hero_downed";
        case Event::Kind::DeathSave: return "death_save";
        case Event::Kind::HeroStabilized: return "hero_stabilized";
        case Event::Kind::HeroRevived: return "hero_revived";
        case Event::Kind::HeroDied: return "hero_died";
        case Event::Kind::HeroExhausted: return "hero_exhausted";
        case Event::Kind::CardLost: return "card_lost";
        case Event::Kind::LongRested: return "long_rested";
        case Event::Kind::ShortRested: return "short_rested";
        case Event::Kind::RoundEnded: return "round_ended";
        case Event::Kind::Victory: return "victory";
        case Event::Kind::Defeat: return "defeat";
    }
    return "?";
}

}  // namespace rpg::combat
