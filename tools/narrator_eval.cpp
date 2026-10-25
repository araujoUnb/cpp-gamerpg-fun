// Sends the narrator test situations to a running llama-server, with the same
// request and checks the game uses, and records every answer for review.
// Runs on lab10 next to the model (see treino/llm_avalia.sbatch).
#include "data/content.hpp"
#include "data/json_io.hpp"
#include "narrator/narrator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
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
    std::string situations;
    std::string out;
    int samples = 1;
};

void usage() {
    std::cout << "uso: gamerpg_narrator_eval --situacoes ARQUIVO --saida ARQUIVO.jsonl [--url URL] [--amostras N] [--data DIR]\n";
}

bool parse_args(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) return false;
        const std::string v = argv[++i];
        if (a == "--url") o.url = v;
        else if (a == "--data") o.data_dir = v;
        else if (a == "--situacoes") o.situations = v;
        else if (a == "--saida") o.out = v;
        else if (a == "--amostras") o.samples = std::atoi(v.c_str());
        else return false;
    }
    return !o.situations.empty() && !o.out.empty() && o.samples > 0;
}

// Lines the model gave to the heroes, before parse_reply drops them.
int hero_lines(const json& situation, const std::string& reply) {
    const auto outer = json::parse(reply, nullptr, false);
    if (outer.is_discarded()) return 0;
    const auto answer = json::parse(outer["choices"][0]["message"].value("content", ""), nullptr, false);
    if (answer.is_discarded() || !answer.contains("falas") || !answer["falas"].is_array()) return 0;
    int n = 0;
    for (const auto& l : answer["falas"]) {
        for (const auto& h : situation.value("grupo", json::array())) {
            n += l.is_object() && l.value("personagem", "") == h.value("nome", "?");
        }
    }
    return n;
}

std::string lower_ascii(std::string text) {
    for (auto& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

// NPC secrets that show up in the narration or in any line. The words that
// give each secret away come in the situation's "_avaliacao", never sent.
json leaked_secrets(const json& check, const narrator::Narration& n) {
    std::string all = n.text;
    for (const auto& l : n.lines) all += "\n" + l.text;
    all = lower_ascii(all);
    json out = json::array();
    for (const auto& secret : check.value("segredos", json::array())) {
        for (const auto& word : secret.value("palavras", json::array())) {
            if (all.find(lower_ascii(word.get<std::string>())) != std::string::npos) {
                out.push_back(secret.value("npc", "?"));
                break;
            }
        }
    }
    return out;
}

// Speakers that are neither NPCs nor enemies of the scene; only for scenes that list NPCs.
int offstage_lines(const json& situation, const narrator::Narration& n) {
    if (!situation.contains("npcs")) return 0;
    std::vector<std::string> cast;
    for (const auto* key : {"npcs", "inimigos"}) {
        for (const auto& c : situation.value(key, json::array())) cast.push_back(c.value("nome", ""));
    }
    int out = 0;
    for (const auto& l : n.lines) out += std::find(cast.begin(), cast.end(), l.speaker) == cast.end();
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse_args(argc, argv, o)) {
        usage();
        return 2;
    }
    try {
        const auto cfg = narrator::load_config(o.data_dir);
        const auto warning = narrator::load_warnings(data::load_content(o.data_dir).text);
        const auto situations = data::read_json_file(o.situations);
        // Generous limit: here we measure the time, the game is the one that gives up.
        auto transport = narrator::make_http_transport(o.url, std::chrono::minutes(5));
        std::ofstream out(o.out);

        int total = 0, valid = 0, heroes = 0, dropped = 0, leaks = 0, offstage = 0;
        double seconds = 0;
        for (auto s : situations) {
            const auto check = s.value("_avaliacao", json::object());
            s.erase("_avaliacao");
            for (int k = 0; k < o.samples; ++k) {
                const auto t0 = std::chrono::steady_clock::now();
                const auto reply = transport->post(narrator::request_body(cfg, s).dump());
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                const auto n = reply ? narrator::parse_reply(cfg, s, *reply, warning) : std::nullopt;

                json record{{"id", s.value("id", "")}, {"amostra", k}, {"segundos", std::round(secs * 10) / 10},
                            {"valida", n.has_value()}};
                if (reply) {
                    const auto outer = json::parse(*reply, nullptr, false);
                    if (!outer.is_discarded() && outer.contains("usage")) record["tokens"] = outer["usage"];
                    record["falas_de_herois"] = hero_lines(s, *reply);
                    heroes += record["falas_de_herois"].get<int>();
                }
                if (n) {
                    record["resposta"] = narrator::to_json(std::vector{*n})[0];
                    dropped += static_cast<int>(n->warnings.size());
                    record["segredos_vazados"] = leaked_secrets(check, *n);
                    record["falas_fora_de_cena"] = offstage_lines(s, *n);
                    leaks += static_cast<int>(record["segredos_vazados"].size());
                    offstage += record["falas_fora_de_cena"].get<int>();
                }
                out << record.dump() << '\n' << std::flush;

                ++total;
                valid += n.has_value();
                seconds += secs;
                std::cout << record["id"].get<std::string>() << " #" << k << "  " << record["segundos"] << " s"
                          << (n ? "" : "  INVÁLIDA");
                if (n && !record["segredos_vazados"].empty()) std::cout << "  vazou: " << record["segredos_vazados"].dump();
                std::cout << '\n' << std::flush;
            }
        }
        std::cout << "\n" << total << " pedidos: " << valid << " válidos, " << seconds / std::max(total, 1)
                  << " s em média, " << heroes << " falas de heróis, " << dropped << " ações descartadas, " << leaks
                  << " segredos vazados, " << offstage << " falas de quem não está na cena\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "erro: " << e.what() << '\n';
        return 3;
    }
}
