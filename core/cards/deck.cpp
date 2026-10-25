#include "cards/deck.hpp"

#include <set>
#include <stdexcept>

namespace rpg::cards {

void validate_deck(const CardRegistry& registry, const character::ClassDef& cls, int level,
                   const std::vector<std::string>& deck) {
    const std::string who = "deck for '" + cls.id + "'";
    if (static_cast<int>(deck.size()) != cls.hand_size) {
        throw std::invalid_argument(who + ": must have exactly " + std::to_string(cls.hand_size) + " cards, has " +
                                    std::to_string(deck.size()));
    }
    std::set<std::string> seen;
    for (const auto& id : deck) {
        const auto* card = registry.find(id);
        if (!card) throw std::invalid_argument(who + ": unknown card '" + id + "'");
        if (card->class_id != cls.id) throw std::invalid_argument(who + ": card '" + id + "' is from another class");
        if (card->level > level) {
            throw std::invalid_argument(who + ": card '" + id + "' needs level " + std::to_string(card->level));
        }
        if (!seen.insert(id).second) throw std::invalid_argument(who + ": card '" + id + "' appears twice");
    }
}

}  // namespace rpg::cards
