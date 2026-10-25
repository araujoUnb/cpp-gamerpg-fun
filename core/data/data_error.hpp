#pragma once

#include <stdexcept>
#include <string>

namespace rpg::data {

// Thrown when a content or save file is missing, malformed or breaks a rule.
// The message says which file and field caused it.
class DataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace rpg::data
