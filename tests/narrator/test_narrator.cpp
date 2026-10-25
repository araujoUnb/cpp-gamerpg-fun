#include "narrator/bank.hpp"
#include "narrator/narrator.hpp"
#include "narrator/situation.hpp"
#include "narrator/text.hpp"

#include "combat/auto_player.hpp"
#include "combat/combat.hpp"
#include "data/content.hpp"
#include "data/data_error.hpp"
#include "data/default_hero.hpp"
#include "data/json_io.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <vector>

using namespace rpg;
using namespace rpg::narrator;
using combat::Event;
using nlohmann::json;
using namespace std::chrono_literals;

namespace {

const data::Content& content() {
    static const auto c = data::load_content(GAMERPG_DATA_DIR);
    return c;
}

combat::Combat arena_combat(std::uint64_t seed = 1) {
    auto scenario = data::load_scenario(std::filesystem::path(GAMERPG_DATA_DIR) / "scenarios" / "arena.json", content());
    std::vector<character::CharacterSheet> party{data::default_hero(content(), "h1", "Aria", "fighter"),
                                                 data::default_hero(content(), "h2", "Bento", "wizard")};
    return combat::Combat(content(), std::move(scenario), std::move(party), seed);
}

// Plays until the first round ends; returns the index just past its RoundEnded event.
std::size_t play_first_round(combat::Combat& c) {
    const combat::AutoPlayer player;
    for (;;) {
        for (std::size_t i = 0; i < c.events().size(); ++i) {
            if (c.events()[i].kind == Event::Kind::RoundEnded) return i + 1;
        }
        REQUIRE(!c.finished());
        c.decide(player.choose(c, *c.pending()));
    }
}

std::string reply_with(const json& answer) {
    return json{{"choices", json::array({json{{"message", json{{"role", "assistant"}, {"content", answer.dump()}}}}})}}.dump();
}

const Warnings kWarning{"descartada: {tipo}", "teste extra", "repetida"};

// Answers every request with a fixed narration and keeps the bodies it got.
class FakeTransport : public Transport {
public:
    explicit FakeTransport(std::vector<std::string>* bodies) : bodies_(bodies) {}
    std::optional<std::string> post(const std::string& body) override {
        bodies_->push_back(body);
        const auto user = json::parse(body)["messages"][1]["content"].get<std::string>();
        const auto situation = json::parse(user.substr(user.find('{')));
        return reply_with(json{{"narracao", "Narração de " + situation["id"].get<std::string>()},
                               {"falas", json::array()},
                               {"acoes", json::array({json{{"tipo", "dar_ouro"}, {"quantia", 5}, {"motivo", "x"}}})}});
    }

private:
    std::vector<std::string>* bodies_;
};

// Holds every request until released or cancelled, like a slow model.
class SlowTransport : public Transport {
public:
    std::optional<std::string> post(const std::string&) override {
        std::unique_lock lock(m_);
        ++started_;
        cv_.notify_all();
        cv_.wait(lock, [this] { return released_ > 0 || cancelled_; });
        if (cancelled_) return std::nullopt;
        --released_;
        ++n_;
        return reply_with(json{{"narracao", "resposta " + std::to_string(n_)}, {"falas", json::array()}, {"acoes", json::array()}});
    }
    void cancel() override {
        std::lock_guard lock(m_);
        cancelled_ = true;
        cv_.notify_all();
    }
    void release() {
        std::lock_guard lock(m_);
        ++released_;
        cv_.notify_all();
    }
    void wait_started(int n) {
        std::unique_lock lock(m_);
        cv_.wait(lock, [&] { return started_ >= n; });
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    int started_ = 0;
    int released_ = 0;
    int n_ = 0;
    bool cancelled_ = false;
};

class DownTransport : public Transport {
public:
    std::optional<std::string> post(const std::string&) override { return std::nullopt; }
};

Config test_config() {
    auto cfg = load_config(GAMERPG_DATA_DIR);
    cfg.history = 1;
    return cfg;
}

}  // namespace

TEST_CASE("fill replaces every placeholder and truncate_utf8 keeps whole characters") {
    std::string s = "{a} e {a} com {b}";
    fill(s, "a", "x{a}");
    fill(s, "b", "ção");
    CHECK(s == "x{a} e x{a} com ção");

    std::string t = "ação";
    truncate_utf8(t, 2);
    CHECK(t == "aç");
    truncate_utf8(t, 10);
    CHECK(t == "aç");
}

TEST_CASE("sentences that copy an earlier narration are dropped") {
    CHECK(sentences("Um golpe seco. E outro!  Fim de tudo") == std::vector<std::string>{"Um golpe seco.", "E outro!", "Fim de tudo"});
    const std::vector<std::string> before{"O esqueleto arqueiro puxa a corda e mira no mago. Tudo treme."};
    // Same words in another order and case: dropped. New sentence and short ones: kept.
    CHECK(drop_repeated("Tudo treme. O ESQUELETO arqueiro puxa a corda, mira no mago! Bento ergue o cajado contra o brutamontes.",
                        before) == "Tudo treme. Bento ergue o cajado contra o brutamontes.");
    CHECK(drop_repeated("Nada se repete aqui hoje.", {}) == "Nada se repete aqui hoje.");
}

TEST_CASE("the narrator config comes from data/narrator") {
    const auto cfg = load_config(GAMERPG_DATA_DIR);
    CHECK(!cfg.system.empty());
    CHECK(cfg.request.find("{situacao}") != std::string::npos);
    CHECK(cfg.schema["required"] == json::array({"narracao", "falas", "acoes"}));
    CHECK(cfg.timeout_ms > 0);
}

TEST_CASE("events become lines from the language file; events without text are left out") {
    auto c = arena_combat();
    const auto end = play_first_round(c);

    Event attack;
    attack.kind = Event::Kind::AttackRolled;
    attack.actor = "h1";
    attack.target = c.monsters().front().id;
    attack.roll = 15;
    attack.total = 18;
    attack.target_number = 13;
    attack.flag = true;
    const auto line = describe_event(c, attack);
    CHECK(line.find("Aria ataca") == 0);
    CHECK(line.find("18 contra CA 13, acerta") != std::string::npos);
    CHECK(line.find('{') == std::string::npos);

    // Hidden information never reaches the model: chosen cards are not described.
    Event chosen;
    chosen.kind = Event::Kind::CardsSelected;
    chosen.actor = "h1";
    CHECK(describe_event(c, chosen).empty());

    const auto s = round_situation(c, 0, end);
    CHECK(s["id"] == "rodada_1");
    CHECK(s["momento"] == "combate");
    CHECK(s["grupo"].size() == 2);
    CHECK(s["grupo"][0]["nome"] == "Aria");
    CHECK(s["inimigos"].size() >= 1);
    CHECK(!s["eventos"].empty());
    CHECK(s["acoes_permitidas"].empty());
    for (const auto& e : s["eventos"]) CHECK(e.get<std::string>().find('{') == std::string::npos);
}

TEST_CASE("the closing situation is marked when the events include the end of the combat") {
    auto c = arena_combat(3);
    const combat::AutoPlayer player;
    while (!c.finished()) c.decide(player.choose(c, *c.pending()));
    const auto s = round_situation(c, 0, c.events().size());
    CHECK(s["id"] == "fim");
    const auto last = s["eventos"].back().get<std::string>();
    CHECK((last == content().text.get("narrator.event.victory") || last == content().text.get("narrator.event.defeat")));
}

TEST_CASE("the request asks for the reply schema and states the narration size") {
    const auto cfg = load_config(GAMERPG_DATA_DIR);
    auto c = arena_combat();
    const auto opening = opening_situation(c);
    const auto body = request_body(cfg, opening);
    CHECK(body["response_format"]["json_schema"]["schema"] == reply_schema(cfg, opening));
    CHECK(body["messages"][0]["content"] == cfg.system);
    const auto user = body["messages"][1]["content"].get<std::string>();
    CHECK(user.find(cfg.size_scene) != std::string::npos);
    CHECK(user.find("\"abertura\"") != std::string::npos);
}

TEST_CASE("replies are checked: bad JSON is refused, limits are applied, disallowed actions are dropped") {
    auto cfg = load_config(GAMERPG_DATA_DIR);
    cfg.max_text_chars = 5;
    cfg.max_lines = 1;
    const json situation{{"id", "x"}, {"acoes_permitidas", json::array({json{{"tipo", "teste"}}})}};

    CHECK(!parse_reply(cfg, situation, "not json", kWarning));
    CHECK(!parse_reply(cfg, situation, reply_with(json{{"falas", json::array()}}), kWarning));
    CHECK(!parse_reply(cfg, situation, json{{"choices", json::array({json{{"message", json{{"content", "{oops"}}}}})}}.dump(), kWarning));

    const auto n = parse_reply(cfg, situation,
                               reply_with(json{{"narracao", "Uma narração longa"},
                                               {"falas", json::array({json{{"personagem", "Marta"}, {"texto", "Olá"}},
                                                                      json{{"personagem", "Rui"}, {"texto", "Oi"}}})},
                                               {"acoes", json::array({json{{"tipo", "teste"}, {"cd", 12}, {"motivo", "a"}},
                                                                      json{{"tipo", "dar_ouro"}, {"motivo", "b"}},
                                                                      json{{"tipo", "teste"}, {"cd", 10}, {"motivo", "c"}}})}}),
                               kWarning);
    REQUIRE(n);
    CHECK(n->situation == "x");
    CHECK(n->text == "Uma n");
    CHECK(n->lines == std::vector<Line>{{"Marta", "Olá"}});
    REQUIRE(n->actions.size() == 1);
    CHECK(n->actions[0]["tipo"] == "teste");
    CHECK(n->warnings == std::vector<std::string>{"descartada: dar_ouro", "teste extra"});
}

TEST_CASE("the narration log round-trips through JSON") {
    Narration n;
    n.situation = "rodada_2";
    n.text = "O lobo uiva.";
    n.lines = {{"Aria", "Para trás!"}};
    n.actions = {json{{"tipo", "teste"}}};
    n.warnings = {"aviso"};
    n.after_event = 42;
    const auto back = log_from_json(to_json(std::vector<Narration>{n}));
    REQUIRE(back.size() == 1);
    CHECK(back[0].situation == n.situation);
    CHECK(back[0].text == n.text);
    CHECK(back[0].lines == n.lines);
    CHECK(back[0].actions == n.actions);
    CHECK(back[0].warnings == n.warnings);
    CHECK(back[0].after_event == 42);
    CHECK_THROWS_AS(log_from_json(json{{"x", 1}}), data::DataError);
    CHECK_THROWS_AS(log_from_json(json::array({json{{"situacao", "a"}}})), data::DataError);
}

TEST_CASE("the narrator answers in the background and tells the model what it said and what was dropped") {
    std::vector<std::string> bodies;
    Narrator n(test_config(), std::make_unique<FakeTransport>(&bodies), kWarning);

    n.request(json{{"id", "abertura"}, {"momento", "cena"}});
    const auto first = n.receive(5s);
    REQUIRE(first);
    CHECK(first->text == "Narração de abertura");
    CHECK(first->warnings == std::vector<std::string>{"descartada: dar_ouro"});

    n.request(json{{"id", "rodada_1"}, {"momento", "combate"}});
    const auto second = n.receive(5s);
    REQUIRE(second);
    CHECK(second->text == "Narração de rodada_1");

    REQUIRE(bodies.size() == 2);
    const auto user = json::parse(bodies[1])["messages"][1]["content"].get<std::string>();
    CHECK(user.find("Narração de abertura") != std::string::npos);  // history
    CHECK(user.find("descartada: dar_ouro") != std::string::npos);  // warning
    CHECK(!n.receive());
}

TEST_CASE("a slow model never blocks the game and late narrations are dropped") {
    auto slow = std::make_unique<SlowTransport>();
    auto* transport = slow.get();
    {
        Narrator n(test_config(), std::move(slow), kWarning);
        n.request(json{{"id", "rodada_1"}});
        transport->wait_started(1);
        CHECK(!n.receive(20ms));  // time limit passes: the game keeps the default text

        n.request(json{{"id", "rodada_2"}});  // the game moved on
        transport->release();                // the answer about round 1 arrives late
        transport->wait_started(2);
        CHECK(!n.receive(20ms));
        transport->release();
        const auto got = n.receive(5s);
        REQUIRE(got);
        CHECK(got->situation == "rodada_2");

        n.request(json{{"id", "rodada_3"}});
        transport->wait_started(3);
    }  // destroyed with a request in flight: cancels it instead of waiting
}

TEST_CASE("with the server down the game does not wait for the time limit") {
    Narrator n(test_config(), std::make_unique<DownTransport>(), kWarning);
    n.request(json{{"id", "abertura"}});
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!n.receive(60s));
    CHECK(std::chrono::steady_clock::now() - t0 < 10s);
}

