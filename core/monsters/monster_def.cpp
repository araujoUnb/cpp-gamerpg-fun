#include "monsters/monster_def.hpp"

#include <algorithm>
#include <stdexcept>

namespace rpg::monsters {

bool MonsterDef::immune_to(const std::string& condition) const {
    return std::find(immunities.begin(), immunities.end(), condition) != immunities.end();
}

void MonsterRegistry::add(MonsterDef def) {
    const auto id = def.id;
    if (!monsters_.add(std::move(def))) throw std::invalid_argument("duplicate monster id '" + id + "'");
}

const MonsterDef* MonsterRegistry::find(const std::string& id) const {
    return monsters_.find(id);
}

const MonsterDef& MonsterRegistry::get(const std::string& id) const {
    const auto* m = find(id);
    if (!m) throw std::invalid_argument("unknown monster id '" + id + "'");
    return *m;
}

std::vector<std::string> MonsterRegistry::ids() const {
    std::vector<std::string> out;
    for (const auto& m : monsters_) out.push_back(m.id);
    return out;
}

}  // namespace rpg::monsters
