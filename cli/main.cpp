// Terminal runner for a single scenario. A development tool to play and watch
// the rules before the Godot frontend exists. Content names come from the
// language file; the interface strings of this tool are fixed in Portuguese.
#include "combat/auto_player.hpp"
#include "combat/combat.hpp"
#include "data/content.hpp"
#include "data/data_error.hpp"
#include "data/default_hero.hpp"
#include "data/json_io.hpp"
#include "narrator/bank.hpp"
#include "narrator/narrator.hpp"
#include "narrator/situation.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace rpg;
using combat::Combat;
using combat::Decision;
using combat::Event;
using combat::Hex;

namespace {

struct Options {
    std::uint64_t seed = 1;
    bool automatic = false;
    int bench = 0;  // > 0: play this many silent auto battles and report speed
    std::string data_dir = GAMERPG_DATA_DIR;
    std::string scenario = "arena";
    std::string difficulty = "normal";
    std::vector<std::string> classes{"fighter", "wizard"};
    std::string narrator_url;    // llama-server, e.g. http://127.0.0.1:8080, or "banco" for the phrase bank
    std::string narration_file;  // written with --narrador, replayed without it
};

const char* const kHeroNames[] = {"Aria", "Bento", "Cora", "Davi"};

void usage() {
    std::cout << "uso: gamerpg_cli [--seed N] [--auto] [--bench N] [--classes fighter,wizard] [--scenario arena] [--dificuldade easy|normal|hard|standard] [--data DIR]\n"
                 "                   [--narrador URL] [--narracao ARQUIVO]\n"
                 "  --narrador URL      narra o combate com o modelo servido em URL (llama-server);\n"
                 "                      o banco de frases cobre as rodadas que o modelo não narrar\n"
                 "  --narrador banco    narra com o banco de frases, sem modelo (instantâneo)\n"
                 "  --narracao ARQUIVO  com --narrador grava a narração; sem ele, repete a gravada\n";
}

std::optional<Options> parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::optional<std::string> {
            if (i + 1 >= argc) return std::nullopt;
            return std::string(argv[++i]);
        };
        if (a == "--auto") {
            o.automatic = true;
        } else if (a == "--bench") {
            const auto v = next();
            if (!v) return std::nullopt;
            o.bench = std::atoi(v->c_str());
        } else if (a == "--seed") {
            const auto v = next();
            if (!v) return std::nullopt;
            o.seed = std::strtoull(v->c_str(), nullptr, 10);
        } else if (a == "--classes") {
            const auto v = next();
            if (!v) return std::nullopt;
            o.classes.clear();
            std::stringstream ss(*v);
            for (std::string item; std::getline(ss, item, ',');) o.classes.push_back(item);
        } else if (a == "--scenario") {
            const auto v = next();
            if (!v) return std::nullopt;
            o.scenario = *v;
        } else if (a == "--dificuldade") {
            const auto v = next();
            if (!v) return std::nullopt;
            o.difficulty = *v;
        } else if (a == "--narrador") {
            const auto v = next();
            if (!v) return std::nullopt;
            o.narrator_url = *v;
        } else if (a == "--narracao") {
            const auto v = next();
            if (!v) return std::nullopt;
            o.narration_file = *v;
        } else if (a == "--data") {
            const auto v = next();
            if (!v) return std::nullopt;
            o.data_dir = *v;
        } else {
            return std::nullopt;
        }
    }
    return o;
}

class Ui {
public:
    // Each monster type gets a distinct letter on the map: the first letter of its id not taken yet.
    explicit Ui(const Combat& c) : c_(c), text_(c.content().text) {
        for (const auto& m : c.monsters()) {
            if (letters_.count(m.type)) continue;
            for (char ch : m.type + "abcdefghijklmnopqrstuvwxyz") {
                const bool used = std::any_of(letters_.begin(), letters_.end(), [&](const auto& p) { return p.second == ch; });
                if (ch >= 'a' && ch <= 'z' && !used) {
                    letters_[m.type] = ch;
                    break;
                }
            }
        }
    }

