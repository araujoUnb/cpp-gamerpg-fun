#pragma once

#include "cards/card_def.hpp"
#include "util/id_table.hpp"

#include <string>
#include <vector>

namespace rpg::cards {

class CardRegistry {
public:
    // Throws std::invalid_argument on a duplicate id.
    void add(CardDef card);

    const CardDef* find(const std::string& id) const;
    // Throws std::invalid_argument when the id is unknown.
    const CardDef& get(const std::string& id) const;

    // Cards of a class unlocked at or below `level`, sorted by id.
    std::vector<std::string> available(const std::string& class_id, int level) const;

    std::size_t size() const { return cards_.size(); }

private:
    util::IdTable<CardDef> cards_;
};

}  // namespace rpg::cards