TEST_CASE("the reply schema only lets the model write the actions the situation allows") {
    const auto cfg = load_config(GAMERPG_DATA_DIR);

    const auto none = reply_schema(cfg, json{{"acoes_permitidas", json::array()}});
    CHECK(none["properties"]["acoes"]["maxItems"] == 0);
    CHECK(none["properties"]["falas"]["maxItems"] == cfg.max_lines);
    CHECK(none["required"] == cfg.schema["required"]);

    const json situation{{"grupo", json::array({json{{"nome", "Aria"}}, json{{"nome", "Bento"}}})},
                         {"acoes_permitidas", json::array({json{{"tipo", "teste"}, {"atributos", {"car"}}, {"cd", {10, 15}}},
                                                           json{{"tipo", "dar_ouro"}, {"maximo", 20}},
                                                           json{{"tipo", "abrir_loja"}},
                                                           json{{"tipo", "dar_item"}},       // malformed: no items
                                                           json{{"tipo", "voar"}}})}};      // unknown
    const auto s = reply_schema(cfg, situation);
    const auto& options = s["properties"]["acoes"]["items"]["anyOf"];
    REQUIRE(options.size() == 3);

    const auto& test = options[0];
    CHECK(test["properties"]["tipo"]["const"] == "teste");
    CHECK(test["properties"]["atributo"]["enum"] == json::array({"car"}));
    CHECK(test["properties"]["cd"]["minimum"] == 10);
    CHECK(test["properties"]["cd"]["maximum"] == 15);
    CHECK(test["properties"]["heroi"]["enum"] == json::array({"Aria", "Bento"}));
    CHECK(test["required"] == json::array({"tipo", "motivo", "heroi", "atributo", "cd"}));
    CHECK(test["additionalProperties"] == false);

    CHECK(options[1]["properties"]["quantia"]["maximum"] == 20);
    CHECK(options[2]["required"] == json::array({"tipo", "motivo"}));

    const auto body = request_body(cfg, situation);
    CHECK(body["response_format"]["json_schema"]["schema"] == s);
    CHECK(body["top_k"] == cfg.top_k);
}

