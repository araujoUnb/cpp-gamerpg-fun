#pragma once

#include "character/attributes.hpp"
#include "character/character_sheet.hpp"
#include "character/class_def.hpp"
#include "character/ruleset.hpp"
#include "dice/rng.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace rpg::data {

using nlohmann::json;

// Reads and parses a JSON file (comments allowed). Throws DataError naming the file.
json read_json_file(const std::filesystem::path& path);

// Every loader throws DataError on bad input. `where` prefixes the message,
// typically the file name, so content errors point at the right place.

character::Ruleset ruleset_from_json(const json& j, const std::string& where = "ruleset");

// Checked against the ruleset: hand size, attribute range, skill ids.
character::ClassDef class_from_json(const json& j, const character::Ruleset& rules,
                                    const std::string& where = "class");

character::AttributeScores attributes_from_json(const json& j, const std::string& where = "attributes");
json to_json(const character::AttributeScores& scores);

// Save data. The sheet is checked against its class and the ruleset.
json to_json(const character::CharacterSheet& sheet);
character::CharacterSheet sheet_from_json(const json& j, const character::Ruleset& rules,
                                          const character::ClassDef& cls,
                                          const std::string& where = "character");

json to_json(const dice::Rng::State& state);
dice::Rng::State rng_state_from_json(const json& j, const std::string& where = "rng");

}  // namespace rpg::data
