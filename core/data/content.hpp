#pragma once

#include "cards/card_registry.hpp"
#include "character/ruleset.hpp"
#include "data/class_registry.hpp"
#include "data/text_table.hpp"
#include "monsters/game_master.hpp"
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
    util::IdTable<monsters::GameMasterDef> game_masters;
    util::IdTable<monsters::DifficultyDef> difficulties;
    TextTable text;
};

// Loads rules, classes, cards, monsters and the text of one language.
// Throws DataError on the first problem found.
Content load_content(const std::filesystem::path& data_dir, const std::string& language = "pt-BR");

monsters::MonsterDef monster_from_json(const json& j, const character::Ruleset& rules,
                                       const std::string& where = "monster");

// A game master profile; weights outside [-kMaxGmWeight, kMaxGmWeight] are rejected.
inline constexpr int kMaxGmWeight = 10000;
monsters::GameMasterDef game_master_from_json(const json& j, const std::string& where = "game master");
json to_json(const monsters::GameMasterDef& gm);
// Checks the ranges and that its game master exists in `content` (or is "classic").
monsters::DifficultyDef difficulty_from_json(const json& j, const Content& content, const std::string& where = "difficulty");
json to_json(const monsters::DifficultyDef& d);

// Checks map bounds, overlapping starts and unknown monster types.
scenario::ScenarioDef scenario_from_json(const json& j, const Content& content,
                                         const std::string& where = "scenario");
scenario::ScenarioDef load_scenario(const std::filesystem::path& file, const Content& content);

}  // namespace rpg::data
