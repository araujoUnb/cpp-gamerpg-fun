#pragma once

#include "cards/card_def.hpp"
#include "cards/card_registry.hpp"
#include "cards/hero_cards.hpp"
#include "character/ruleset.hpp"
#include "data/class_registry.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace rpg::data {

using nlohmann::json;

// [ { "id": "<condition>", "rounds": N }, ... ]
std::vector<cards::ConditionApply> condition_list_from_json(const json& arr, const character::Ruleset& rules,
                                                            const std::string& where = "conditions");

// Unknown fields, unknown conditions, out-of-range values and actions with
// fields that do not apply to their type are all rejected with DataError.
cards::ActionDef action_from_json(const json& j, const character::Ruleset& rules,
                                  const std::string& where = "action");
cards::CardDef card_from_json(const json& j, const character::Ruleset& rules, const std::string& class_id,
                              const std::string& where = "card");

// Loads every *.json in the directory. Each file is
//   { "class": "<class id>", "cards": [ ... ] }
// and card ids must start with "<class id>.".
cards::CardRegistry load_cards(const std::filesystem::path& dir, const character::Ruleset& rules,
                               const ClassRegistry& classes);

json to_json(const cards::RoundSelection& s);
json to_json(const cards::HeroCards& cards);
cards::HeroCards hero_cards_from_json(const json& j, const std::vector<std::string>& deck,
                                      const std::string& where = "cards");

}  // namespace rpg::data