TEST_CASE("lines given to the heroes are dropped; the player speaks for them") {
    const auto cfg = load_config(GAMERPG_DATA_DIR);
    const json situation{{"id", "x"}, {"grupo", json::array({json{{"nome", "Aria"}}})}};
    const auto n = parse_reply(cfg, situation,
                               reply_with(json{{"narracao", "Texto"},
                                               {"falas", json::array({json{{"personagem", "Aria"}, {"texto", "Vamos!"}},
                                                                      json{{"personagem", "Marta"}, {"texto", "Fora!"}}})},
                                               {"acoes", json::array()}}),
                               kWarning);
    REQUIRE(n);
    CHECK(n->lines == std::vector<Line>{{"Marta", "Fora!"}});
}

TEST_CASE("bank phrases must keep their marks and nothing that would not fit every case") {
    const std::vector<std::string> marks{"{ator}", "{alvo}"};
    CHECK(valid_phrase("{ator} acerta {alvo} com um estalo seco", marks));
    CHECK(!valid_phrase("{ator} acerta o inimigo com um estalo seco", marks));  // missing {alvo}
    CHECK(!valid_phrase("{ator} acerta {alvo} {arma} com força", marks));       // unknown mark
    CHECK(!valid_phrase("{ator} acerta {alvo} e tira 5 de vida", marks));        // number
    CHECK(!valid_phrase("{ator} acerta {alvo} sem fechar {chave", marks));
    CHECK(!valid_phrase("{ator} {alvo}", marks));                               // too short
    CHECK(valid_phrase("O último inimigo tomba e o silêncio volta.", {}));
    // Names take no article: "o {alvo}" would also give it a gender.
    CHECK(!valid_phrase("O {ator} acerta {alvo} com um estalo seco", marks));
    CHECK(!valid_phrase("{ator} acerta o {alvo} com um estalo seco", marks));
    CHECK(!valid_phrase("{ator} rasga o escudo do {alvo} de uma vez", marks));
    CHECK(!valid_phrase("{ator} avança e à {alvo} resta recuar", marks));
    CHECK(valid_phrase("{ator} rasga o escudo de {alvo} de uma vez", marks));
    // Nor gendered pronouns.
    CHECK(!valid_phrase("{ator} acerta {alvo} e derruba-o no chão", marks));
    CHECK(!valid_phrase("{ator} acerta {alvo} e ele cambaleia", marks));
    CHECK(!valid_phrase("{ator} acerta {alvo} sem deixá-la respirar", marks));
}

