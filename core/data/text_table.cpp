#include "data/text_table.hpp"

#include "data/data_error.hpp"
#include "data/json_io.hpp"

namespace rpg::data {

void TextTable::set(std::string key, std::string text) {
    texts_[std::move(key)] = std::move(text);
}

bool TextTable::contains(const std::string& key) const {
    return texts_.find(key) != texts_.end();
}

const std::string& TextTable::get(const std::string& key) const {
    const auto it = texts_.find(key);
    return it == texts_.end() ? key : it->second;
}

TextTable load_text_table(const std::filesystem::path& file) {
    const auto j = read_json_file(file);
    if (!j.is_object()) throw DataError(file.string() + ": expected an object");

    TextTable table;
    for (const auto& [key, value] : j.items()) {
        if (!value.is_string()) throw DataError(file.string() + ": '" + key + "' must be a string");
        table.set(key, value.get<std::string>());
    }
    return table;
}

}  // namespace rpg::data
