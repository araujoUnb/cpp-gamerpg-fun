#pragma once

#include "narrator/config.hpp"

#include "data/text_table.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rpg::narrator {

struct Line {
    std::string speaker;
    std::string text;

    friend bool operator==(const Line&, const Line&) = default;
};

// What the narrator said about one situation, checked and trimmed to the limits.
struct Narration {
    std::string situation;  // id of the situation it answers
    std::string text;
    std::vector<Line> lines;
    // Requests that passed the first check (type allowed in this situation).
    // The core validates and resolves them (part N3).
    std::vector<nlohmann::json> actions;
    // Why requests were dropped; sent back to the model with the next situation.
    std::vector<std::string> warnings;
    // Where it was shown: number of combat events already shown. Replays print it at the same point.
    std::size_t after_event = 0;
};

// What the model is told when the core drops one of its requests.
struct Warnings {
    std::string not_allowed;  // with {tipo}
    std::string extra_test;   // more than one test in a reply
    std::string repeated;     // sentences copied from the previous narrations
};
// From the "narrator.warning.*" texts.
Warnings load_warnings(const data::TextTable& text);

// The reply format for one situation: the base schema with "acoes" narrowed
// to what "acoes_permitidas" allows (types, attributes, DC range, items,
// gold, flags, encounters) and "falas" capped. The server turns it into a
// grammar, so the model cannot even write an action that is not allowed.
nlohmann::json reply_schema(const Config& cfg, const nlohmann::json& situation);

// The body of the request to llama-server's chat endpoint.
nlohmann::json request_body(const Config& cfg, const nlohmann::json& situation);

// Reads the server's reply to `situation`. nullopt when the reply is not the
// JSON asked for or has no narration; the game then keeps its default text.
// Lines given to the heroes are dropped: they belong to the player. Only the
// first test is kept: the model waits for its result before asking again.
std::optional<Narration> parse_reply(const Config& cfg, const nlohmann::json& situation, const std::string& reply,
                                     const Warnings& warnings);

// Everything the narrator showed, for the save and for replays without the model.
nlohmann::json to_json(const std::vector<Narration>& log);
// Throws data::DataError when malformed.
std::vector<Narration> log_from_json(const nlohmann::json& j);

}  // namespace rpg::narrator
