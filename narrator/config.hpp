#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace rpg::narrator {

// How the narrator talks to the model and what it accepts back, from data/narrator/.
struct Config {
    std::string system;        // instructions for the model
    std::string request;       // user message template with {tamanho} and {situacao}
    std::string size_combat;   // narration length asked during combat
    std::string size_scene;    // and outside it
    nlohmann::json schema;     // reply format, enforced by the server's grammar
    // Sampling; Qwen3 suggests 0.7 / 0.8 / 20 / 0 without thinking.
    double temperature = 0.7;
    double top_p = 0.8;
    int top_k = 20;
    double min_p = 0.0;
    int max_tokens = 700;
    int timeout_ms = 45000;    // a reply later than this is dropped and the default text stays
    int history = 2;           // previous narrations sent back so the story stays coherent
    int max_text_chars = 1200;
    int max_lines = 4;
    int max_line_chars = 300;
};

// Reads mestre.json and resposta.schema.json. Throws data::DataError.
Config load_config(const std::filesystem::path& data_dir);

}  // namespace rpg::narrator
