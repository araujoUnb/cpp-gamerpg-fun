#include "narrator/bank.hpp"

#include "data/json_helpers.hpp"
#include "data/json_io.hpp"
#include "narrator/situation.hpp"
#include "narrator/text.hpp"

#include <algorithm>

namespace rpg::narrator {

using combat::Event;
using nlohmann::json;
using namespace data::detail;

const std::vector<std::string>* PhraseBank::find(std::string_view key) const {
    for (const auto& [k, v] : events) {
        if (k == key) return &v;
    }
    return nullptr;
}

PhraseBank bank_from_json(const json& j) {
    const std::string where = "phrase bank";
    only_keys(j, {"eventos_por_rodada", "abertura", "eventos"}, where);
    PhraseBank b;
    b.events_per_round = get_int(j, "eventos_por_rodada", where);
    if (b.events_per_round < 1) fail(where, "'eventos_por_rodada' must be positive");
    b.openings = string_list(get_array(j, "abertura", where), where + ": abertura");
    const auto& events = field(j, "eventos", where);
    require_object(events, where + ": eventos");
    for (const auto& [key, list] : events.items()) {
        auto phrases = string_list(list, where + ": " + key);
        if (phrases.empty()) fail(where, "'" + key + "' has no phrases");
        b.events.emplace_back(key, std::move(phrases));
    }
    return b;
}

PhraseBank load_bank(const std::filesystem::path& file) {
    return bank_from_json(data::read_json_file(file));
}

json to_json(const PhraseBank& bank) {
    json events = json::object();
    for (const auto& [k, v] : bank.events) events[k] = v;
    return json{{"eventos_por_rodada", bank.events_per_round}, {"abertura", bank.openings}, {"eventos", events}};
}

namespace {

// "o {alvo}", "do {ator}": names take no article in Portuguese, and the
// article would give a gender to whoever fills the mark.
bool article_before(std::string_view phrase, std::size_t mark) {
    static constexpr std::string_view articles[] = {"o",  "a",  "os", "as", "do", "da",  "dos",  "das",  "no",  "na",  "nos",
                                                    "nas", "ao", "aos", "à", "às", "pelo", "pela", "um", "uma", "num", "numa"};
    auto end = mark;
    while (end > 0 && phrase[end - 1] == ' ') --end;
    if (end == mark) return false;
    auto begin = end;
    while (begin > 0 && phrase[begin - 1] != ' ') --begin;
    std::string word(phrase.substr(begin, end - begin));
    for (auto& ch : word) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    if (word.rfind("À", 0) == 0) word.replace(0, 2, "à");
    return std::find(std::begin(articles), std::end(articles), word) != std::end(articles);
}

}  // namespace

bool valid_phrase(std::string_view phrase, const std::vector<std::string>& marks) {
    if (phrase.size() < 15 || phrase.size() > 300) return false;
    // Gendered pronouns would get the gender of whoever fills a mark wrong.
    static constexpr std::string_view pronouns[] = {"-lo", "-la", "-o ", "-a ", " ele ", " ela ", " dele", " dela", "Ele ", "Ela "};
    for (const auto p : pronouns) {
        if (phrase.find(p) != std::string_view::npos) return false;
    }
    if (phrase.ends_with("-o") || phrase.ends_with("-a") || phrase.ends_with("-o.") || phrase.ends_with("-a.")) return false;
    for (const auto& m : marks) {
        if (phrase.find(m) == std::string_view::npos) return false;
    }
    for (std::size_t i = 0; i < phrase.size(); ++i) {
        const char ch = phrase[i];
        if (ch >= '0' && ch <= '9') return false;
        if (ch == '}') return false;  // a lone closing brace
        if (ch != '{') continue;
        if (article_before(phrase, i)) return false;
        const auto close = phrase.find('}', i);
        if (close == std::string_view::npos) return false;
        const auto mark = phrase.substr(i, close - i + 1);
        if (std::find(marks.begin(), marks.end(), mark) == marks.end()) return false;
        i = close;
    }
    return true;
}

int salience(const Event& e) {
    using K = Event::Kind;
    switch (e.kind) {
        case K::Victory:
        case K::Defeat: return 100;
        case K::HeroDied: return 90;
        case K::HeroDowned: return 80;
        case K::MonsterDied: return 70;
        case K::HeroRevived: return 65;
        case K::AttackRolled: return e.critical ? 60 : e.roll == 1 ? 45 : e.flag ? 20 : 10;
        case K::HeroExhausted: return 55;
        case K::TrapTriggered: return 50;
        case K::DeathSave: return 40;
        case K::HeroStabilized: return 40;
        case K::ConditionApplied: return 30;  // a helpful one weighs less: see BankNarrator::round
        case K::Healed: return 25;
        case K::TurnSkipped: return 15;
        case K::LongRested: return 15;
        case K::DoorOpened: return 15;
        case K::Moved: return e.flag ? 15 : 0;
        default: return 0;
    }
}

bool needs_cover(const std::optional<Narration>& model) { return !model || model->text.empty(); }

std::optional<Narration> cover(std::optional<Narration> model, std::optional<Narration> bank) {
    if (!needs_cover(model) || !bank) return model;
    if (!model) return bank;
    model->text = std::move(bank->text);
    return model;
}

BankNarrator::BankNarrator(const PhraseBank& bank, std::uint64_t seed) : bank_(&bank), rng_(seed, 0x6e61727261) {}

const std::string& BankNarrator::pick(const std::string& key, const std::vector<std::string>& variants) {
    auto it = std::find_if(last_.begin(), last_.end(), [&](const auto& p) { return p.first == key; });
    if (it == last_.end()) {
        last_.emplace_back(key, variants.size());
        it = last_.end() - 1;
    }
    auto i = static_cast<std::size_t>(rng_.below(static_cast<std::uint32_t>(variants.size())));
    if (variants.size() > 1 && i == it->second) i = (i + 1 + rng_.below(static_cast<std::uint32_t>(variants.size() - 1))) % variants.size();
    it->second = i;
    return variants[i];
}

std::optional<Narration> BankNarrator::opening(const combat::Combat& c) {
    if (bank_->openings.empty()) return std::nullopt;
    Narration n;
    n.situation = "abertura";
    n.text = pick("abertura", bank_->openings);
    fill(n.text, "cenario", c.content().text.get(c.scenario().name_key));
    return n;
}

std::optional<Narration> BankNarrator::round(const combat::Combat& c, std::size_t first_event, std::size_t end_event) {
    const auto& all = c.events();
    end_event = std::min(end_event, all.size());

    struct Candidate {
        std::size_t index;
        int score;
        std::string key;
        const std::vector<std::string>* phrases;
    };
    std::vector<Candidate> chosen;
    int round = c.round();
    bool end = false;
    for (std::size_t i = first_event; i < end_event; ++i) {
        const auto& e = all[i];
        if (e.kind == Event::Kind::RoundStarted) round = e.value;
        end = end || e.kind == Event::Kind::Victory || e.kind == Event::Kind::Defeat;
        // Blessings come every round with healers; blows matter more.
        const bool blessing = e.kind == Event::Kind::ConditionApplied && event_key(c, e) == "condition_applied.beneficial";
        const int score = blessing ? 8 : salience(e);
        if (score == 0) continue;
        // A condition's own phrases, when the bank has them, say what it does
        // ("o veneno entra...") instead of naming it.
        std::string key = e.kind == Event::Kind::ConditionApplied ? "condition_applied." + e.detail : "";
        const auto* phrases = key.empty() ? nullptr : bank_->find(key);
        if (!phrases) {
            key = event_key(c, e);
            phrases = bank_->find(key);
        }
        if (!phrases) continue;
        chosen.push_back({i, score, std::move(key), phrases});
    }
    if (chosen.empty()) return std::nullopt;

    std::stable_sort(chosen.begin(), chosen.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (chosen.size() > static_cast<std::size_t>(bank_->events_per_round)) {
        chosen.resize(static_cast<std::size_t>(bank_->events_per_round));
    }
    std::sort(chosen.begin(), chosen.end(), [](const auto& a, const auto& b) { return a.index < b.index; });

    Narration n;
    n.situation = end ? "fim" : "rodada_" + std::to_string(round);
    for (const auto& ch : chosen) {
        auto phrase = fill_event(c, all[ch.index], pick(ch.key, *ch.phrases));
        const auto b = phrase.find_first_not_of(' ');
        auto e = phrase.find_last_not_of(" ,;:");
        if (b == std::string::npos || e == std::string::npos) continue;
        phrase = phrase.substr(b, e - b + 1);
        if (!n.text.empty()) n.text += ' ';
        n.text += phrase;
        const char last = n.text.back();
        if (last != '.' && last != '!' && last != '?') n.text += '.';
    }
    return n;
}

}  // namespace rpg::narrator