TEST_CASE("a reply that repeats the previous narrations is cut, with a warning for the model") {
    const auto cfg = load_config(GAMERPG_DATA_DIR);
    const json situation{{"id", "rodada_3"},
                         {"narracoes_anteriores", {"O brutamontes avança com os punhos cerrados como pedras."}},
                         {"acoes_permitidas", json::array()}};
    const auto n = parse_reply(cfg, situation,
                               reply_with(json{{"narracao", "O brutamontes avança, os punhos cerrados como pedras. Aria salta para o lado."},
                                               {"falas", json::array()},
                                               {"acoes", json::array()}}),
                               kWarning);
    REQUIRE(n);
    CHECK(n->text == "Aria salta para o lado.");
    CHECK(n->warnings == std::vector<std::string>{"repetida"});
    // Nothing new to show: no narration.
    CHECK(!parse_reply(cfg, situation,
                       reply_with(json{{"narracao", "O brutamontes avança com os punhos cerrados como pedras."},
                                       {"falas", json::array()},
                                       {"acoes", json::array()}}),
                       kWarning));
}

TEST_CASE("heals on oneself and helpful conditions have their own event keys") {
    const auto c = arena_combat();
    Event heal;
    heal.kind = Event::Kind::Healed;
    heal.actor = "h1";
    heal.target = "h2";
    CHECK(event_key(c, heal) == "healed");
    heal.target = "h1";
    CHECK(event_key(c, heal) == "healed.self");
    Event cond;
    cond.kind = Event::Kind::ConditionApplied;
    cond.detail = "poisoned";
    CHECK(event_key(c, cond) == "condition_applied");
    cond.detail = "protected";
    CHECK(event_key(c, cond) == "condition_applied.beneficial");
    CHECK(!describe_event(c, cond).empty());
}

