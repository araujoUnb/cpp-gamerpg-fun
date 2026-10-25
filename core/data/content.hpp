#pragma once

#include "cards/card_registry.hpp"
#include "character/ruleset.hpp"
#include "data/class_registry.hpp"
#include "data/text_table.hpp"
#include "monsters/monster_def.hpp"
#include "scenario/scenario_def.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace rpg::data {

using nlohmann::json;

// Everything the rules read from data/, loaded and cross-checked once.
struct Content {
    character::Ruleset rules;
    ClassRegistry classes;
    cards::CardRegistry cards;
    monsters::MonsterRegistry monsters;
    TextTable text;
};

// Loads rules, classes, cards, monsters and the text of one language.
// Throws DataError on the first problem found.
Content load_content(const std::filesystem::path& data_dir, const std::string& language = "pt-BR");

monsters::MonsterDef monster_from_json(const json& j, const character::Ruleset& rules,
                                       const std::string& where = "monster");

// Checks map bounds, overlapping starts and unknown monster types.
scenario::ScenarioDef scenario_from_json(const json& j, const Content& content,
                                         const std::string& where = "scenario");
scenario::ScenarioDef load_scenario(const std::filesystem::path& file, const Content& content);

}  // namespace rpg::data