    std::string mark(const combat::MonsterState& m) const {
        return std::string(1, letters_.at(m.type)) + m.id.substr(m.id.find('#') + 1);
    }

    std::string name(const std::string& id) const {
        for (const auto& h : c_.heroes()) {
            if (h.sheet.id == id) return h.sheet.name;
        }
        for (const auto& m : c_.monsters()) {
            if (m.id == id) return text_.get(c_.content().monsters.get(m.type).name_key) + " " + m.id.substr(m.id.find('#'));
        }
        if (const auto* m = c_.content().monsters.find(id)) return text_.get(m->name_key);
        return id;
    }

    std::string card_name(const std::string& id) const { return text_.get(c_.content().cards.get(id).name_key); }
    std::string condition_name(const std::string& id) const {
        const auto* def = c_.content().rules.find_condition(id);
        return def ? text_.get(def->name_key) : id;
    }

    std::string describe(const cards::ActionDef& a) const {
        std::ostringstream o;
        switch (a.type) {
            case cards::ActionType::Move: o << "mover " << a.value << (a.jump ? " (salto)" : ""); break;
            case cards::ActionType::Attack:
                o << "ataque " << a.value;
                if (a.range > 1) o << " alc. " << a.range;
                if (a.targets > 1) o << " x" << a.targets;
                if (a.advantage) o << " c/ vantagem";
                if (a.push) o << " empurra " << a.push;
                if (a.pull) o << " puxa " << a.pull;
                break;
            case cards::ActionType::Heal:
                o << "curar " << a.value;
                if (a.target == cards::TargetKind::Ally) o << " aliado alc. " << a.range;
                if (a.targets > 1) o << " x" << a.targets;
                break;
            case cards::ActionType::Condition:
                o << (a.target == cards::TargetKind::Self ? "em si" : a.target == cards::TargetKind::Ally ? "aliado" : "inimigo");
                if (a.target != cards::TargetKind::Self) o << " alc. " << a.range;
                if (a.targets > 1) o << " x" << a.targets;
                break;
        }
        for (const auto& cond : a.conditions) o << ", " << condition_name(cond.id) << " " << cond.rounds;
        return o.str();
    }

    std::string describe(const cards::CardHalf& h) const {
        std::string out;
        for (const auto& a : h.actions) out += (out.empty() ? "" : " + ") + describe(a);
        if (h.lose) out += " [PERDE]";
        return out;
    }

    std::string describe(const cards::HalfUse& u, cards::Half half) const {
        if (u.basic) return half == cards::Half::Top ? "ataque básico 2" : "mover básico 2";
        const auto& card = c_.content().cards.get(u.card);
        return card_name(u.card) + ": " + describe(card.half(half));
    }

    void draw_map() const {
        const auto& map = c_.scenario().map;
        int radius = 0;
        for (Hex h : map.hexes()) radius = std::max(radius, distance(h, {}));
        std::map<Hex, std::string> marks;
        for (std::size_t i = 0; i < c_.heroes().size(); ++i) {
            const auto& h = c_.heroes()[i];
            if (h.on_board()) marks[h.hex] = std::string(1, static_cast<char>('A' + i)) + (h.status == combat::HeroStatus::Active ? " " : "x");
        }
        for (const auto& m : c_.monsters()) {
            if (!m.alive) continue;
            marks[m.hex] = mark(m);
        }
        std::cout << '\n';
        for (int r = -radius; r <= radius; ++r) {
            std::cout << std::string(static_cast<std::size_t>(2 * std::abs(r)) + 2, ' ');
            for (int q = -radius; q <= radius; ++q) {
                const Hex h{q, r};
                if (!map.contains(h)) continue;
                std::string cell = " .";
                switch (map.terrain(h)) {
                    case combat::Terrain::Obstacle: cell = "##"; break;
                    case combat::Terrain::Difficult: cell = " ~"; break;
                    case combat::Terrain::Trap: cell = " ^"; break;
                    case combat::Terrain::Door: cell = "[]"; break;
                    case combat::Terrain::Open: break;
                }
                if (marks.count(h)) cell = marks[h];
                std::cout << cell << "  ";
            }
            std::cout << '\n';
        }
        std::cout << '\n';
    }

