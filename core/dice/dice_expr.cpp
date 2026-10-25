#include "dice/dice_expr.hpp"

#include <charconv>
#include <cctype>

namespace rpg::dice {

namespace {

constexpr int kMaxCount = 100;
constexpr int kMaxSides = 1000;
constexpr int kMaxBonus = 1000;

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// Parses a non-empty run of digits that must consume the whole view.
std::optional<int> parse_uint(std::string_view s) {
    if (s.empty()) return std::nullopt;
    int value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size() || value < 0) return std::nullopt;
    return value;
}

}  // namespace

std::optional<DiceExpr> parse_dice(std::string_view text) {
    text = trim(text);
    if (text.empty()) return std::nullopt;

    DiceExpr expr;

    const auto d = text.find_first_of("dD");
    std::string_view rest = text;
    if (d != std::string_view::npos) {
        const auto count_part = text.substr(0, d);
        if (count_part.empty()) {
            expr.count = 1;
        } else {
            const auto c = parse_uint(count_part);
            if (!c || *c < 1 || *c > kMaxCount) return std::nullopt;
            expr.count = *c;
        }
        rest = text.substr(d + 1);

        const auto sign = rest.find_first_of("+-");
        const auto sides = parse_uint(rest.substr(0, sign));
        if (!sides || *sides < 2 || *sides > kMaxSides) return std::nullopt;
        expr.sides = *sides;
        rest = sign == std::string_view::npos ? std::string_view{} : rest.substr(sign);
        if (rest.empty()) return expr;
    }

    // Flat part: "+3", "-1", or a bare number when there are no dice.
    bool negative = false;
    if (rest.front() == '+' || rest.front() == '-') {
        negative = rest.front() == '-';
        rest.remove_prefix(1);
    } else if (d != std::string_view::npos) {
        return std::nullopt;
    }
    const auto b = parse_uint(rest);
    if (!b || *b > kMaxBonus) return std::nullopt;
    expr.bonus = negative ? -*b : *b;
    return expr;
}

std::string to_string(const DiceExpr& expr) {
    std::string out;
    if (expr.count > 0) {
        out = std::to_string(expr.count) + "d" + std::to_string(expr.sides);
        if (expr.bonus > 0) out += "+" + std::to_string(expr.bonus);
        if (expr.bonus < 0) out += std::to_string(expr.bonus);
    } else {
        out = std::to_string(expr.bonus);
    }
    return out;
}

DamageRoll roll_damage(Rng& rng, const DiceExpr& expr, bool critical) {
    DamageRoll result;
    const int count = critical ? expr.count * 2 : expr.count;
    result.dice.reserve(static_cast<std::size_t>(count));
    int sum = 0;
    for (int i = 0; i < count; ++i) {
        const int v = rng.roll(expr.sides);
        result.dice.push_back(v);
        sum += v;
    }
    result.bonus = expr.bonus;
    result.total = sum + expr.bonus < 0 ? 0 : sum + expr.bonus;
    return result;
}

}  // namespace rpg::dice
