#pragma once

#include "combat/hex.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace rpg::combat {

// Everything that happens in a combat is reported as an event, in order.
// A frontend animates them one by one; tests and replays read them directly.
//
// Field use per kind (unused fields keep their defaults):
//   RoundStarted        value = round number
//   CardsSelected       actor, value = initiative, flag = long rest
//   MonsterCardDrawn    actor = monster type, detail = card id, value = initiative
//   TurnStarted         actor
//   TurnSkipped         actor, detail = reason ("stunned", "down")
//   HalfUsed            actor, detail = card id, value = 0 top / 1 bottom, flag = basic action
//   NoTarget            actor, detail = action type ("attack", "heal", "condition"): nothing in range
//   Moved               actor, from, to, path = hexes entered in order (ends at `to`),
//                       flag = forced (push/pull), critical = jump
//   DoorOpened          actor, to = door hex
//   TrapTriggered       target, to = trap hex, value = damage
//   AttackRolled        actor, target, roll = natural d20, total, target_number = AC,
//                       flag = hit, critical, value = 1 advantage / -1 disadvantage / 0
//   Damaged             target, value = damage, value2 = HP left
//   Healed              actor, target, value = HP healed, value2 = HP now
//   ConditionApplied    target, detail = condition id, value = rounds
//   ConditionResisted   target, detail = condition id (immune)
//   ConditionEnded      target, detail = condition id
//   MonsterDied         target
//   HeroDowned          target
//   DeathSave           target, roll, flag = success, value = successes, value2 = failures
//   HeroStabilized / HeroRevived / HeroDied / HeroExhausted    target
//   CardLost            actor, detail = card id
//   LongRested / ShortRested                                    actor
//   RoundEnded          value = round number
//   Victory / Defeat
struct Event {
    enum class Kind {
        RoundStarted, CardsSelected, MonsterCardDrawn, TurnStarted, TurnSkipped, HalfUsed, NoTarget, Moved,
        DoorOpened, TrapTriggered,
        AttackRolled, Damaged, Healed, ConditionApplied, ConditionResisted, ConditionEnded,
        MonsterDied, HeroDowned, DeathSave, HeroStabilized, HeroRevived, HeroDied, HeroExhausted,
        CardLost, LongRested, ShortRested, RoundEnded, Victory, Defeat
    };

    Kind kind = Kind::RoundStarted;
    std::string actor;
    std::string target;
    std::string detail;
    int value = 0;
    int value2 = 0;
    int roll = 0;
    int total = 0;
    int target_number = 0;
    bool flag = false;
    bool critical = false;
    Hex from;
    Hex to;
    std::vector<Hex> path;
};

std::string_view event_kind_id(Event::Kind k);

}  // namespace rpg::combat
