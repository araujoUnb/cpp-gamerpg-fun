// Plays whole auto battles with a narrator, over several scenarios, class
// pairs and seeds, and measures whether the narration keeps up: time per
// round, repeated sentences within a battle, leftover marks.
//   gamerpg_narrator_play --narrador banco|URL --saida partidas.jsonl
// With a model it runs on lab10 next to it (see treino/llm_partidas.sbatch).
#include "combat/auto_player.hpp"
#include "combat/combat.hpp"
#include "data/content.hpp"
#include "data/default_hero.hpp"
#include "narrator/bank.hpp"
#include "narrator/narrator.hpp"
#include "narrator/situation.hpp"
#include "narrator/text.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace rpg;
using nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

struct Options {
    std::string narrator = "banco";
    bool fallback = false;  // --reserva banco: the bank covers rounds the model misses
    std::string data_dir = GAMERPG_DATA_DIR;
    std::string out;
    std::string difficulty = "normal";
    std::vector<std::string> scenarios{"arena", "ruins"};
    std::vector<std::vector<std::string>> parties{{"fighter", "wizard"}, {"cleric", "cleric"}, {"rogue", "wizard"}};
    int seeds = 2;
    int history = -1;  // previous narrations sent to the model; -1 = mestre.json
};

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    for (std::string item; std::getline(ss, item, sep);) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

