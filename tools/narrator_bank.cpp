// Writes the narrator's phrase bank with the model: for each kind of event,
// asks for variants a few at a time, keeps the ones that pass valid_phrase
// and saves the bank the game uses without a model (--narrador banco).
// Runs on lab10 next to the model (see treino/llm_banco.sbatch).
#include "data/json_helpers.hpp"
#include "data/json_io.hpp"
#include "narrator/bank.hpp"
#include "narrator/narrator.hpp"
#include "narrator/text.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace rpg;
using nlohmann::json;

namespace {

struct Options {
    std::string url = "http://127.0.0.1:8080";
    std::string data_dir = GAMERPG_DATA_DIR;
    std::string out;
};

std::string lower_ascii(std::string text) {
    for (auto& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

std::string joined(const std::vector<std::string>& items, const std::string& none) {
    if (items.empty()) return none;
    std::string out;
    for (const auto& i : items) out += (out.empty() ? "" : "; ") + i;
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--url") o.url = argv[i + 1];
        else if (a == "--data") o.data_dir = argv[i + 1];
        else if (a == "--saida") o.out = argv[i + 1];
    }
    if (o.out.empty() || argc % 2 == 0) {
        std::cout << "uso: gamerpg_narrator_bank --saida ARQUIVO.json [--url URL] [--data DIR]\n";
        return 2;
    }
    try {
        using namespace data::detail;
        const auto ins = data::read_json_file(std::filesystem::path(o.data_dir) / "narrator" / "banco_instrucoes.json");
        const std::string where = "banco_instrucoes.json";
        const auto system = get_string(ins, "sistema", where);
        const auto request = get_string(ins, "pedido", where);
        const int target = get_int(ins, "frases_por_evento", where);
        const int per_request = get_int(ins, "frases_por_pedido", where);
        auto transport = narrator::make_http_transport(o.url, std::chrono::minutes(5));

        narrator::PhraseBank bank;
        bank.events_per_round = get_int(ins, "eventos_por_rodada", where);
        for (const auto& [key, ev] : field(ins, "eventos", where).items()) {
            const auto marks = string_list(get_array(ev, "marcadores", where), where + ": " + key);
            const auto description = get_string(ev, "descricao", where + ": " + key);
            std::vector<std::string> phrases, seen;
            int rejected = 0;
            for (int attempt = 0; attempt < 3 * (target / per_request + 1) && static_cast<int>(phrases.size()) < target; ++attempt) {
                const int n = std::min(per_request, target - static_cast<int>(phrases.size()));
                std::string user = request;
                narrator::fill(user, "n", std::to_string(n));
                narrator::fill(user, "descricao", description);
                narrator::fill(user, "marcadores", joined(marks, "nenhum"));
                std::vector<std::string> recent(phrases.end() - std::min<std::ptrdiff_t>(12, std::ssize(phrases)), phrases.end());
                narrator::fill(user, "feitas", joined(recent, "nenhuma"));
                const json body{
                    {"messages", json::array({json{{"role", "system"}, {"content", system}}, json{{"role", "user"}, {"content", user}}})},
                    {"response_format",
                     json{{"type", "json_schema"},
                          {"json_schema",
                           json{{"name", "frases"},
                                {"schema", json{{"type", "object"},
                                                {"additionalProperties", false},
                                                {"required", {"frases"}},
                                                {"properties", json{{"frases", json{{"type", "array"},
                                                                                    {"minItems", n},
                                                                                    {"maxItems", n},
                                                                                    {"items", json{{"type", "string"}}}}}}}}}}}}},
                    {"chat_template_kwargs", json{{"enable_thinking", false}}},
                    // More variety than when narrating: these are drawn many times.
                    {"temperature", 0.9},
                    {"top_p", 0.95},
                    {"top_k", 40},
                    {"max_tokens", 120 * n}};
                const auto reply = transport->post(body.dump());
                if (!reply) continue;
                const auto outer = json::parse(*reply, nullptr, false);
                if (outer.is_discarded()) continue;
                const auto answer = json::parse(outer["choices"][0]["message"].value("content", ""), nullptr, false);
                if (answer.is_discarded() || !answer.contains("frases")) continue;
                for (const auto& f : answer["frases"]) {
                    if (!f.is_string()) continue;
                    const auto phrase = f.get<std::string>();
                    const auto norm = lower_ascii(phrase);
                    if (!narrator::valid_phrase(phrase, marks) || std::find(seen.begin(), seen.end(), norm) != seen.end()) {
                        ++rejected;
                        continue;
                    }
                    seen.push_back(norm);
                    phrases.push_back(phrase);
                }
            }
            std::cout << key << ": " << phrases.size() << " frases, " << rejected << " recusadas\n" << std::flush;
            if (phrases.empty()) continue;
            if (key == "abertura") bank.openings = std::move(phrases);
            else bank.events.emplace_back(key, std::move(phrases));
        }
        std::ofstream(o.out) << narrator::to_json(bank).dump(2) << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "erro: " << e.what() << '\n';
        return 3;
    }
}
