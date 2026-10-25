#include "character/attributes.hpp"

namespace rpg::character {

std::string_view attribute_id(Attribute a) {
    switch (a) {
        case Attribute::Strength: return "str";
        case Attribute::Dexterity: return "dex";
        case Attribute::Constitution: return "con";
        case Attribute::Intelligence: return "int";
        case Attribute::Wisdom: return "wis";
        case Attribute::Charisma: return "cha";
    }
    return "?";
}

std::optional<Attribute> parse_attribute(std::string_view id) {
    for (auto a : kAllAttributes) {
        if (attribute_id(a) == id) return a;
    }
    return std::nullopt;
}

}  // namespace rpg::character
