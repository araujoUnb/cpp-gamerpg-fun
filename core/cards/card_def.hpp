#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rpg::cards {

enum class ActionType { Move, Attack, Heal, Condition };
enum class TargetKind { Enemy, Self, Ally };  // Ally includes the user

std::string_view action_type_id(ActionType t);
std::string_view target_kind_id(TargetKind t);

struct ConditionApply {
    std::string id;  // a condition of the ruleset
    int rounds = 1;

    friend bool operator==(const ConditionApply&, const ConditionApply&) = default;
};

// One step of a card half. Which fields matter depends on the type:
//   move:      value, jump
//   attack:    value (base damage), range, targets, advantage, push, pull, conditions
//   heal:      value, range, targets, target (self/ally), conditions
//   condition: range, targets, target, conditions
// The combat module gives these their meaning; this module only stores and validates them.
struct ActionDef {
    ActionType type = ActionType::Move;
    int value = 0;
    int range = 0;    // 0: self, 1: adjacent
    int targets = 1;
    TargetKind target = TargetKind::Self;
    bool jump = false;
    bool advantage = false;
    int push = 0;
    int pull = 0;
    std::vector<ConditionApply> conditions;

    friend bool operator==(const ActionDef&, const ActionDef&) = default;
};

struct CardHalf {
    std::vector<ActionDef> actions;
    bool lose = false;  // using this half sends the card to the lost pile
};

enum class Half { Top, Bottom };

struct CardDef {
    std::string id;        // "fighter.steady_strike"
    std::string class_id;
    int level = 1;         // hero level that unlocks it
    std::string name_key;  // text key
    int initiative = 50;
    CardHalf top;
    CardHalf bottom;

    const CardHalf& half(Half h) const { return h == Half::Top ? top : bottom; }
};

}  // namespace rpg::cards
