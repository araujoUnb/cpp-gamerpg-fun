#include "data/default_hero.hpp"

#include "cards/deck.hpp"

#include <stdexcept>

namespace rpg::data {

character::CharacterSheet default_hero(const Content& content, const std::string& id, const std::string& name,
                                       const std::string& class_id) {
    const auto& cls = content.classes.get(class_id);
    std::vector<std::string> skills;
    for (int i = 0; i < cls.skill_choice.count; ++i) {
        const auto k = static_cast<std::size_t>(i);
        skills.push_back(cls.skill_choice.any ? content.rules.skills.at(k).id : cls.skill_choice.options.at(k));
    }
    auto sheet = character::create_character(content.rules, id, name, cls, cls.suggested_attributes, std::move(skills));

    const auto available = content.cards.available(class_id, 1);
    if (static_cast<int>(available.size()) < cls.hand_size) {
        throw std::invalid_argument("class '" + class_id + "' does not have enough level-1 cards yet");
    }
    sheet.deck.assign(available.begin(), available.begin() + cls.hand_size);
    cards::validate_deck(content.cards, cls, sheet.level, sheet.deck);
    return sheet;
}

}  // namespace rpg::data
