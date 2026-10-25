#include "data/json_io.hpp"

#include "data/data_error.hpp"
#include "data/json_helpers.hpp"

#include <fstream>
#include <set>

namespace rpg::data {

using character::Attribute;

using namespace detail;

json read_json_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) throw DataError(path.string() + ": cannot open file");
    try {
        return json::parse(in, nullptr, true, /*ignore_comments=*/true);
    } catch (const json::parse_error& e) {
        throw DataError(path.string() + ": " + e.what());
    }
}

character::Ruleset ruleset_from_json(const json& j, const std::string& where) {
    only_keys(j, {"attribute_score", "hand_size", "proficiency_by_level", "skills", "conditions", "cards",
                  "death_saves"}, where);
    character::Ruleset r;
    r.attribute_score = range_from_json(field(j, "attribute_score", where), where + ".attribute_score");
    r.hand_size = range_from_json(field(j, "hand_size", where), where + ".hand_size");

    r.proficiency_by_level.clear();
    for (const auto& v : get_array(j, "proficiency_by_level", where)) {
        if (!v.is_number_integer()) fail(where, "'proficiency_by_level' must hold integers");
        r.proficiency_by_level.push_back(v.get<int>());
    }
    if (r.proficiency_by_level.empty()) fail(where, "'proficiency_by_level' must not be empty");

    std::set<std::string> ids;
    for (const auto& s : get_array(j, "skills", where)) {
        const std::string ctx = where + ".skills";
        only_keys(s, {"id", "attribute", "name"}, ctx);
        character::SkillDef skill;
        skill.id = get_string(s, "id", ctx);
        skill.attribute = attribute_from_json(field(s, "attribute", ctx), ctx + " [" + skill.id + "]");
        skill.name_key = get_string(s, "name", ctx);
        if (!ids.insert(skill.id).second) fail(ctx, "duplicate skill '" + skill.id + "'");
        r.skills.push_back(std::move(skill));
    }

    ids.clear();
    for (const auto& c : get_array(j, "conditions", where)) {
        const std::string ctx = where + ".conditions";
        only_keys(c, {"id", "name", "description", "effects"}, ctx);
        character::ConditionDef cond;
        cond.id = get_string(c, "id", ctx);
        cond.name_key = get_string(c, "name", ctx);
        cond.description_key = get_string_or(c, "description", ctx);
        if (c.contains("effects")) {
            const std::string ectx = ctx + " [" + cond.id + "].effects";
            const auto& e = c["effects"];
            only_keys(e, {"damage_taken_bonus", "damage_reduction", "skip_turn", "no_move", "attack_advantage",
                          "attack_disadvantage"}, ectx);
            auto& fx = cond.effects;
            fx.damage_taken_bonus = get_int_or(e, "damage_taken_bonus", 0, ectx);
            fx.damage_reduction = get_int_or(e, "damage_reduction", 0, ectx);
            fx.skip_turn = get_bool_or(e, "skip_turn", false, ectx);
            fx.no_move = get_bool_or(e, "no_move", false, ectx);
            fx.attack_advantage = get_bool_or(e, "attack_advantage", false, ectx);
            fx.attack_disadvantage = get_bool_or(e, "attack_disadvantage", false, ectx);
            if (fx.damage_taken_bonus < 0 || fx.damage_reduction < 0) fail(ectx, "damage modifiers must not be negative");
        }
        if (!ids.insert(cond.id).second) fail(ctx, "duplicate condition '" + cond.id + "'");
        r.conditions.push_back(std::move(cond));
    }

    const std::string cctx = where + ".cards";
    const auto& cards = field(j, "cards", where);
    only_keys(cards, {"initiative", "basic_attack", "basic_move", "rest_min_discard", "long_rest_heal",
                      "long_rest_initiative"}, cctx);
    auto& cr = r.cards;
    cr.initiative = range_from_json(field(cards, "initiative", cctx), cctx + ".initiative");
    cr.basic_attack = get_int(cards, "basic_attack", cctx);
    cr.basic_move = get_int(cards, "basic_move", cctx);
    cr.rest_min_discard = get_int(cards, "rest_min_discard", cctx);
    cr.long_rest_heal = get_int(cards, "long_rest_heal", cctx);
    cr.long_rest_initiative = get_int(cards, "long_rest_initiative", cctx);
    if (cr.initiative.min < 0) fail(cctx, "'initiative.min' must not be negative");
    if (cr.basic_attack < 0 || cr.basic_move < 0 || cr.long_rest_heal < 0) {
        fail(cctx, "basic actions and healing must not be negative");
    }
    if (cr.rest_min_discard < 1) fail(cctx, "'rest_min_discard' must be at least 1");
    if (!cr.initiative.contains(cr.long_rest_initiative)) {
        fail(cctx, "'long_rest_initiative' must be inside the initiative range");
    }

    if (j.contains("death_saves")) {
        const std::string dctx = where + ".death_saves";
        const auto& d = j["death_saves"];
        only_keys(d, {"dc", "failures_to_die", "successes_to_stabilize"}, dctx);
        r.death_saves.dc = get_int(d, "dc", dctx);
        r.death_saves.failures_to_die = get_int(d, "failures_to_die", dctx);
        r.death_saves.successes_to_stabilize = get_int(d, "successes_to_stabilize", dctx);
        if (r.death_saves.failures_to_die < 1 || r.death_saves.successes_to_stabilize < 1) {
            fail(dctx, "counts must be at least 1");
        }
    }
    return r;
}

