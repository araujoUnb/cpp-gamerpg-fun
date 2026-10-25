#include "cards/card_def.hpp"

namespace rpg::cards {

std::string_view action_type_id(ActionType t) {
    switch (t) {
        case ActionType::Move: return "move";
        case ActionType::Attack: return "attack";
        case ActionType::Heal: return "heal";
        case ActionType::Condition: return "condition";
    }
    return "?";
}

std::string_view target_kind_id(TargetKind t) {
    switch (t) {
        case TargetKind::Enemy: return "enemy";
        case TargetKind::Self: return "self";
        case TargetKind::Ally: return "ally";
    }
    return "?";
}

}  // namespace rpg::cards