TEST_CASE("the phrase bank is read and written as JSON") {
    const json j{{"eventos_por_rodada", 2},
                 {"abertura", {"A luta começa em {cenario}."}},
                 {"eventos", {{"monster_died", {"{alvo} tomba de vez."}}}}};
    const auto bank = bank_from_json(j);
    CHECK(bank.events_per_round == 2);
    REQUIRE(bank.find("monster_died"));
    CHECK(!bank.find("victory"));
    CHECK(to_json(bank) == j);
    CHECK_THROWS_AS(bank_from_json(json{{"eventos_por_rodada", 0}, {"abertura", json::array()}, {"eventos", json::object()}}),
                    data::DataError);
    CHECK_THROWS_AS(bank_from_json(json{{"eventos_por_rodada", 1}, {"abertura", json::array()}, {"eventos", {{"victory", json::array()}}}}),
                    data::DataError);
}

TEST_CASE("the bank narrates the most striking events, in order, the same way for the same seed") {
    PhraseBank bank;
    bank.events_per_round = 2;
    bank.openings = {"Começa a luta em {cenario}."};
    bank.events = {{"attack_rolled.hit", {"{ator} acerta {alvo}.", "{alvo} sente o golpe de {ator}."}},
                   {"attack_rolled.miss", {"{ator} erra {alvo}.", "{alvo} escapa de {ator}."}},
                   {"attack_rolled.critical", {"{ator} esmaga {alvo}!", "{ator} arrasa {alvo}!"}},
                   {"monster_died", {"{alvo} tomba.", "{alvo} não levanta mais."}},
                   {"victory", {"Vitória do grupo.", "O campo é dos heróis."}}};

    auto c = arena_combat(3);
    const combat::AutoPlayer player;
    while (!c.finished()) c.decide(player.choose(c, *c.pending()));

    BankNarrator a(bank, 7), b(bank, 7);
    const auto open = a.opening(c);
    REQUIRE(open);
    CHECK(open->text == "Começa a luta em " + content().text.get("scenario.arena.name") + ".");
    CHECK(b.opening(c)->text == open->text);

    const auto all = a.round(c, 0, c.events().size());
    REQUIRE(all);
    CHECK(all->situation == "fim");
    CHECK(all->text.find('{') == std::string::npos);
    // At most two events, and the victory (most striking) is always one of them, last.
    const auto victory = all->text.rfind("Vitória do grupo.") != std::string::npos || all->text.rfind("O campo é dos heróis.") != std::string::npos;
    CHECK(victory);
    CHECK(b.round(c, 0, c.events().size())->text == all->text);

    // Events without phrases are left out; nothing to say gives nullopt.
    Event moved;
    moved.kind = Event::Kind::Moved;
    CHECK(salience(moved) == 0);
    CHECK(!a.round(c, 0, 0));
}

