#include "data/class_registry.hpp"

#include "data/data_error.hpp"
#include "data/json_io.hpp"

#include <algorithm>

namespace rpg::data {

namespace fs = std::filesystem;

void ClassRegistry::add(character::ClassDef cls) {
    const auto id = cls.id;
    if (!classes_.emplace(id, std::move(cls)).second) {
        throw DataError("duplicate class id '" + id + "'");
    }
}

const character::ClassDef* ClassRegistry::find(const std::string& id) const {
    const auto it = classes_.find(id);
    return it == classes_.end() ? nullptr : &it->second;
}

const character::ClassDef& ClassRegistry::get(const std::string& id) const {
    const auto* cls = find(id);
    if (!cls) throw DataError("unknown class id '" + id + "'");
    return *cls;
}

std::vector<std::string> ClassRegistry::ids() const {
    std::vector<std::string> out;
    out.reserve(classes_.size());
    for (const auto& [id, _] : classes_) out.push_back(id);
    return out;
}

ClassRegistry load_classes(const fs::path& dir, const character::Ruleset& rules) {
    if (!fs::is_directory(dir)) throw DataError(dir.string() + ": not a directory");

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    ClassRegistry registry;
    for (const auto& file : files) {
        auto cls = class_from_json(read_json_file(file), rules, file.filename().string());
        try {
            registry.add(std::move(cls));
        } catch (const DataError& e) {
            throw DataError(file.filename().string() + ": " + e.what());
        }
    }
    return registry;
}

}  // namespace rpg::data
