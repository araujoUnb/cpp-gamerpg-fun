#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rpg::character {

enum class Attribute : std::uint8_t { Strength, Dexterity, Constitution, Intelligence, Wisdom, Charisma };

inline constexpr std::array<Attribute, 6> kAllAttributes{
    Attribute::Strength,     Attribute::Dexterity, Attribute::Constitution,
    Attribute::Intelligence, Attribute::Wisdom,    Attribute::Charisma};

// Short id used in data files: "str", "dex", "con", "int", "wis", "cha".
std::string_view attribute_id(Attribute a);
std::optional<Attribute> parse_attribute(std::string_view id);

// (score - 10) / 2, rounded down (also for scores below 10).
constexpr int modifier(int score) {
    return score >= 10 ? (score - 10) / 2 : -((11 - score) / 2);
}

class AttributeScores {
public:
    AttributeScores() { values_.fill(10); }

    int score(Attribute a) const { return values_[index(a)]; }
    void set(Attribute a, int value) { values_[index(a)] = value; }
    int mod(Attribute a) const { return modifier(score(a)); }

    friend bool operator==(const AttributeScores&, const AttributeScores&) = default;

private:
    static std::size_t index(Attribute a) { return static_cast<std::size_t>(a); }

    std::array<int, 6> values_{};
};

}  // namespace rpg::character
