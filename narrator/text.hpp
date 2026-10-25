#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rpg::narrator {

// Replaces every "{name}" in `text` with `value`.
inline void fill(std::string& text, std::string_view name, std::string_view value) {
    const std::string mark = "{" + std::string(name) + "}";
    for (auto pos = text.find(mark); pos != std::string::npos; pos = text.find(mark, pos + value.size())) {
        text.replace(pos, mark.size(), value);
    }
}

// Cuts UTF-8 text to at most `max_chars` characters, never inside a character.
inline void truncate_utf8(std::string& text, std::size_t max_chars) {
    std::size_t chars = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
        if (chars++ == max_chars) {
            text.resize(i);
            return;
        }
    }
}

// The sentences of a text, each ending at '.', '!' or '?' (the last one may
// have no ending), without leading spaces. Fragments of 3 bytes or less are skipped.
inline std::vector<std::string> sentences(std::string_view text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text[i] != '.' && text[i] != '!' && text[i] != '?') continue;
        const auto end = std::min(i + 1, text.size());
        const auto b = text.find_first_not_of(' ', start);
        if (b != std::string_view::npos && b < end && end - b > 3) out.emplace_back(text.substr(b, end - b));
        start = end;
    }
    return out;
}

// Lowercase ASCII words of 3 bytes or more (accented letters count as letters).
inline std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char ch = i < text.size() ? text[i] : ' ';
        const bool letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                            (static_cast<unsigned char>(ch) & 0x80) != 0;
        if (letter) {
            cur += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
        } else {
            if (cur.size() >= 3) out.push_back(cur);
            cur.clear();
        }
    }
    return out;
}

// Removes from `text` the sentences that repeat one of `before`: at least
// 80% of their words are in a single earlier sentence (sentences with fewer
// than 4 words are always kept). Small models copy the
// previous narration they are shown, and the player would read it twice.
inline std::string drop_repeated(std::string_view text, const std::vector<std::string>& before) {
    std::vector<std::vector<std::string>> old;
    for (const auto& b : before) {
        for (const auto& s : sentences(b)) old.push_back(words(s));
    }
    std::string out;
    for (const auto& s : sentences(text)) {
        const auto w = words(s);
        const bool repeated = w.size() >= 4 && std::any_of(old.begin(), old.end(), [&](const auto& o) {
            const auto common = std::count_if(w.begin(), w.end(), [&](const auto& x) { return std::find(o.begin(), o.end(), x) != o.end(); });
            return 5 * common >= 4 * static_cast<std::ptrdiff_t>(w.size());
        });
        if (repeated) continue;
        if (!out.empty()) out += ' ';
        out += s;
    }
    return out;
}

}  // namespace rpg::narrator
