#pragma once

#include "cards/card_def.hpp"
#include "combat/hex.hpp"

#include <string>
#include <vector>

namespace rpg::scenario {

struct MonsterSpawn {
    std::string type;  // monster id
    combat::Hex hex;
};

// Fires once when a creature enters its hex (not when jumping over it), then disappears.
struct TrapDef {
    combat::Hex hex;
    int damage = 0;
    std::vector<cards::ConditionApply> conditions;
};

// A battle: the map, where the heroes start and which monsters are there.
// Trap hexes have Terrain::Trap in the map and an entry in `traps`.
struct ScenarioDef {
    std::string id;
    std::string name_key;
    combat::HexMap map;
    std::vector<combat::Hex> hero_starts;  // one per hero, in party order
    std::vector<MonsterSpawn> monsters;
    std::vector<TrapDef> traps;

    const TrapDef* trap_at(combat::Hex h) const {
        for (const auto& t : traps) {
            if (t.hex == h) return &t;
        }
        return nullptr;
    }
};

}  // namespace rpg::scenario
