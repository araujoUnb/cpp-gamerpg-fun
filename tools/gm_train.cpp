// Trains the weights of the tactical game master with the cross-entropy
// method: sample weight vectors around a mean, play each against the
// AutoPlayer on every scenario and party, keep the best quarter, repeat.
// Every candidate of a generation plays the same seeds, so they are compared
// on equal dice. Prints progress to stderr and the trained profile as JSON.
//
// With --calibrate it trains nothing: it tries a grid of blunder chances and
// monster HP for the tactical master in data/, and for every difficulty with
// calibration goals picks the setting closest to its target win rate whose
// every matchup stays at or above the floor. Writes <out>/<id>.json.
//
//   gamerpg_gm_train [--generations 200] [--population 64] [--seeds 40]
//                    [--threads N] [--eval-seeds 400] [--data DIR] [--out FILE]
//   gamerpg_gm_train --calibrate [--eval-seeds 1000] [--threads N] [--out DIR]
#include "combat/auto_player.hpp"
#include "combat/combat.hpp"
#include "data/content.hpp"
#include "data/default_hero.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace rpg;

namespace {

struct Options {
    int generations = 200;
    int population = 64;
    int seeds = 40;        // per scenario and party, each generation
    int eval_seeds = 400;  // per scenario and party, for the final comparison
    int threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    std::string data_dir = GAMERPG_DATA_DIR;
    std::string out;
    bool calibrate = false;
};

struct Matchup {
    const scenario::ScenarioDef* scenario;
    std::vector<character::CharacterSheet> party;
    std::string label;
};

struct Result {
    double fitness = 0;  // defeats plus partial credit for hero HP lost
    double defeat_rate = 0;
};

constexpr std::size_t kDims = monsters::kGmWeightFields.size();
using Vec = std::array<double, kDims>;
constexpr std::size_t kFixed = 0;  // expected_damage stays at its start value: scores only matter up to scale

monsters::GmWeights to_weights(const Vec& v) {
    monsters::GmWeights w;
    for (std::size_t d = 0; d < kDims; ++d) {
        w.*monsters::kGmWeightFields[d].second =
            static_cast<int>(std::lround(std::clamp(v[d], -double(data::kMaxGmWeight), double(data::kMaxGmWeight))));
    }
    return w;
}

Vec to_vec(const monsters::GmWeights& w) {
    Vec v{};
    for (std::size_t d = 0; d < kDims; ++d) v[d] = w.*monsters::kGmWeightFields[d].second;
    return v;
}

// Plays every matchup with `seeds` seeds starting at `seed0`.
Result evaluate(data::Content& content, const monsters::GameMasterDef& gm, const std::vector<Matchup>& matchups,
                std::uint64_t seed0, int seeds, int blunder = 0, int hp_percent = 100) {
    content.game_masters = {};
    content.game_masters.add(gm);
    monsters::DifficultyDef level;
    level.id = "train";
    level.game_master = gm.id;
    level.blunder = blunder;
    level.monster_hp_percent = hp_percent;
    content.difficulties = {};
    content.difficulties.add(level);
    const combat::AutoPlayer player;
    double fitness = 0;
    int defeats = 0, games = 0;
    for (const auto& mu : matchups) {
        for (int s = 0; s < seeds; ++s) {
            combat::Combat c(content, *mu.scenario, mu.party, seed0 + static_cast<std::uint64_t>(s), level.id);
            while (!c.finished()) c.decide(player.choose(c, *c.pending()));
            ++games;
            if (c.outcome() == combat::Outcome::Defeat) {
                ++defeats;
                fitness += 1;
                continue;
            }
            int hp = 0, max = 0;
            for (const auto& h : c.heroes()) {
                hp += h.status == combat::HeroStatus::Active ? h.sheet.current_hp : 0;
                max += character::max_hp(h.sheet, content.classes.get(h.sheet.class_id));
            }
            fitness += 0.3 * (1.0 - double(hp) / std::max(1, max));
        }
    }
    return {fitness / games, double(defeats) / games};
}

// Standard normal from the seeded generator (Box-Muller). Training only:
// the result is written to data/ as integers, so this needs no portability.
double normal(dice::Rng& rng) {
    const double u1 = (rng.next_u32() + 1.0) / 4294967297.0;
    const double u2 = rng.next_u32() / 4294967296.0;
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}

std::optional<Options> parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--calibrate") {
            o.calibrate = true;
            continue;
        }
        if (i + 1 >= argc) return std::nullopt;
        const std::string v = argv[++i];
        if (a == "--generations") o.generations = std::atoi(v.c_str());
        else if (a == "--population") o.population = std::atoi(v.c_str());
        else if (a == "--seeds") o.seeds = std::atoi(v.c_str());
        else if (a == "--eval-seeds") o.eval_seeds = std::atoi(v.c_str());
        else if (a == "--threads") o.threads = std::atoi(v.c_str());
        else if (a == "--data") o.data_dir = v;
        else if (a == "--out") o.out = v;
        else return std::nullopt;
    }
    if (o.generations < 0 || o.population < 4 || o.seeds < 1 || o.eval_seeds < 1 || o.threads < 1) return std::nullopt;
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    const auto opts = parse(argc, argv);
    if (!opts) {
        std::cerr << "uso: gamerpg_gm_train [--generations N] [--population N] [--seeds N] [--eval-seeds N]"
                     " [--threads N] [--data DIR] [--out FILE]\n"
                     "     gamerpg_gm_train --calibrate [--eval-seeds N] [--threads N] [--data DIR] [--out DIR]\n";
        return 2;
    }
    const auto base = data::load_content(opts->data_dir);
    const std::filesystem::path dir(opts->data_dir);

    // Every scenario, with every pair of classes that has a full level-1 deck.
    std::vector<scenario::ScenarioDef> scenarios;
    for (const auto& entry : std::filesystem::directory_iterator(dir / "scenarios")) {
        if (entry.path().extension() == ".json") scenarios.push_back(data::load_scenario(entry.path(), base));
    }
    std::sort(scenarios.begin(), scenarios.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::vector<std::string> classes;
    for (const auto& id : base.classes.ids()) {
        try {
            data::default_hero(base, "h", "h", id);
            classes.push_back(id);
        } catch (const std::exception&) {
        }
    }
    std::vector<Matchup> matchups;
    for (const auto& s : scenarios) {
        for (std::size_t a = 0; a < classes.size(); ++a) {
            for (std::size_t b = a; b < classes.size(); ++b) {
                matchups.push_back({&s,
                                    {data::default_hero(base, "h1", "h1", classes[a]), data::default_hero(base, "h2", "h2", classes[b])},
                                    s.id + ":" + classes[a] + "+" + classes[b]});
            }
        }
    }
    std::cerr << matchups.size() << " confrontos (" << scenarios.size() << " cenários, " << classes.size()
              << " classes), " << opts->threads << " threads\n";

    const auto* start = base.game_masters.find("tactical");
    monsters::GameMasterDef trained = start ? *start : monsters::GameMasterDef{};
    trained.id = "tactical";
    trained.name_key = "game_master.tactical.name";
    trained.tactical = true;
    if (!start) trained.weights.expected_damage = 10;

    std::vector<data::Content> contents(static_cast<std::size_t>(opts->threads), base);
    // Runs `jobs` evaluations in parallel; job(i, content) fills slot i.
    auto parallel = [&](int jobs, const auto& job) {
        std::atomic<int> next{0};
        std::vector<std::thread> pool;
        for (int t = 0; t < opts->threads; ++t) {
            pool.emplace_back([&, t] {
                for (int i = next++; i < jobs; i = next++) job(i, contents[static_cast<std::size_t>(t)]);
            });
        }
        for (auto& th : pool) th.join();
    };

    if (opts->calibrate) {
        if (!start) {
            std::cerr << "falta data/game_masters/tactical.json\n";
            return 1;
        }
        std::vector<std::pair<int, int>> grid;  // blunder, monster HP %
        for (int b = 0; b <= 1000; b += 100) {
            for (int hp = 50; hp <= 120; hp += 10) grid.emplace_back(b, hp);
        }
        // win[g][m]: hero win rate of matchup m at grid point g, on the same seeds for every point.
        std::vector<std::vector<double>> win(grid.size(), std::vector<double>(matchups.size()));
        parallel(static_cast<int>(grid.size() * matchups.size()), [&](int i, data::Content& content) {
            const auto g = static_cast<std::size_t>(i) / matchups.size(), m = static_cast<std::size_t>(i) % matchups.size();
            win[g][m] = 1 - evaluate(content, *start, {matchups[m]}, 9'000'000'000ull, opts->eval_seeds, grid[g].first,
                                     grid[g].second).defeat_rate;
        });
        int status = 0;
        for (const auto& level : base.difficulties) {
            if (level.target_win_percent == 0) continue;
            const double target = level.target_win_percent / 100.0, floor = level.floor_win_percent / 100.0;
            std::size_t best = 0;
            double best_gap = 1e9, best_min = -1;
            bool feasible = false;
            for (std::size_t g = 0; g < grid.size(); ++g) {
                double avg = 0, low = 1;
                for (double w : win[g]) {
                    avg += w / double(matchups.size());
                    low = std::min(low, w);
                }
                const bool ok = low >= floor;
                // Floor first; then the closest average; without any feasible point, the highest floor.
                if (ok && (!feasible || std::abs(avg - target) < best_gap)) {
                    best = g;
                    best_gap = std::abs(avg - target);
                    feasible = true;
                } else if (!feasible && low > best_min) {
                    best = g;
                    best_min = low;
                }
            }
            auto tuned = level;
            tuned.game_master = start->id;
            tuned.blunder = grid[best].first;
            tuned.monster_hp_percent = grid[best].second;
            double avg = 0;
            std::cerr << "\n" << level.id << ": deslize " << tuned.blunder << "‰, PV dos monstros " << tuned.monster_hp_percent
                      << "%" << (feasible ? "" : "  ** NENHUM PONTO RESPEITA O PISO **") << '\n';
            for (std::size_t m = 0; m < matchups.size(); ++m) {
                const double w = win[best][m];
                avg += w / double(matchups.size());
                std::cerr << "  " << std::left << std::setw(26) << matchups[m].label << std::lround(100 * w) << '%'
                          << (w < floor ? "  abaixo do piso" : "") << '\n';
            }
            std::cerr << "  média " << std::lround(100 * avg) << "% (alvo " << level.target_win_percent << "%, piso "
                      << level.floor_win_percent << "%)\n";
            if (!feasible) status = 3;
            const auto text = data::to_json(tuned).dump(2) + "\n";
            if (opts->out.empty()) {
                std::cout << text;
            } else {
                std::ofstream(std::filesystem::path(opts->out) / (level.id + ".json")) << text;
            }
        }
        return status;
    }

    Vec mean = to_vec(trained.weights);
    Vec sigma{};
    for (std::size_t d = 0; d < kDims; ++d) sigma[d] = d == kFixed ? 0.0 : std::max(10.0, std::abs(mean[d]));
    dice::Rng rng(20261025);
    const int elites = std::max(2, opts->population / 4);

    for (int g = 0; g < opts->generations; ++g) {
        std::vector<Vec> pop(static_cast<std::size_t>(opts->population));
        pop[0] = mean;  // the current mean competes too
        for (std::size_t p = 1; p < pop.size(); ++p) {
            for (std::size_t d = 0; d < kDims; ++d) pop[p][d] = mean[d] + sigma[d] * normal(rng);
        }
        std::vector<Result> res(pop.size());
        const std::uint64_t seed0 = 1'000'000ull * static_cast<std::uint64_t>(g + 1);
        parallel(opts->population, [&](int i, data::Content& content) {
            auto gm = trained;
            gm.weights = to_weights(pop[static_cast<std::size_t>(i)]);
            res[static_cast<std::size_t>(i)] = evaluate(content, gm, matchups, seed0, opts->seeds);
        });
        std::vector<std::size_t> order(pop.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](auto a, auto b) { return res[a].fitness > res[b].fitness; });

        Vec m{}, var{};
        for (int e = 0; e < elites; ++e) {
            for (std::size_t d = 0; d < kDims; ++d) m[d] += pop[order[static_cast<std::size_t>(e)]][d] / elites;
        }
        for (int e = 0; e < elites; ++e) {
            for (std::size_t d = 0; d < kDims; ++d) {
                const double x = pop[order[static_cast<std::size_t>(e)]][d] - m[d];
                var[d] += x * x / elites;
            }
        }
        // Smoothed update, with a noise floor that shrinks over time.
        const double floor = 4.0 * (1.0 - double(g) / std::max(1, opts->generations));
        for (std::size_t d = 0; d < kDims; ++d) {
            if (d == kFixed) continue;
            mean[d] = 0.7 * m[d] + 0.3 * mean[d];
            sigma[d] = 0.7 * std::sqrt(var[d]) + 0.3 * sigma[d] + floor;
        }
        const auto& best = res[order[0]];
        std::cerr << "geração " << std::setw(3) << g << ": melhor " << std::fixed << std::setprecision(4) << best.fitness
                  << " (derrotas " << best.defeat_rate << "), média atual " << res[0].fitness << "  pesos";
        for (std::size_t d = 0; d < kDims; ++d) std::cerr << ' ' << std::lround(mean[d]);
        std::cerr << '\n';
    }
    trained.weights = to_weights(mean);

    // Final comparison on seeds never used in training.
    monsters::GameMasterDef classic;
    const std::vector<const monsters::GameMasterDef*> finalists{&classic, start, &trained};
    const char* const names[] = {"clássico", "tático inicial", "tático treinado"};
    std::vector<std::vector<Result>> table(finalists.size(), std::vector<Result>(matchups.size()));
    const int jobs = static_cast<int>(finalists.size() * matchups.size());
    parallel(jobs, [&](int i, data::Content& content) {
        const auto f = static_cast<std::size_t>(i) / matchups.size(), m = static_cast<std::size_t>(i) % matchups.size();
        if (!finalists[f]) return;
        table[f][m] = evaluate(content, *finalists[f], {matchups[m]}, 7'000'000'000ull, opts->eval_seeds);
    });
    std::cerr << "\nvitórias dos heróis em " << opts->eval_seeds << " sementes novas por confronto\n";
    std::cerr << std::left << std::setw(28) << "confronto";
    for (const auto* n : names) std::cerr << std::setw(18) << n;
    std::cerr << '\n';
    for (std::size_t m = 0; m <= matchups.size(); ++m) {
        std::cerr << std::setw(28) << (m < matchups.size() ? matchups[m].label : "média");
        for (std::size_t f = 0; f < finalists.size(); ++f) {
            double d = 0;
            if (m < matchups.size()) {
                d = table[f][m].defeat_rate;
            } else {
                for (const auto& r : table[f]) d += r.defeat_rate / double(matchups.size());
            }
            std::cerr << std::setw(18) << (finalists[f] ? std::to_string(std::lround(100 * (1 - d))) + "%" : "-");
        }
        std::cerr << '\n';
    }

    const auto json = data::to_json(trained).dump(2) + "\n";
    if (opts->out.empty()) {
        std::cout << json;
    } else {
        std::ofstream(opts->out) << json;
    }
    return 0;
}
