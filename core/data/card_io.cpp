#include "data/card_io.hpp"

#include "data/json_helpers.hpp"
#include "data/json_io.hpp"

#include <algorithm>
#include <stdexcept>

namespace rpg::data {

using namespace detail;
using cards::ActionType;
using cards::TargetKind;

namespace {

ActionType action_type_from_json(const json& j, const std::string& where) {
    const auto id = get_string(j, "type", where);
    for (auto t : {ActionType::Move, ActionType::Attack, ActionType::Heal, ActionType::Condition}) {
        if (cards::action_type_id(t) == id) return t;
    }
    fail(where, "unknown action type '" + id + "'");
}

TargetKind target_from_json(const json& j, const std::string& where) {
    const auto id = get_string(j, "target", where);
    for (auto t : {TargetKind::Enemy, TargetKind::Self, TargetKind::Ally}) {
        if (cards::target_kind_id(t) == id) return t;
    }
    fail(where, "unknown target '" + id + "'");
}

std::vector<cards::ConditionApply> conditions_from_json(const json& j, const character::Ruleset& rules,
                                                        const std::string& where) {
    if (!j.contains("conditions")) return {};
    return condition_list_from_json(get_array(j, "conditions", where), rules, where + ".conditions");
}

void require_at_least(int value, int min, const char* key, const std::string& where) {
    if (value < min) fail(where, quote_key(key) + " must be at least " + std::to_string(min));
}

cards::CardHalf half_from_json(const json& j, const character::Ruleset& rules, const std::string& where) {
    only_keys(j, {"actions", "lose"}, where);
    cards::CardHalf half;
    half.lose = get_bool_or(j, "lose", false, where);
    const auto& actions = get_array(j, "actions", where);
    if (actions.empty()) fail(where, "a half needs at least one action");
    for (std::size_t i = 0; i < actions.size(); ++i) {
        half.actions.push_back(action_from_json(actions[i], rules, where + ".actions[" + std::to_string(i) + "]"));
    }
    return half;
}

}  // namespace

std::vector<cards::ConditionApply> condition_list_from_json(const json& arr, const character::Ruleset& rules,
                                                            const std::string& where) {
    if (!arr.is_array()) fail(where, "expected a list of conditions");
    std::vector<cards::ConditionApply> out;
    for (const auto& c : arr) {
        only_keys(c, {"id", "rounds"}, where);
        cards::ConditionApply apply;
        apply.id = get_string(c, "id", where);
        apply.rounds = get_int(c, "rounds", where);
        if (!rules.find_condition(apply.id)) fail(where, "unknown condition '" + apply.id + "'");
        if (apply.rounds < 1) fail(where, "'rounds' must be at least 1");
        out.push_back(std::move(apply));
    }
    return out;
}

cards::ActionDef action_from_json(const json& j, const character::Ruleset& rules, const std::string& where) {
    require_object(j, where);
    cards::ActionDef a;
    a.type = action_type_from_json(j, where);
    switch (a.type) {
        case ActionType::Move:
            only_keys(j, {"type", "value", "jump"}, where);
            a.value = get_int(j, "value", where);
            a.jump = get_bool_or(j, "jump", false, where);
            require_at_least(a.value, 1, "value", where);
            break;

        case ActionType::Attack:
            only_keys(j, {"type", "value", "range", "targets", "advantage", "push", "pull", "conditions"}, where);
            a.value = get_int(j, "value", where);
            a.range = get_int_or(j, "range", 1, where);
            a.targets = get_int_or(j, "targets", 1, where);
            a.target = TargetKind::Enemy;
            a.advantage = get_bool_or(j, "advantage", false, where);
            a.push = get_int_or(j, "push", 0, where);
            a.pull = get_int_or(j, "pull", 0, where);
            a.conditions = conditions_from_json(j, rules, where);
            require_at_least(a.value, 1, "value", where);
            require_at_least(a.range, 1, "range", where);
            require_at_least(a.targets, 1, "targets", where);
            require_at_least(a.push, 0, "push", where);
            require_at_least(a.pull, 0, "pull", where);
            if (a.push > 0 && a.pull > 0) fail(where, "an attack cannot both push and pull");
            break;

        case ActionType::Heal:
            only_keys(j, {"type", "value", "range", "targets", "target", "conditions"}, where);
            a.value = get_int(j, "value", where);
            a.target = j.contains("target") ? target_from_json(j, where) : TargetKind::Self;
            a.range = get_int_or(j, "range", 0, where);
            a.targets = get_int_or(j, "targets", 1, where);
            a.conditions = conditions_from_json(j, rules, where);
            require_at_least(a.value, 1, "value", where);
            require_at_least(a.range, 0, "range", where);
            require_at_least(a.targets, 1, "targets", where);
            if (a.target == TargetKind::Enemy) fail(where, "heal cannot target enemies");
            break;

        case ActionType::Condition:
            only_keys(j, {"type", "range", "targets", "target", "conditions"}, where);
            a.target = target_from_json(j, where);
            a.range = get_int_or(j, "range", a.target == TargetKind::Self ? 0 : 1, where);
            a.targets = get_int_or(j, "targets", 1, where);
            a.conditions = conditions_from_json(j, rules, where);
            require_at_least(a.range, 0, "range", where);
            require_at_least(a.targets, 1, "targets", where);
            if (a.conditions.empty()) fail(where, "a condition action needs at least one condition");
            break;
    }
    if (a.target == TargetKind::Self && (a.range != 0 || a.targets != 1)) {
        fail(where, "a self action has no range and exactly one target");
    }
    return a;
}

cards::CardDef card_from_json(const json& j, const character::Ruleset& rules, const std::string& class_id,
                              const std::string& where) {
    only_keys(j, {"id", "level", "name", "initiative", "top", "bottom"}, where);
    cards::CardDef c;
    c.id = get_string(j, "id", where);
    const std::string ctx = where + " [" + c.id + "]";
    if (c.id.rfind(class_id + ".", 0) != 0 || c.id.size() <= class_id.size() + 1) {
        fail(ctx, "card id must start with '" + class_id + ".'");
    }
    c.class_id = class_id;
    c.level = get_int(j, "level", ctx);
    c.name_key = get_string(j, "name", ctx);
    c.initiative = get_int(j, "initiative", ctx);
    if (c.level < 1 || c.level > rules.max_level()) fail(ctx, "'level' out of range");
    if (!rules.cards.initiative.contains(c.initiative)) fail(ctx, "'initiative' out of range");
    c.top = half_from_json(field(j, "top", ctx), rules, ctx + ".top");
    c.bottom = half_from_json(field(j, "bottom", ctx), rules, ctx + ".bottom");
    return c;
}

cards::CardRegistry load_cards(const std::filesystem::path& dir, const character::Ruleset& rules,
                               const ClassRegistry& classes) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(dir)) throw DataError(dir.string() + ": not a directory");

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    cards::CardRegistry registry;
    for (const auto& file : files) {
        const auto where = file.filename().string();
        const auto j = read_json_file(file);
        only_keys(j, {"class", "cards"}, where);
        const auto class_id = get_string(j, "class", where);
        if (!classes.find(class_id)) fail(where, "unknown class '" + class_id + "'");

        const auto& list = get_array(j, "cards", where);
        for (std::size_t i = 0; i < list.size(); ++i) {
            auto card = card_from_json(list[i], rules, class_id, where + " cards[" + std::to_string(i) + "]");
            try {
                registry.add(std::move(card));
            } catch (const std::invalid_argument& e) {
                fail(where, e.what());
            }
        }
    }
    return registry;
}

