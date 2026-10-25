#include "cards/card_registry.hpp"

#include <stdexcept>

namespace rpg::cards {

void CardRegistry::add(CardDef card) {
    const auto id = card.id;
    if (!cards_.add(std::move(card))) {
        throw std::invalid_argument("duplicate card id '" + id + "'");
    }
}

const CardDef* CardRegistry::find(const std::string& id) const {
    return cards_.find(id);
}

const CardDef& CardRegistry::get(const std::string& id) const {
    const auto* card = find(id);
    if (!card) throw std::invalid_argument("unknown card id '" + id + "'");
    return *card;
}

std::vector<std::string> CardRegistry::available(const std::string& class_id, int level) const {
    std::vector<std::string> out;
    for (const auto& card : cards_) {
        if (card.class_id == class_id && card.level <= level) out.push_back(card.id);
    }
    return out;
}

}  // namespace rpg::cards