    void draw_status() const {
        const auto& content = c_.content();
        for (std::size_t i = 0; i < c_.heroes().size(); ++i) {
            const auto& h = c_.heroes()[i];
            const auto& cls = content.classes.get(h.sheet.class_id);
            std::cout << "  " << static_cast<char>('A' + i) << "  " << h.sheet.name << " (" << text_.get(cls.name_key)
                      << ")  PV " << h.sheet.current_hp << "/" << character::max_hp(h.sheet, cls) << "  CA "
                      << character::armor_class(h.sheet, cls) << "  mão " << h.cards.hand().size() << "  descarte "
                      << h.cards.discard().size() << "  perdidas " << h.cards.lost().size();
            switch (h.status) {
                case combat::HeroStatus::Dying: std::cout << "  [CAÍDO " << h.death_successes << "✓ " << h.death_failures << "✗]"; break;
                case combat::HeroStatus::Stable: std::cout << "  [ESTÁVEL]"; break;
                case combat::HeroStatus::Dead: std::cout << "  [MORTO]"; break;
                case combat::HeroStatus::Exhausted: std::cout << "  [EXAUSTO]"; break;
                case combat::HeroStatus::Active: break;
            }
            for (const auto& cond : h.conditions) std::cout << "  " << condition_name(cond.id) << "(" << cond.rounds << ")";
            std::cout << '\n';
        }
        for (const auto& m : c_.monsters()) {
            if (!m.alive) continue;
            const auto& def = content.monsters.get(m.type);
            std::cout << "  " << mark(m) << " " << name(m.id) << "  PV " << m.hp << "/"
                      << c_.max_hp_of(m) << "  CA " << def.armor_class;
            for (const auto& cond : m.conditions) std::cout << "  " << condition_name(cond.id) << "(" << cond.rounds << ")";
            std::cout << '\n';
        }
    }