json to_json(const cards::RoundSelection& s) {
    if (s.long_rest) return json{{"long_rest", true}};
    return json{{"long_rest", false}, {"lead", s.lead}, {"other", s.other}};
}

json to_json(const cards::HeroCards& c) {
    return json{
        {"hand", c.hand()},
        {"discard", c.discard()},
        {"lost", c.lost()},
        {"selection", c.selection() ? to_json(*c.selection()) : json(nullptr)},
    };
}

cards::HeroCards hero_cards_from_json(const json& j, const std::vector<std::string>& deck, const std::string& where) {
    only_keys(j, {"hand", "discard", "lost", "selection"}, where);
    auto hand = string_list(field(j, "hand", where), where + ".hand");
    auto discard = string_list(field(j, "discard", where), where + ".discard");
    auto lost = string_list(field(j, "lost", where), where + ".lost");

    std::optional<cards::RoundSelection> selection;
    const auto& sel = field(j, "selection", where);
    if (!sel.is_null()) {
        const std::string ctx = where + ".selection";
        if (get_bool_or(sel, "long_rest", false, ctx)) {
            only_keys(sel, {"long_rest"}, ctx);
            selection = cards::RoundSelection::rest();
        } else {
            only_keys(sel, {"long_rest", "lead", "other"}, ctx);
            selection = cards::RoundSelection::play(get_string(sel, "lead", ctx), get_string(sel, "other", ctx));
        }
    }
    try {
        return cards::HeroCards::from_piles(deck, std::move(hand), std::move(discard), std::move(lost),
                                            std::move(selection));
    } catch (const std::invalid_argument& e) {
        fail(where, e.what());
    }
}

}  // namespace rpg::data