character::AttributeScores attributes_from_json(const json& j, const std::string& where) {
    if (!j.is_object()) fail(where, "expected an object");
    character::AttributeScores scores;
    for (auto a : character::kAllAttributes) {
        scores.set(a, get_int(j, std::string(character::attribute_id(a)).c_str(), where));
    }
    for (const auto& [key, _] : j.items()) {
        if (!character::parse_attribute(key)) fail(where, "unknown attribute '" + key + "'");
    }
    return scores;
}

json to_json(const character::AttributeScores& scores) {
    json j = json::object();
    for (auto a : character::kAllAttributes) {
        j[std::string(character::attribute_id(a))] = scores.score(a);
    }
    return j;
}

character::ClassDef class_from_json(const json& j, const character::Ruleset& rules, const std::string& where) {
    only_keys(j, {"id", "name", "description", "primary_attribute", "hand_size", "hit_points", "armor_class",
                  "saving_throws", "skill_choice", "suggested_attributes", "visual"}, where);
    character::ClassDef c;
    c.id = get_string(j, "id", where);
    if (c.id.empty()) fail(where, "'id' must not be empty");
    const std::string ctx = where + " [" + c.id + "]";

    c.name_key = get_string(j, "name", ctx);
    c.description_key = get_string_or(j, "description", ctx);
    c.primary = attribute_from_json(field(j, "primary_attribute", ctx), ctx + ".primary_attribute");

    c.hand_size = get_int(j, "hand_size", ctx);
    if (!rules.hand_size.contains(c.hand_size)) {
        fail(ctx, "'hand_size' must be between " + std::to_string(rules.hand_size.min) + " and " +
                      std::to_string(rules.hand_size.max));
    }

    const auto& hp = field(j, "hit_points", ctx);
    only_keys(hp, {"base", "per_level"}, ctx + ".hit_points");
    c.hp_base = get_int(hp, "base", ctx + ".hit_points");
    c.hp_per_level = get_int(hp, "per_level", ctx + ".hit_points");
    if (c.hp_base < 1) fail(ctx, "'hit_points.base' must be at least 1");
    if (c.hp_per_level < 0) fail(ctx, "'hit_points.per_level' must not be negative");

    const auto& ac = field(j, "armor_class", ctx);
    only_keys(ac, {"base", "max_dex_bonus", "extra_attribute"}, ctx + ".armor_class");
    c.armor.base = get_int(ac, "base", ctx + ".armor_class");
    if (ac.contains("max_dex_bonus")) c.armor.max_dex_bonus = get_int(ac, "max_dex_bonus", ctx + ".armor_class");
    if (ac.contains("extra_attribute")) {
        c.armor.extra_attribute = attribute_from_json(ac["extra_attribute"], ctx + ".armor_class.extra_attribute");
    }

    for (const auto& s : get_array(j, "saving_throws", ctx)) {
        c.saving_throws.push_back(attribute_from_json(s, ctx + ".saving_throws"));
    }

    const std::string sctx = ctx + ".skill_choice";
    const auto& sc = field(j, "skill_choice", ctx);
    only_keys(sc, {"count", "options"}, sctx);
    c.skill_choice.count = get_int(sc, "count", sctx);
    const auto& opts = field(sc, "options", sctx);
    if (opts.is_string() && opts.get<std::string>() == "any") {
        c.skill_choice.any = true;
    } else if (opts.is_array()) {
        c.skill_choice.options = string_list(opts, sctx);
        std::set<std::string> seen;
        for (const auto& id : c.skill_choice.options) {
            if (!rules.find_skill(id)) fail(sctx, "unknown skill '" + id + "'");
            if (!seen.insert(id).second) fail(sctx, "skill '" + id + "' listed twice");
        }
    } else {
        fail(sctx, "'options' must be a list of skills or \"any\"");
    }
    const auto available = c.skill_choice.any ? rules.skills.size() : c.skill_choice.options.size();
    if (c.skill_choice.count < 0 || static_cast<std::size_t>(c.skill_choice.count) > available) {
        fail(sctx, "'count' must be between 0 and the number of options");
    }

    c.suggested_attributes = attributes_from_json(field(j, "suggested_attributes", ctx), ctx + ".suggested_attributes");
    for (auto a : character::kAllAttributes) {
        if (!rules.attribute_score.contains(c.suggested_attributes.score(a))) {
            fail(ctx, "suggested '" + std::string(character::attribute_id(a)) + "' is outside the attribute range");
        }
    }

    if (j.contains("visual")) {
        const auto& v = j["visual"];
        only_keys(v, {"color", "portrait"}, ctx + ".visual");
        c.visual.color = get_string_or(v, "color", ctx + ".visual");
        c.visual.portrait = get_string_or(v, "portrait", ctx + ".visual");
    }
    return c;
}