    void print_event(const Event& e) const {
        using K = Event::Kind;
        switch (e.kind) {
            case K::RoundStarted: std::cout << "\n══════════ Rodada " << e.value << " ══════════\n"; break;
            case K::CardsSelected:
                std::cout << "  " << name(e.actor) << (e.flag ? " vai descansar (iniciativa " : " escolheu suas cartas (iniciativa ")
                          << e.value << ")\n";
                break;
            case K::MonsterCardDrawn: {
                std::string card = e.detail;
                for (const auto& mc : c_.content().monsters.get(e.actor).deck) {
                    if (mc.id == e.detail) card = text_.get(mc.name_key);
                }
                std::cout << "  " << name(e.actor) << " revela \"" << card << "\" (iniciativa " << e.value << ")\n";
                break;
            }
            case K::TurnStarted: std::cout << "\n▶ Vez de " << name(e.actor) << '\n'; break;
            case K::TurnSkipped: std::cout << "  " << name(e.actor) << " perde o turno (" << (e.detail == "stunned" ? "atordoado" : e.detail) << ")\n"; break;
            case K::HalfUsed:
                if (e.flag) std::cout << "  usa ação básica no lugar de " << card_name(e.detail) << '\n';
                else std::cout << "  usa " << card_name(e.detail) << (e.value == 0 ? " (cima)" : " (baixo)") << '\n';
                break;
            case K::DoorOpened: std::cout << "  " << name(e.actor) << " abre a porta em (" << e.to.q << "," << e.to.r << ")\n"; break;
            case K::TrapTriggered:
                std::cout << "  " << name(e.target) << " cai numa armadilha em (" << e.to.q << "," << e.to.r << ")!\n";
                break;
            case K::NoTarget: std::cout << "    (nenhum alvo ao alcance)\n"; break;
            case K::Moved:
                std::cout << "  " << name(e.actor) << (e.flag ? " é empurrado de (" : " move de (") << e.from.q << "," << e.from.r
                          << ") para (" << e.to.q << "," << e.to.r << ")\n";
                break;
            case K::AttackRolled:
                std::cout << "  " << name(e.actor) << " ataca " << name(e.target) << ": d20=" << e.roll
                          << (e.value > 0 ? " (vantagem)" : e.value < 0 ? " (desvantagem)" : "") << " total " << e.total
                          << " contra CA " << e.target_number << " → "
                          << (e.critical ? "CRÍTICO!" : e.flag ? "acerto" : e.roll == 1 ? "falha crítica" : "erro") << '\n';
                break;
            case K::Damaged: std::cout << "    " << name(e.target) << " sofre " << e.value << " de dano (PV " << e.value2 << ")\n"; break;
            case K::Healed: std::cout << "    " << name(e.target) << " recupera " << e.value << " PV (PV " << e.value2 << ")\n"; break;
            case K::ConditionApplied: std::cout << "    " << name(e.target) << " fica " << condition_name(e.detail) << " por " << e.value << '\n'; break;
            case K::ConditionResisted: std::cout << "    " << name(e.target) << " é imune a " << condition_name(e.detail) << '\n'; break;
            case K::ConditionEnded: std::cout << "    " << name(e.target) << " não está mais " << condition_name(e.detail) << '\n'; break;
            case K::MonsterDied: std::cout << "    ✝ " << name(e.target) << " foi derrotado\n"; break;
            case K::HeroDowned: std::cout << "    ✝ " << name(e.target) << " caiu!\n"; break;
            case K::DeathSave:
                std::cout << "  " << name(e.target) << " teste contra a morte: d20=" << e.roll << (e.flag ? " sucesso" : " falha")
                          << " (" << e.value << "✓ " << e.value2 << "✗)\n";
                break;
            case K::HeroStabilized: std::cout << "  " << name(e.target) << " está estável\n"; break;
            case K::HeroRevived: std::cout << "  " << name(e.target) << " volta à luta!\n"; break;
            case K::HeroDied: std::cout << "  " << name(e.target) << " morreu\n"; break;
            case K::HeroExhausted: std::cout << "  " << name(e.target) << " está exausto e deixa o combate\n"; break;
            case K::CardLost: std::cout << "    perde a carta " << card_name(e.detail) << '\n'; break;
            case K::LongRested: std::cout << "  " << name(e.actor) << " faz um descanso longo\n"; break;
            case K::ShortRested: std::cout << "  " << name(e.actor) << " faz um descanso curto\n"; break;
            case K::RoundEnded: break;
            case K::Victory: std::cout << "\n★★★ VITÓRIA! ★★★\n"; break;
            case K::Defeat: std::cout << "\n✗✗✗ DERROTA ✗✗✗\n"; break;
        }
    }

    std::vector<std::string> option_labels(const Decision& d) const {
        std::vector<std::string> out;
        switch (d.kind) {
            case Decision::Kind::SelectCards:
                for (const auto& s : d.selections) {
                    if (s.long_rest) out.push_back("descanso longo (iniciativa 99)");
                    else out.push_back(card_name(s.lead) + " + " + card_name(s.other));
                }
                break;
            case Decision::Kind::PlayTurn:
                for (const auto& p : d.plays) {
                    const auto top = "cima " + describe(p.top, cards::Half::Top);
                    const auto bottom = "baixo " + describe(p.bottom, cards::Half::Bottom);
                    out.push_back(p.top_first ? top + "  →  " + bottom : bottom + "  →  " + top);
                }
                break;
            case Decision::Kind::Move:
                for (Hex h : d.hexes) {
                    int nearest = 99;
                    for (const auto& m : c_.monsters()) {
                        if (m.alive) nearest = std::min(nearest, distance(h, m.hex));
                    }
                    out.push_back("(" + std::to_string(h.q) + "," + std::to_string(h.r) + ")  inimigo mais próximo a " +
                                  std::to_string(nearest));
                }
                break;
            case Decision::Kind::Target:
                for (const auto& t : d.targets) {
                    if (t.empty()) {
                        out.push_back("não escolher mais alvos");
                        continue;
                    }
                    int hp = 0;
                    for (const auto& m : c_.monsters()) if (m.id == t) hp = m.hp;
                    for (const auto& h : c_.heroes()) if (h.sheet.id == t) hp = h.sheet.current_hp;
                    out.push_back(name(t) + " (PV " + std::to_string(hp) + ")");
                }
                break;
            case Decision::Kind::LongRestLoss:
                for (const auto& card : d.cards) out.push_back(card_name(card));
                break;
            case Decision::Kind::ShortRest:
                out = {"não descansar", "descanso curto (perde uma carta aleatória do descarte)"};
                break;
        }
        return out;
    }