TEST_CASE("the bank ends every phrase with punctuation and prefers a condition's own phrases") {
    const combat::AutoPlayer player;
    std::optional<combat::Combat> played;
    std::size_t cond = 0;
    for (std::uint64_t seed = 1; cond == 0 && seed < 40; ++seed) {
        played.emplace(arena_combat(seed));
        while (!played->finished()) played->decide(player.choose(*played, *played->pending()));
        for (std::size_t i = 0; i < played->events().size(); ++i) {
            if (played->events()[i].kind == Event::Kind::ConditionApplied) cond = i + 1;
        }
    }
    REQUIRE(cond > 0);
    const auto& c = *played;
    const auto& e = c.events()[cond - 1];

    PhraseBank bank;
    bank.events = {{"condition_applied", {" {alvo} fica {condicao};"}}, {"condition_applied.beneficial", {" {alvo} fica {condicao};"}}};
    BankNarrator generic(bank, 1);
    const auto g = generic.round(c, cond - 1, cond);
    REQUIRE(g);
    CHECK(g->text.front() != ' ');
    CHECK(g->text.ends_with(content().text.get(content().rules.find_condition(e.detail)->name_key) + "."));

    bank.events.push_back({"condition_applied." + e.detail, {"Algo muda em {alvo}!"}});
    BankNarrator own(bank, 1);
    const auto o = own.round(c, cond - 1, cond);
    REQUIRE(o);
    CHECK(o->text.find("Algo muda em ") == 0);
    CHECK(o->text.back() == '!');
}

