#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace rpg::data {

// Game text for one language, keyed by ids such as "class.bulwark.name".
// Content files hold keys, never the text itself, so translating the game
// means adding another file to data/lang/.
class TextTable {
public:
    void set(std::string key, std::string text);
    bool contains(const std::string& key) const;

    // Returns the key itself when it has no text, so missing entries are visible in game.
    const std::string& get(const std::string& key) const;

    std::size_t size() const { return texts_.size(); }

private:
    std::map<std::string, std::string> texts_;
};

// Flat JSON object { "key": "texto", ... }. Throws DataError on bad input.
TextTable load_text_table(const std::filesystem::path& file);

}  // namespace rpg::data
