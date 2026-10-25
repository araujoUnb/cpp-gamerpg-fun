#pragma once

#include "cards/card_def.hpp"
#include "character/attributes.hpp"
#include "character/class_def.hpp"

#include <map>
#include <string>
#include <vector>

namespace rpg::monsters {

// One card of a monster type's action deck. A card is drawn per type each round.
struct MonsterCard {
    std::string id;
    std::string name_key;
    int initiative = 50;
    std::vector<cards::ActionDef> actions;
    bool reshuffle = false;  // shuffle the whole deck back at the end of the round
};

struct MonsterDef {
    std::string id;
    std::string name_key;
    int armor_class = 10;
    int hit_points = 1;
    int attack_bonus = 0;  // added to the d20 of every attack
    int damage_bonus = 0;  // added to the damage of every hit
    character::AttributeScores attributes;
    std::vector<std::string> immunities;  // condition ids
    std::vector<MonsterCard> deck;
    character::VisualDef visual;

    bool immune_to(const std::string& condition) const;
};

class MonsterRegistry {
public:
    // Throws std::invalid_argument on a duplicate id.
    void add(MonsterDef def);
    const MonsterDef* find(const std::string& id) const;
    // Throws std::invalid_argument when the id is unknown.
    const MonsterDef& get(const std::string& id) const;
    std::vector<std::string> ids() const;
    std::size_t size() const { return monsters_.size(); }

private:
    std::map<std::string, MonsterDef> monsters_;
};

}  // namespace rpg::monsters
