#include "data/json_io.hpp"

#include "data/data_error.hpp"

#include <fstream>
#include <set>

namespace rpg::data {

using character::Attribute;

namespace {

[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw DataError(where + ": " + what);
}

std::string quoted(const char* key) {
    return std::string("'") + key + "'";
}

const json& field(const json& j, const char* key, const std::string& where) {
    if (!j.is_object()) fail(where, "expected an object");
    const auto it = j.find(key);
    if (it == j.end()) fail(where, "missing field " + quoted(key));
    return *it;
}

int get_int(const json& j, const char* key, const std::string& where) {
    const auto& v = field(j, key, where);
    if (!v.is_number_integer()) fail(where, quoted(key) + " must be an integer");
    return v.get<int>();
}

std::string get_string(const json& j, const char* key, const std::string& where) {
    const auto& v = field(j, key, where);
    if (!v.is_string()) fail(where, quoted(key) + " must be a string");
    return v.get<std::string>();
}

std::string get_string_or(const json& j, const char* key, const std::string& where) {
    if (!j.contains(key)) return {};
    return get_string(j, key, where);
}

const json& get_array(const json& j, const char* key, const std::string& where) {
    const auto& v = field(j, key, where);
    if (!v.is_array()) fail(where, quoted(key) + " must be an array");
    return v;
}

Attribute attribute_from_json(const json& v, const std::string& where) {
    if (!v.is_string()) fail(where, "attribute must be a string");
    const auto a = character::parse_attribute(v.get<std::string>());
    if (!a) fail(where, "unknown attribute '" + v.get<std::string>() + "'");
    return *a;
}

character::IntRange range_from_json(const json& j, const std::string& where) {
    character::IntRange r{get_int(j, "min", where), get_int(j, "max", where)};
    if (r.min > r.max) fail(where, "'min' must not exceed 'max'");
    return r;
}

std::vector<std::string> string_list(const json& arr, const std::string& where) {
    std::vector<std::string> out;
    for (const auto& v : arr) {
        if (!v.is_string()) fail(where, "expected a list of strings");
        out.push_back(v.get<std::string>());
    }
    return out;
}

}  // namespace

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
        character::SkillDef skill;
        skill.id = get_string(s, "id", ctx);
        skill.attribute = attribute_from_json(field(s, "attribute", ctx), ctx + " [" + skill.id + "]");
        skill.name_key = get_string(s, "name", ctx);
        if (!ids.insert(skill.id).second) fail(ctx, "duplicate skill '" + skill.id + "'");
        r.skills.push_back(std::move(skill));
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
    c.hp_base = get_int(hp, "base", ctx + ".hit_points");
    c.hp_per_level = get_int(hp, "per_level", ctx + ".hit_points");
    if (c.hp_base < 1) fail(ctx, "'hit_points.base' must be at least 1");
    if (c.hp_per_level < 0) fail(ctx, "'hit_points.per_level' must not be negative");

    const auto& ac = field(j, "armor_class", ctx);
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
        {"current_hp", sheet.current_hp},
    };
}

character::CharacterSheet sheet_from_json(const json& j, const character::Ruleset& rules,
                                          const character::ClassDef& cls, const std::string& where) {
    character::CharacterSheet s;
    s.id = get_string(j, "id", where);
    s.name = get_string(j, "name", where);
    s.class_id = get_string(j, "class", where);
    s.level = get_int(j, "level", where);
    s.attributes = attributes_from_json(field(j, "attributes", where), where + ".attributes");
    s.skills = string_list(get_array(j, "skills", where), where + ".skills");
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
    dice::Rng::State s;
    for (auto [key, out] : {std::pair{"state", &s.state}, std::pair{"inc", &s.inc}}) {
        const auto& v = field(j, key, where);
        if (!v.is_number_unsigned()) fail(where, quoted(key) + " must be an unsigned integer");
        *out = v.get<std::uint64_t>();
    }
    if ((s.inc & 1u) == 0) fail(where, "'inc' must be odd");
    return s;
}

}  // namespace rpg::data
