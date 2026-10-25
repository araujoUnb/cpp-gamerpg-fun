#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace rpg::monsters {

// Feature weights of the tactical game master. A monster's options (where to
// end its move, whom to attack) are scored as the sum of weight x feature.
// Every feature is an integer where 400 means 1.0 (d20 odds with advantage
// are counted out of 400), so scores are exact and replays match everywhere.
struct GmWeights {
    int expected_damage = 0;  // expected damage of the attack on that hero
    int down_chance = 0;      // chance that the attack drops the hero to 0 HP
    int wounded = 0;          // missing HP fraction of the hero
    int healer = 0;           // the hero carries healing cards
    int surround = 0;         // per other monster already next to the hero
    int exposure_melee = 0;   // per active hero next to the end hex, melee attackers
    int exposure_ranged = 0;  // the same, for monsters attacking at range
    int approach = 0;         // per step still to walk to the nearest hero, when nothing is in reach
    int steps = 0;            // per movement point spent

    friend bool operator==(const GmWeights&, const GmWeights&) = default;
};

// Field names as they appear in data/game_masters/*.json.
inline constexpr std::array<std::pair<std::string_view, int GmWeights::*>, 9> kGmWeightFields{{
    {"expected_damage", &GmWeights::expected_damage},
    {"down_chance", &GmWeights::down_chance},
    {"wounded", &GmWeights::wounded},
    {"healer", &GmWeights::healer},
    {"surround", &GmWeights::surround},
    {"exposure_melee", &GmWeights::exposure_melee},
    {"exposure_ranged", &GmWeights::exposure_ranged},
    {"approach", &GmWeights::approach},
    {"steps", &GmWeights::steps},
}};

// How the monsters are played. The classic one follows the simple rule of
// the SPEC (nearest hero, fewest steps); the tactical one scores its options.
struct GameMasterDef {
    static constexpr std::string_view kClassic = "classic";

    std::string id{kClassic};
    std::string name_key;
    bool tactical = false;
    GmWeights weights;
};

// A difficulty level: which game master plays and how it is softened.
// The calibration goals are not rules; the training tool reads them.
struct DifficultyDef {
    static constexpr std::string_view kStandard = "standard";  // built in: classic master, nothing changed

    std::string id{kStandard};
    std::string name_key;
    std::string game_master{GameMasterDef::kClassic};
    int blunder = 0;               // per mille: chance per monster turn of playing by the classic rule
    int monster_hp_percent = 100;  // of each monster's hit points, rounded, at least 1
    int target_win_percent = 0;    // average hero win rate the level aims for (0: no goal)
    int floor_win_percent = 0;     // no tested matchup may fall below this

    int monster_hp(int base) const { return std::max(1, (base * monster_hp_percent + 50) / 100); }
};

}  // namespace rpg::monsters
