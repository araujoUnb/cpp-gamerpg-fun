#include "character/ruleset.hpp"

#include <stdexcept>

namespace rpg::character {

int Ruleset::proficiency_bonus(int level) const {
    if (level < 1 || level > max_level()) {
        throw std::invalid_argument("proficiency_bonus: level out of range");
    }
    return proficiency_by_level[static_cast<std::size_t>(level - 1)];
}

const SkillDef* Ruleset::find_skill(const std::string& id) const {
    for (const auto& s : skills) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

}  // namespace rpg::character
