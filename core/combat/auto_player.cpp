#include "combat/auto_player.hpp"

#include <algorithm>
#include <limits>

namespace rpg::combat {

using cards::ActionDef;
using cards::ActionType;
using cards::Half;
using cards::TargetKind;

namespace {

constexpr int kLossPenalty = 4;

const HeroState& hero_by_id(const Combat& c, const std::string& id) {
    for (const auto& h : c.heroes()) {
        if (h.sheet.id == id) return h;
    }
    return c.heroes().front();
}

int nearest_monster(const Combat& c, Hex from) {
    int best = std::numeric_limits<int>::max();
    for (const auto& m : c.monsters()) {
        if (m.alive) best = std::min(best, distance(from, m.hex));
    }
    return best;
}

bool someone_hurt(const Combat& c) {
    for (const auto& h : c.heroes()) {
        if (!h.on_board()) continue;
        const auto max = character::max_hp(h.sheet, c.content().classes.get(h.sheet.class_id));
        if (h.sheet.current_hp <= max - 3 || h.status != HeroStatus::Active) return true;
    }
    return false;
}

// What the scoring needs from the board, computed once per decision.
struct Eval {
    const character::CardRules& rules;
    int enemy_distance;  // from the deciding hero to the nearest monster
    bool hurt;           // some hero could use healing
};

int action_value(const Eval& e, const ActionDef& a) {
    switch (a.type) {
        case ActionType::Attack: return (a.value + static_cast<int>(a.conditions.size())) * a.targets + a.range / 2;
        case ActionType::Move: return a.value / 2;
        case ActionType::Heal: return e.hurt ? a.value * a.targets : 0;
        case ActionType::Condition: return 2 * a.targets;
    }
    return 0;
}

int actions_value(const Eval& e, const std::vector<ActionDef>& actions) {
    int v = 0;
    for (const auto& a : actions) v += action_value(e, a);
    return v;
}

int move_of(const std::vector<ActionDef>& actions) {
    int v = 0;
    for (const auto& a : actions) {
        if (a.type == ActionType::Move) v += a.value;
    }
    return v;
}

// Score of one half. Attacks that cannot reach anyone this turn (range plus
// the movement done before them) are worth nothing, and a wasted loss half
// costs extra.
int half_score(const Eval& e, const cards::CardDef& card, Half half, bool basic, int move_before) {
    if (basic) {
        if (half == Half::Bottom) return e.rules.basic_move / 2;
        return e.enemy_distance <= 1 + move_before ? e.rules.basic_attack : 0;
    }
    const auto& h = card.half(half);
    int v = 0;
    for (const auto& a : h.actions) {
        if (a.type != ActionType::Attack || e.enemy_distance <= a.range + move_before) v += action_value(e, a);
    }
    return v - (h.lose ? kLossPenalty * (v > 0 ? 1 : 3) : 0);
}

int play_score(const Eval& e, const cards::CardDef& top, bool top_basic, const cards::CardDef& bottom, bool bottom_basic,
               bool top_first) {
    const int bottom_move = bottom_basic ? e.rules.basic_move : move_of(bottom.bottom.actions);
    // Moving can only help an attack that comes after it.
    return half_score(e, top, Half::Top, top_basic, top_first ? 0 : bottom_move) +
           half_score(e, bottom, Half::Bottom, bottom_basic, 0);
}

int play_score(const Eval& e, const cards::CardRegistry& reg, const cards::TurnPlay& p) {
    return play_score(e, reg.get(p.top.card), p.top.basic, reg.get(p.bottom.card), p.bottom.basic, p.top_first);
}

// A half never scores less with more movement before it, so moving first is
// always at least as good and only that order needs scoring.
int best_play_score(const Eval& e, const cards::CardDef& a, const cards::CardDef& b) {
    int best = std::numeric_limits<int>::min();
    for (const auto& [top, bottom] : {std::pair{&a, &b}, std::pair{&b, &a}}) {
        for (bool tb : {false, true}) {
            for (bool bb : {false, true}) best = std::max(best, play_score(e, *top, tb, *bottom, bb, false));
        }
    }
    return best;
}

// Position of the card `id` in the hand, or hand.size().
std::size_t hand_slot(const std::vector<std::string>& hand, const std::string& id) {
    return static_cast<std::size_t>(std::find(hand.begin(), hand.end(), id) - hand.begin());
}

// Range of the first attack still to come in the hero's turn, or 0.
int attack_range(const std::vector<ActionDef>& actions) {
    for (const auto& a : actions) {
        if (a.type == ActionType::Attack) return a.range;
    }
    return 0;
}

}  // namespace

std::size_t AutoPlayer::choose(const Combat& c, const Decision& d) const {
    const auto& hero = hero_by_id(c, d.actor);
    const auto& reg = c.content().cards;
    const Eval eval{c.content().rules.cards, nearest_monster(c, hero.hex), someone_hurt(c)};
    std::size_t best = 0;
    long best_score = std::numeric_limits<long>::min();
    auto consider = [&](std::size_t i, long score) {
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    };

    switch (d.kind) {
        case Decision::Kind::SelectCards: {
            // Both orders of a pair have the same best play: score each pair
            // once, with the hand's cards looked up once.
            const auto& hand = hero.cards.hand();
            const std::size_t n = hand.size();
            std::vector<const cards::CardDef*> defs;
            defs.reserve(n);
            for (const auto& id : hand) defs.push_back(&reg.get(id));
            std::vector<int> pair_score(n * n, std::numeric_limits<int>::min());
            for (std::size_t i = 0; i < d.selections.size(); ++i) {
                const auto& s = d.selections[i];
                if (s.long_rest) {
                    consider(i, n < 2 ? 1000 : -1000);
                    continue;
                }
                const std::size_t a = hand_slot(hand, s.lead), b = hand_slot(hand, s.other);
                const auto& lead = a < n ? *defs[a] : reg.get(s.lead);
                const auto& other = b < n ? *defs[b] : reg.get(s.other);
                int score = 0;
                if (a < n && b < n) {
                    auto& slot = pair_score[std::min(a, b) * n + std::max(a, b)];
                    if (slot == std::numeric_limits<int>::min()) slot = best_play_score(eval, lead, other);
                    score = slot;
                } else {
                    score = best_play_score(eval, lead, other);
                }
                // Value of the best play, then prefer acting early.
                consider(i, static_cast<long>(score) * 100 - lead.initiative);
            }
            break;
        }

        case Decision::Kind::PlayTurn:
            for (std::size_t i = 0; i < d.plays.size(); ++i) {
                const auto& p = d.plays[i];
                // On equal scores, attack first when something is already in range.
                const int range = p.top.basic ? 1 : attack_range(reg.get(p.top.card).top.actions);
                const bool in_range = range > 0 && eval.enemy_distance <= range;
                consider(i, play_score(eval, reg, p) * 10 + (p.top_first == in_range ? 1 : 0));
            }
            break;

        case Decision::Kind::Move: {
            // STR and DEX classes close in; casters keep some distance.
            const auto& cls = c.content().classes.get(hero.sheet.class_id);
            const int want = (cls.primary == character::Attribute::Strength ||
                              cls.primary == character::Attribute::Dexterity) ? 1 : 3;
            for (std::size_t i = 0; i < d.hexes.size(); ++i) {
                const int dist = nearest_monster(c, d.hexes[i]);
                const bool trap = c.scenario().map.terrain(d.hexes[i]) == Terrain::Trap;
                consider(i, -std::abs(dist - want) * 10 - distance(hero.hex, d.hexes[i]) - (trap ? 100 : 0));
            }
            break;
        }

        case Decision::Kind::Target:
            for (std::size_t i = 0; i < d.targets.size(); ++i) {
                const auto& id = d.targets[i];
                if (id.empty()) {
                    consider(i, -1);  // stop only when nothing else is worth it
                    continue;
                }
                if (d.action && d.action->target == TargetKind::Enemy) {
                    for (const auto& m : c.monsters()) {
                        if (m.id == id) consider(i, 1000 - m.hp);  // finish the weakest
                    }
                } else {
                    for (const auto& h : c.heroes()) {
                        if (h.sheet.id != id) continue;
                        const auto max = character::max_hp(h.sheet, c.content().classes.get(h.sheet.class_id));
                        const bool useful = d.action->type != ActionType::Heal || h.sheet.current_hp < max ||
                                            h.status != HeroStatus::Active;
                        consider(i, useful ? 1000 - h.sheet.current_hp * 10 + (h.status != HeroStatus::Active ? 500 : 0)
                                           : -2);
                    }
                }
            }
            break;

        case Decision::Kind::LongRestLoss:
            for (std::size_t i = 0; i < d.cards.size(); ++i) {
                const auto& card = reg.get(d.cards[i]);
                consider(i, -(actions_value(eval, card.top.actions) + actions_value(eval, card.bottom.actions)));
            }
            break;

        case Decision::Kind::ShortRest:
            best = hero.cards.hand().size() <= 2 ? 1 : 0;
            break;
    }
    return best;
}

}  // namespace rpg::combat