bool parse_args(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) return false;
        const std::string v = argv[++i];
        if (a == "--narrador") o.narrator = v;
        else if (a == "--data") o.data_dir = v;
        else if (a == "--saida") o.out = v;
        else if (a == "--dificuldade") o.difficulty = v;
        else if (a == "--cenarios") o.scenarios = split(v, ',');
        else if (a == "--sementes") o.seeds = std::atoi(v.c_str());
        else if (a == "--anteriores") o.history = std::atoi(v.c_str());
        else if (a == "--reserva") {
            if (v != "banco") return false;
            o.fallback = true;
        }
        else if (a == "--classes") {
            o.parties.clear();
            for (const auto& p : split(v, ';')) o.parties.push_back(split(p, ','));
        } else return false;
    }
    return !o.out.empty() && o.seeds > 0;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<std::size_t>(p * static_cast<double>(v.size())))];
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse_args(argc, argv, o)) {
        std::cout << "uso: gamerpg_narrator_play --saida ARQUIVO.jsonl [--narrador banco|URL] [--cenarios arena,ruins]\n"
                     "       [--classes \"fighter,wizard;cleric,cleric\"] [--sementes N] [--dificuldade normal] [--anteriores N] [--reserva banco] [--data DIR]\n";
        return 2;
    }
    try {
        const auto content = data::load_content(o.data_dir);
        const bool use_bank = o.narrator == "banco";
        std::optional<narrator::PhraseBank> bank;
        std::unique_ptr<narrator::Narrator> model;
        std::chrono::milliseconds limit{0};
        if (use_bank || o.fallback) {
            bank = narrator::load_bank(std::filesystem::path(o.data_dir) / "narrator" / "banco.json");
        }
        if (!use_bank) {
            auto cfg = narrator::load_config(o.data_dir);
            if (o.history >= 0) cfg.history = o.history;
            limit = std::chrono::milliseconds(cfg.timeout_ms);
            model = std::make_unique<narrator::Narrator>(cfg, narrator::make_http_transport(o.narrator, limit),
                                                         narrator::load_warnings(content.text));
        }

        std::ofstream out(o.out);
        const combat::AutoPlayer player;
        const char* const names[] = {"Aria", "Bento", "Cora", "Davi"};
        std::vector<double> latencies;
        int narrated = 0, missing = 0, leftover = 0, battles = 0, from_bank = 0;
        long total_sentences = 0, repeated = 0, chars = 0;

        for (const auto& scenario_id : o.scenarios) {
            const auto scenario = data::load_scenario(std::filesystem::path(o.data_dir) / "scenarios" / (scenario_id + ".json"), content);
            for (const auto& classes : o.parties) {
                for (int seed = 1; seed <= o.seeds; ++seed) {
                    std::vector<character::CharacterSheet> party;
                    for (std::size_t i = 0; i < classes.size() && i < 4; ++i) {
                        party.push_back(data::default_hero(content, "h" + std::to_string(i + 1), names[i], classes[i]));
                    }
                    combat::Combat c(content, scenario, party, static_cast<std::uint64_t>(seed), o.difficulty);
                    std::optional<narrator::BankNarrator> teller;
                    if (bank) teller.emplace(*bank, static_cast<std::uint64_t>(seed));

                    std::vector<std::string> seen;  // sentences already told in this battle
                    json texts = json::array();
                    json situations = json::array();  // what each narration was about, for judging it later
                    using Ask = std::function<std::optional<narrator::Narration>(const json&)>;
                    // `spare` is the bank's narration, used when the model leaves the round untold.
                    auto narrate = [&](json situation, const Ask& ask, const std::function<std::optional<narrator::Narration>()>& spare) {
                        const auto t0 = Clock::now();
                        auto n = ask(situation);
                        situations.push_back(std::move(situation));
                        bool spared = false;
                        if (narrator::needs_cover(n) && spare) {
                            auto b = spare();
                            spared = b.has_value();
                            from_bank += spared;
                            n = narrator::cover(std::move(n), std::move(b));
                        }
                        const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
                        // Lines or actions alone leave the round untold.
                        if (!n || n->text.empty()) {
                            ++missing;
                            texts.push_back(nullptr);
                            return;
                        }
                        ++narrated;
                        latencies.push_back(secs);
                        chars += static_cast<long>(n->text.size());
                        leftover += n->text.find('{') != std::string::npos;
                        for (const auto& s : narrator::sentences(n->text)) {
                            ++total_sentences;
                            if (std::find(seen.begin(), seen.end(), s) != seen.end()) ++repeated;
                            else seen.push_back(s);
                        }
                        json t{{"s", std::round(secs * 1000) / 1000}, {"texto", n->text}};
                        if (spared) t["banco"] = true;
                        texts.push_back(std::move(t));
                    };
                    auto from_model = [&](const json& situation) {
                        model->request(situation);
                        // Wait a little past the limit so slow replies are still measured.
                        return model->receive(limit * 2);
                    };

                    const std::function<std::optional<narrator::Narration>()> none;
                    if (use_bank) narrate(narrator::opening_situation(c), [&](const json&) { return teller->opening(c); }, none);
                    else narrate(narrator::opening_situation(c), from_model, o.fallback ? [&] { return teller->opening(c); } : none);

                    std::size_t first = 0, shown = 0;
                    bool ended = false;
                    auto flush = [&] {
                        for (; shown < c.events().size();) {
                            const auto& e = c.events()[shown++];
                            const bool end = e.kind == combat::Event::Kind::Victory || e.kind == combat::Event::Kind::Defeat;
                            if (ended || (!end && e.kind != combat::Event::Kind::RoundEnded)) continue;
                            const auto from_bank_round = [&] { return teller->round(c, first, shown); };
                            auto situation = narrator::round_situation(c, first, shown);
                            if (use_bank) narrate(std::move(situation), [&](const json&) { return from_bank_round(); }, none);
                            else narrate(std::move(situation), from_model, o.fallback ? from_bank_round : none);
                            first = shown;
                            ended = end;
                        }
                    };
                    while (!c.finished()) {
                        flush();
                        c.decide(player.choose(c, *c.pending()));
                    }
                    flush();
                    ++battles;

                    std::string party_id;
                    for (const auto& cl : classes) party_id += (party_id.empty() ? "" : "+") + cl;
                    out << json{{"cenario", scenario_id}, {"classes", party_id}, {"semente", seed}, {"rodadas", c.round()},
                                {"resultado", c.outcome() == combat::Outcome::Victory ? "vitoria" : "derrota"},
                                {"narracoes", texts}, {"situacoes", situations}}.dump()
                        << '\n' << std::flush;
                    std::cout << scenario_id << " " << party_id << " semente " << seed << ": " << c.round() << " rodadas\n"
                              << std::flush;
                }
            }
        }

        const auto within = [&](double s) {
            return 100.0 * static_cast<double>(std::count_if(latencies.begin(), latencies.end(), [&](double x) { return x <= s; })) /
                   std::max(1, narrated + missing);
        };
        std::cout << "\n" << battles << " partidas, " << narrated << " narrações (" << from_bank << " do banco), " << missing << " sem narração\n"
                  << "tempo: mediana " << percentile(latencies, 0.5) << " s, p90 " << percentile(latencies, 0.9) << " s, máximo "
                  << percentile(latencies, 1.0) << " s\n"
                  << "a tempo: " << within(20) << "% até 20 s, " << within(30) << "% até 30 s, " << within(45) << "% até 45 s\n"
                  << "frases repetidas na mesma partida: " << repeated << " de " << total_sentences << " ("
                  << 100.0 * static_cast<double>(repeated) / static_cast<double>(std::max(1L, total_sentences)) << "%)\n"
                  << "tamanho médio: " << chars / std::max(1, narrated) << " caracteres, marcadores não preenchidos: " << leftover
                  << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "erro: " << e.what() << '\n';
        return 3;
    }
}
