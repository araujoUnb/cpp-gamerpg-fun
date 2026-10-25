#pragma once

#include "character/class_def.hpp"
#include "character/ruleset.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace rpg::data {

class ClassRegistry {
public:
    // Throws DataError on a duplicate id.
    void add(character::ClassDef cls);

    const character::ClassDef* find(const std::string& id) const;
    // Throws DataError when the id is unknown.
    const character::ClassDef& get(const std::string& id) const;

    std::vector<std::string> ids() const;  // sorted
    std::size_t size() const { return classes_.size(); }

private:
    std::map<std::string, character::ClassDef> classes_;
};

// Loads every *.json in the directory, in file-name order.
ClassRegistry load_classes(const std::filesystem::path& dir, const character::Ruleset& rules);

}  // namespace rpg::data
