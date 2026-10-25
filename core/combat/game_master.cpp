// The tactical game master: scores where each monster ends its move and whom
// it attacks. The classic rule lives with the rest of the monster turn in
// combat.cpp; this file only runs when the profile is tactical.
#include "combat/combat.hpp"

#include <algorithm>
#include <limits>

namespace rpg::combat {

using cards::ActionDef;
using cards::ActionType;
using character::ConditionEffects;

namespace {

constexpr long kOne = dice::CheckOdds::kTotal;  // feature value meaning 1.0
constexpr long kNoScore = std::numeric_limits<long>::min();
constexpr int kFarWalk = 30;  // walking distance used when no hero can be reached

}  // namespace

std::optional<monsters::GameMasterDef> Combat::find_game_master(const data::Content& content, std::string_view id) {
    if (const auto* gm = content.game_masters.find(id)) return *gm;
    if (id == monsters::GameMasterDef::kClassic) return monsters::GameMasterDef{};
    return std::nullopt;
}

bool Combat::hero_heals(const HeroState& h) const {
    for (const auto& id : h.sheet.deck) {
        const auto& card = content_->cards.get(id);
        for (const auto* half : {&card.top, &card.bottom}) {
            for (const auto& a : half->actions) {
                if (a.type == ActionType::Heal && a.target != cards::TargetKind::Self) return true;
            }
        }
    }
    return false;
}

std::vector<long> Combat::gm_target_scores(std::size_t mi, const ActionDef& a) const {
    const auto& m = monsters_[mi];
    const auto& def = def_of(m);
    const auto& w = gm_.weights;
    std::vector<long> out(heroes_.size(), kNoScore);
    for (std::size_t i = 0; i < heroes_.size(); ++i) {
        const auto& h = heroes_[i];
        if (h.status != HeroStatus::Active) continue;
        const int hp = h.sheet.current_hp;
        long expected = 0, down = 0;
        if (a.type == ActionType::Attack) {
            const auto odds = dice::check_odds(def.attack_bonus, character::armor_class(h.sheet, class_of(h)),
                                               monster_roll_mode(m, a));
            const int extra = def.damage_bonus + sum_effect(h.conditions, &ConditionEffects::damage_taken_bonus) -
                              sum_effect(h.conditions, &ConditionEffects::damage_reduction);
            const int hit = std::max(0, a.value + extra);
            const int crit = std::max(0, 2 * a.value + extra);
            expected = static_cast<long>(odds.success) * hit + static_cast<long>(odds.critical) * (crit - hit);
            down = hit >= hp ? odds.success : crit >= hp ? odds.critical : 0;
        }
        const int max = character::max_hp(h.sheet, class_of(h));
        const long wounded = kOne * (max - hp) / std::max(1, max);
        const long healer = hero_heals(h) ? kOne : 0;
        long surround = 0;
        for (std::size_t j = 0; j < monsters_.size(); ++j) {
            if (j != mi && monsters_[j].alive && distance(monsters_[j].hex, h.hex) == 1) surround += kOne;
        }
        out[i] = w.expected_damage * expected + w.down_chance * down + w.wounded * wounded + w.healer * healer +
                 w.surround * surround;
    }
    return out;
}

Hex Combat::gm_destination(std::size_t mi, const ActionDef& move, const ActionDef* attack) const {
    const auto& m = monsters_[mi];
    const auto& map = scenario_.map;
    const auto& w = gm_.weights;
    const int range = attack ? attack->range : 1;
    const auto targets = static_cast<std::size_t>(attack ? std::max(1, attack->targets) : 0);
    const auto scores = attack ? gm_target_scores(mi, *attack) : std::vector<long>{};
    const long exposure = range > 1 ? w.exposure_ranged : w.exposure_melee;
    auto can_enter = [&](Hex h) { return monster_can_enter(h); };

    // Walking distance from each active hero, for the hexes that reach nobody.
    std::vector<DistanceField> walk;
    for (const auto& h : heroes_) {
        if (h.status != HeroStatus::Active) continue;
        walk.emplace_back();
        walk.back().compute(map, h.hex, can_enter);
    }

    Hex best = m.hex;
    long best_score = kNoScore;
    int best_cost = 0;
    std::vector<long> reach;
    auto consider = [&](Hex x, int cost) {
        if (x != m.hex && occupied(x)) return;
        long s = w.steps * kOne * cost;
        int adjacent = 0;
        for (const auto& h : heroes_) adjacent += h.status == HeroStatus::Active && distance(x, h.hex) == 1;
        s += exposure * kOne * adjacent;

        reach.clear();
        for (std::size_t i = 0; i < scores.size(); ++i) {
            if (scores[i] != kNoScore && in_reach(x, heroes_[i].hex, range)) reach.push_back(scores[i]);
        }
        if (!reach.empty()) {
            const auto n = std::min(targets, reach.size());
            std::partial_sort(reach.begin(), reach.begin() + static_cast<long>(n), reach.end(), std::greater<>());
            for (std::size_t k = 0; k < n; ++k) s += reach[k];
        } else {
            int nearest = kFarWalk;
            for (const auto& f : walk) {
                const int d = f.at(map, x);
                if (d >= 0) nearest = std::min(nearest, d);
            }
            s += w.approach * kOne * nearest;
        }
        if (s > best_score || (s == best_score && (cost < best_cost || (cost == best_cost && x < best)))) {
            best = x;
            best_score = s;
            best_cost = cost;
        }
    };

    if (move.jump) {
        for (Hex h : map.hexes()) {
            if (map.walkable(h) && monster_can_enter(h) && distance(m.hex, h) <= move.value) consider(h, distance(m.hex, h));
        }
        consider(m.hex, 0);
    } else {
        DistanceField from_monster;
        from_monster.compute(map, m.hex, can_enter, move.value);
        for (int k = 0; k < from_monster.size(); ++k) {
            if (from_monster.at_index(k) >= 0) consider(map.hex_at(k), from_monster.at_index(k));
        }
    }
    return best;
}

std::vector<std::size_t> Combat::gm_target_order(std::size_t mi, const ActionDef& a) const {
    const auto& m = monsters_[mi];
    const auto scores = gm_target_scores(mi, a);
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < heroes_.size(); ++i) {
        if (scores[i] != kNoScore && in_reach(m.hex, heroes_[i].hex, a.range)) out.push_back(i);
    }
    // Best score first; then the closest, then party order.
    std::sort(out.begin(), out.end(), [&](std::size_t x, std::size_t y) {
        if (scores[x] != scores[y]) return scores[x] > scores[y];
        const int dx = distance(m.hex, heroes_[x].hex), dy = distance(m.hex, heroes_[y].hex);
        return dx != dy ? dx < dy : x < y;
    });
    return out;
}

}  // namespace rpg::combat
