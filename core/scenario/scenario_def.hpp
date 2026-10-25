#pragma once

#include "combat/hex.hpp"

#include <string>
#include <vector>

namespace rpg::scenario {

struct MonsterSpawn {
    std::string type;  // monster id
    combat::Hex hex;
};

// A battle: the map, where the heroes start and which monsters are there.
struct ScenarioDef {
    std::string id;
    std::string name_key;
    combat::HexMap map;
    std::vector<combat::Hex> hero_starts;  // one per hero, in party order
    std::vector<MonsterSpawn> monsters;
};

}  // namespace rpg::scenario