json to_json(const character::CharacterSheet& sheet) {
    return json{
        {"id", sheet.id},
        {"name", sheet.name},
        {"class", sheet.class_id},
        {"level", sheet.level},
        {"attributes", to_json(sheet.attributes)},
        {"skills", sheet.skills},
        {"deck", sheet.deck},
        {"current_hp", sheet.current_hp},
    };
}

character::CharacterSheet sheet_from_json(const json& j, const character::Ruleset& rules,
                                          const character::ClassDef& cls, const std::string& where) {
    only_keys(j, {"id", "name", "class", "level", "attributes", "skills", "deck", "current_hp"}, where);
    character::CharacterSheet s;
    s.id = get_string(j, "id", where);
    s.name = get_string(j, "name", where);
    s.class_id = get_string(j, "class", where);
    s.level = get_int(j, "level", where);
    s.attributes = attributes_from_json(field(j, "attributes", where), where + ".attributes");
    s.skills = string_list(get_array(j, "skills", where), where + ".skills");
    s.deck = string_list(get_array(j, "deck", where), where + ".deck");
    s.current_hp = get_int(j, "current_hp", where);
    try {
        character::validate(rules, s, cls);
    } catch (const std::invalid_argument& e) {
        fail(where, e.what());
    }
    return s;
}

json to_json(const dice::Rng::State& state) {
    return json{{"state", state.state}, {"inc", state.inc}};
}

dice::Rng::State rng_state_from_json(const json& j, const std::string& where) {
    only_keys(j, {"state", "inc"}, where);
    dice::Rng::State s;
    for (auto [key, out] : {std::pair{"state", &s.state}, std::pair{"inc", &s.inc}}) {
        const auto& v = field(j, key, where);
        if (!v.is_number_unsigned()) fail(where, quote_key(key) + " must be an unsigned integer");
        *out = v.get<std::uint64_t>();
    }
    if ((s.inc & 1u) == 0) fail(where, "'inc' must be odd");
    return s;
}

}  // namespace rpg::data
