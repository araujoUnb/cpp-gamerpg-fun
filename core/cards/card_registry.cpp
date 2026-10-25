#include "cards/card_registry.hpp"

#include <stdexcept>

namespace rpg::cards {

void CardRegistry::add(CardDef card) {
    const auto id = card.id;
    if (!cards_.emplace(id, std::move(card)).second) {
        throw std::invalid_argument("duplicate card id '" + id + "'");
    }
}

const CardDef* CardRegistry::find(const std::string& id) const {
    const auto it = cards_.find(id);
    return it == cards_.end() ? nullptr : &it->second;
}

const CardDef& CardRegistry::get(const std::string& id) const {
    const auto* card = find(id);
    if (!card) throw std::invalid_argument("unknown card id '" + id + "'");
    return *card;
}

std::vector<std::string> CardRegistry::available(const std::string& class_id, int level) const {
    std::vector<std::string> out;
    for (const auto& [id, card] : cards_) {
        if (card.class_id == class_id && card.level <= level) out.push_back(id);
    }
    return out;
}

}  // namespace rpg::cards
