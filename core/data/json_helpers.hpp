#pragma once

// Internal helpers shared by the JSON loaders. Every failure throws DataError
// with `where` as a prefix, so messages point at the file and field.

#include "character/attributes.hpp"
#include "character/ruleset.hpp"
#include "data/data_error.hpp"

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace rpg::data::detail {

using nlohmann::json;

[[noreturn]] inline void fail(const std::string& where, const std::string& what) {
    throw DataError(where + ": " + what);
}

inline std::string quote_key(std::string_view key) {
    return "'" + std::string(key) + "'";
}

inline void require_object(const json& j, const std::string& where) {
    if (!j.is_object()) fail(where, "expected an object");
}

// Rejects keys outside `allowed`, which catches typos such as "rnage".
inline void only_keys(const json& j, std::initializer_list<std::string_view> allowed, const std::string& where) {
    require_object(j, where);
    for (const auto& [key, _] : j.items()) {
        bool ok = false;
        for (auto a : allowed) ok = ok || a == key;
        if (!ok) fail(where, "unknown field " + quote_key(key));
    }
}

inline const json& field(const json& j, const char* key, const std::string& where) {
    require_object(j, where);
    const auto it = j.find(key);
    if (it == j.end()) fail(where, "missing field " + quote_key(key));
    return *it;
}

inline int get_int(const json& j, const char* key, const std::string& where) {
    const auto& v = field(j, key, where);
    if (!v.is_number_integer()) fail(where, quote_key(key) + " must be an integer");
    return v.get<int>();
}

inline int get_int_or(const json& j, const char* key, int fallback, const std::string& where) {
    return j.contains(key) ? get_int(j, key, where) : fallback;
}

inline bool get_bool_or(const json& j, const char* key, bool fallback, const std::string& where) {
    if (!j.contains(key)) return fallback;
    const auto& v = j[key];
    if (!v.is_boolean()) fail(where, quote_key(key) + " must be true or false");
    return v.get<bool>();
}

inline std::string get_string(const json& j, const char* key, const std::string& where) {
    const auto& v = field(j, key, where);
    if (!v.is_string()) fail(where, quote_key(key) + " must be a string");
    return v.get<std::string>();
}

inline std::string get_string_or(const json& j, const char* key, const std::string& where) {
    return j.contains(key) ? get_string(j, key, where) : std::string{};
}

inline const json& get_array(const json& j, const char* key, const std::string& where) {
    const auto& v = field(j, key, where);
    if (!v.is_array()) fail(where, quote_key(key) + " must be an array");
    return v;
}

inline std::vector<std::string> string_list(const json& arr, const std::string& where) {
    if (!arr.is_array()) fail(where, "expected a list of strings");
    std::vector<std::string> out;
    for (const auto& v : arr) {
        if (!v.is_string()) fail(where, "expected a list of strings");
        out.push_back(v.get<std::string>());
    }
    return out;
}

inline character::Attribute attribute_from_json(const json& v, const std::string& where) {
    if (!v.is_string()) fail(where, "attribute must be a string");
    const auto a = character::parse_attribute(v.get<std::string>());
    if (!a) fail(where, "unknown attribute '" + v.get<std::string>() + "'");
    return *a;
}

inline character::IntRange range_from_json(const json& j, const std::string& where) {
    only_keys(j, {"min", "max"}, where);
    character::IntRange r{get_int(j, "min", where), get_int(j, "max", where)};
    if (r.min > r.max) fail(where, "'min' must not exceed 'max'");
    return r;
}

}  // namespace rpg::data::detail