TEST_CASE("the bank tells a hit before a blessing when it has room for one event") {
    const combat::AutoPlayer player;
    for (std::uint64_t seed = 1; seed < 60; ++seed) {
        auto c = arena_combat(seed);
        while (!c.finished()) c.decide(player.choose(c, *c.pending()));
        std::size_t blessing = 0, hit = 0;
        for (std::size_t i = 0; i < c.events().size() && (blessing == 0 || hit == 0); ++i) {
            const auto& e = c.events()[i];
            if (e.kind == Event::Kind::RoundStarted) blessing = hit = 0;
            if (event_key(c, e) == "condition_applied.beneficial") blessing = i + 1;
            if (event_key(c, e) == "attack_rolled.miss") hit = i + 1;
        }
        if (blessing == 0 || hit == 0) continue;
        PhraseBank bank;
        bank.events_per_round = 1;
        bank.events = {{"condition_applied.beneficial", {"Bênção sobre {alvo}."}}, {"attack_rolled.miss", {"{ator} erra {alvo}."}}};
        BankNarrator n(bank, 1);
        const auto first = std::min(blessing, hit) - 1, end = std::max(blessing, hit);
        const auto r = n.round(c, first, end);
        REQUIRE(r);
        CHECK(r->text.find("erra") != std::string::npos);
        return;
    }
    FAIL("no round with both a blessing and a miss");
}

TEST_CASE("the bank does not repeat the same variant twice in a row") {
    PhraseBank bank;
    bank.openings = {"Um em {cenario}.", "Dois em {cenario}."};
    auto c = arena_combat();
    BankNarrator n(bank, 1);
    std::string last;
    for (int i = 0; i < 20; ++i) {
        const auto text = n.opening(c)->text;
        CHECK(text != last);
        last = text;
    }
}

TEST_CASE("the bank tells what the model leaves untold and keeps what the model said") {
    Narration bank;
    bank.situation = "rodada_2";
    bank.text = "Do banco.";
    Narration model;
    model.situation = "rodada_2";
    model.text = "Do modelo.";

    CHECK(cover(model, bank)->text == "Do modelo.");
    CHECK(cover(std::nullopt, bank)->text == "Do banco.");
    CHECK_FALSE(cover(std::nullopt, std::nullopt));

    // Only lines (or every sentence cut as a repeat): the bank's text, the model's lines.
    model.text.clear();
    model.lines = {{"Bandido #1", "Vocês não passam!"}};
    CHECK(needs_cover(model));
    const auto both = cover(model, bank);
    CHECK(both->text == "Do banco.");
    CHECK(both->lines == model.lines);
    CHECK(cover(model, std::nullopt)->lines == model.lines);
}

TEST_CASE("every phrase in the shipped bank keeps its marks and fits any character") {
    const std::filesystem::path dir = std::filesystem::path(GAMERPG_DATA_DIR) / "narrator";
    const auto bank = load_bank(dir / "banco.json");
    const auto spec = data::read_json_file(dir / "banco_instrucoes.json")["eventos"];
    auto check = [](const std::string& key, const std::vector<std::string>& phrases, const json& marks) {
        INFO(key);
        CHECK(phrases.size() >= 8);
        const auto wanted = marks.get<std::vector<std::string>>();
        for (std::size_t i = 0; i < phrases.size(); ++i) {
            INFO(phrases[i]);
            CHECK(valid_phrase(phrases[i], wanted));
            CHECK(std::find(phrases.begin(), phrases.begin() + static_cast<std::ptrdiff_t>(i), phrases[i]) ==
                  phrases.begin() + static_cast<std::ptrdiff_t>(i));
        }
    };
    check("abertura", bank.openings, spec["abertura"]["marcadores"]);
    for (const auto& [key, s] : spec.items()) {
        if (key == "abertura") continue;
        const auto* phrases = bank.find(key);
        REQUIRE_MESSAGE(phrases, key);
        check(key, *phrases, s["marcadores"]);
    }
}