    std::string question(const Decision& d) const {
        const auto who = name(d.actor);
        switch (d.kind) {
            case Decision::Kind::SelectCards: return who + ": escolha duas cartas. A primeira define a iniciativa.";
            case Decision::Kind::PlayTurn: return who + ": como usar as cartas?";
            case Decision::Kind::Move: return who + ": para onde mover (" + describe(*d.action) + ")?";
            case Decision::Kind::Target: return who + ": escolha o alvo (" + describe(*d.action) + ")";
            case Decision::Kind::LongRestLoss: return who + ": qual carta do descarte perder no descanso?";
            case Decision::Kind::ShortRest: return who + ": descansar agora?";
        }
        return who;
    }

private:
    const Combat& c_;
    const data::TextTable& text_;
    std::map<std::string, char> letters_;
};

// Reads a number in [1, n]. Returns nullopt on end of input.
std::optional<std::size_t> ask(std::size_t n) {
    for (;;) {
        std::cout << "> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) return std::nullopt;
        try {
            const auto v = std::stoul(line);
            if (v >= 1 && v <= n) return v - 1;
        } catch (const std::exception&) {
        }
        std::cout << "  digite um número de 1 a " << n << '\n';
    }
}

// Card selection in two steps: the lead card, then the second one.
std::optional<std::size_t> ask_selection(const Ui& ui, const Decision& d) {
    std::vector<std::string> leads;
    bool can_rest = false;
    for (const auto& s : d.selections) {
        if (s.long_rest) can_rest = true;
        else if (std::find(leads.begin(), leads.end(), s.lead) == leads.end()) leads.push_back(s.lead);
    }
    std::cout << ui.question(d) << '\n';
    for (std::size_t i = 0; i < leads.size(); ++i) std::cout << "  " << i + 1 << ") " << ui.card_name(leads[i]) << '\n';
    if (can_rest) std::cout << "  " << leads.size() + 1 << ") descanso longo\n";
    const auto first = ask(leads.size() + (can_rest ? 1 : 0));
    if (!first) return std::nullopt;
    if (*first == leads.size()) {
        for (std::size_t i = 0; i < d.selections.size(); ++i) {
            if (d.selections[i].long_rest) return i;
        }
    }
    const auto& lead = leads[*first];
    std::vector<std::size_t> seconds;
    std::cout << "Segunda carta:\n";
    for (std::size_t i = 0; i < d.selections.size(); ++i) {
        if (!d.selections[i].long_rest && d.selections[i].lead == lead) {
            seconds.push_back(i);
            std::cout << "  " << seconds.size() << ") " << ui.card_name(d.selections[i].other) << '\n';
        }
    }
    const auto second = ask(seconds.size());
    if (!second) return std::nullopt;
    return seconds[*second];
}

void show_narration(const narrator::Narration& n) {
    std::cout << "\n  » " << n.text << '\n';
    for (const auto& l : n.lines) std::cout << "    — " << l.speaker << ": " << l.text << '\n';
}

void show_hand(const Ui& ui, const Combat& c, const std::string& hero_id) {
    for (const auto& h : c.heroes()) {
        if (h.sheet.id != hero_id) continue;
        std::cout << "Mão de " << h.sheet.name << ":\n";
        for (const auto& id : h.cards.hand()) {
            const auto& card = c.content().cards.get(id);
            std::cout << "  [" << card.initiative << "] " << ui.card_name(id) << "\n      cima:  " << ui.describe(card.top)
                      << "\n      baixo: " << ui.describe(card.bottom) << '\n';
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const auto opts = parse_args(argc, argv);
    if (!opts) {
        usage();
        return 2;
    }
    try {
        const auto content = data::load_content(opts->data_dir);
        auto scenario = data::load_scenario(std::filesystem::path(opts->data_dir) / "scenarios" / (opts->scenario + ".json"), content);

        std::vector<character::CharacterSheet> party;
        for (std::size_t i = 0; i < opts->classes.size() && i < 4; ++i) {
            party.push_back(data::default_hero(content, "h" + std::to_string(i + 1), kHeroNames[i], opts->classes[i]));
        }

        if (opts->bench > 0) {
            const combat::AutoPlayer player;
            int wins = 0;
            long decisions = 0, events = 0, rounds = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < opts->bench; ++i) {
                Combat c(content, scenario, party, opts->seed + static_cast<std::uint64_t>(i), opts->difficulty);
                while (!c.finished()) {
                    c.decide(player.choose(c, *c.pending()));
                    ++decisions;
                }
                wins += c.outcome() == combat::Outcome::Victory;
                events += static_cast<long>(c.events().size());
                rounds += c.round();
            }
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::cout << opts->bench << " batalhas em " << secs << " s: " << opts->bench / secs << " batalhas/s, "
                      << static_cast<double>(decisions) / secs << " decisões/s\n"
                      << "vitórias " << wins << " (" << 100.0 * wins / opts->bench << "%), " << static_cast<double>(rounds) / opts->bench
                      << " rodadas e " << events / opts->bench << " eventos por batalha\n";
            return 0;
        }

        Combat combat(content, std::move(scenario), std::move(party), opts->seed, opts->difficulty);
        const Ui ui(combat);
        const combat::AutoPlayer autoplayer;
        std::cout << content.text.get(combat.scenario().name_key) << "  (semente " << opts->seed << ", "
                  << (combat.difficulty().name_key.empty() ? "padrão" : content.text.get(combat.difficulty().name_key)) << ")\n";

        // Narrator: asks the model for each round in the background, with the
        // phrase bank telling what it misses; or the bank alone; or replays a
        // recorded narration.
        std::unique_ptr<narrator::Narrator> teller;
        std::optional<narrator::PhraseBank> bank;
        std::optional<narrator::BankNarrator> bank_teller;
        std::chrono::milliseconds narration_wait{0};
        std::vector<narrator::Narration> told;      // shown, saved with --narracao
        std::vector<narrator::Narration> recorded;  // replayed
        std::size_t next_recorded = 0;
        if (!opts->narrator_url.empty()) {
            bank = narrator::load_bank(std::filesystem::path(opts->data_dir) / "narrator" / "banco.json");
            bank_teller.emplace(*bank, opts->seed);
        }
        if (!opts->narrator_url.empty() && opts->narrator_url != "banco") {
            auto cfg = narrator::load_config(opts->data_dir);
            narration_wait = std::chrono::milliseconds(cfg.timeout_ms);
            teller = std::make_unique<narrator::Narrator>(
                cfg, narrator::make_http_transport(opts->narrator_url, narration_wait),
                narrator::load_warnings(content.text));
        } else if (!opts->narration_file.empty()) {
            recorded = narrator::log_from_json(data::read_json_file(opts->narration_file));
        }

        std::size_t shown = 0;
        auto tell = [&](narrator::Narration n) {
            n.after_event = shown;
            show_narration(n);
            told.push_back(std::move(n));
        };
        // The moment of the latest request to the model, while still untold.
        struct Untold {
            bool opening;
            std::size_t first_event, end_event;
        };
        std::optional<Untold> untold;
        // `last_chance`: the model's narration is no longer awaited, so the bank tells the moment.
        auto take_narration = [&](std::chrono::milliseconds wait, bool last_chance) {
            if (!teller) return;
            auto n = teller->receive(wait);
            if (!n && !last_chance) return;  // may still be on its way
            if (untold && narrator::needs_cover(n)) {
                n = narrator::cover(std::move(n), untold->opening ? bank_teller->opening(combat)
                                                                  : bank_teller->round(combat, untold->first_event, untold->end_event));
            }
            untold.reset();
            if (n) tell(std::move(*n));
        };
        auto replay_narration = [&] {
            for (; next_recorded < recorded.size() && recorded[next_recorded].after_event <= shown; ++next_recorded) {
                show_narration(recorded[next_recorded]);
            }
        };
        auto save_narration = [&] {
            if ((!teller && !bank_teller) || opts->narration_file.empty()) return;
            std::ofstream(opts->narration_file) << narrator::to_json(told).dump(2) << '\n';
        };

        if (teller) {
            teller->request(narrator::opening_situation(combat));
            untold = Untold{true, 0, 0};
            take_narration(narration_wait, true);
        } else if (bank_teller) {
            if (auto n = bank_teller->opening(combat)) tell(std::move(*n));
        }
        replay_narration();

        std::size_t round_first = 0;
        bool narrated_end = false;
        auto flush_events = [&] {
            for (; shown < combat.events().size();) {
                const auto& e = combat.events()[shown];
                ui.print_event(e);
                ++shown;
                replay_narration();
                const bool end = e.kind == Event::Kind::Victory || e.kind == Event::Kind::Defeat;
                if ((!teller && !bank_teller) || narrated_end || (!end && e.kind != Event::Kind::RoundEnded)) continue;
                if (!teller) {
                    if (auto n = bank_teller->round(combat, round_first, shown)) tell(std::move(*n));
                    round_first = shown;
                    narrated_end = end;
                    continue;
                }
                // A newer request drops the older one, so its round is told now.
                take_narration(std::chrono::milliseconds(0), true);
                teller->request(narrator::round_situation(combat, round_first, shown));
                untold = Untold{false, round_first, shown};
                round_first = shown;
                narrated_end = end;
                // Watching the auto player: wait for the narration; playing: it shows up when ready.
                if (opts->automatic) take_narration(narration_wait, true);
            }
        };

        while (!combat.finished()) {
            flush_events();
            take_narration(std::chrono::milliseconds(0), false);
            const auto& d = *combat.pending();
            if (opts->automatic) {
                const auto i = autoplayer.choose(combat, d);
                if (d.kind == Decision::Kind::SelectCards && d.actor == combat.heroes().front().sheet.id) {
                    ui.draw_map();
                    ui.draw_status();
                }
                combat.decide(i);
                continue;
            }

            std::optional<std::size_t> choice;
            if (d.kind == Decision::Kind::SelectCards) {
                ui.draw_map();
                ui.draw_status();
                show_hand(ui, combat, d.actor);
                choice = ask_selection(ui, d);
            } else {
                if (d.kind == Decision::Kind::Move || d.kind == Decision::Kind::Target) ui.draw_map();
                std::cout << ui.question(d) << '\n';
                const auto labels = ui.option_labels(d);
                for (std::size_t i = 0; i < labels.size(); ++i) std::cout << "  " << i + 1 << ") " << labels[i] << '\n';
                choice = ask(labels.size());
            }
            if (!choice) {
                save_narration();
                std::cout << "\nAté a próxima!\n";
                return 0;
            }
            combat.decide(*choice);
        }
        flush_events();
        take_narration(narration_wait, true);
        save_narration();
        ui.draw_map();
        ui.draw_status();
        std::cout << "Rodadas: " << combat.round() << '\n';
        return combat.outcome() == combat::Outcome::Victory ? 0 : 1;
    } catch (const data::DataError& e) {
        std::cerr << "erro nos dados: " << e.what() << '\n';
        return 3;
    } catch (const std::exception& e) {
        std::cerr << "erro: " << e.what() << '\n';
        return 3;
    }
}
