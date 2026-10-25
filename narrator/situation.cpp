#include "narrator/situation.hpp"

#include "narrator/text.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace rpg::narrator {

using combat::Combat;
using combat::Event;
using nlohmann::json;

namespace {

std::string creature_name(const Combat& c, const std::string& id) {
    const auto& text = c.content().text;
    for (const auto& h : c.heroes()) {
        if (h.sheet.id == id) return h.sheet.name;
    }
    for (const auto& m : c.monsters()) {
        if (m.id == id) return text.get(c.content().monsters.get(m.type).name_key) + " " + m.id.substr(m.id.find('#'));
    }
    if (const auto* def = c.content().monsters.find(id)) return text.get(def->name_key);
    return id;
}

std::string condition_name(const Combat& c, const std::string& id) {
    const auto* def = c.content().rules.find_condition(id);
    return def ? c.content().text.get(def->name_key) : id;
}

std::string card_name(const Combat& c, const Event& e) {
    const auto& text = c.content().text;
    if (e.kind == Event::Kind::MonsterCardDrawn) {
        for (const auto& mc : c.content().monsters.get(e.actor).deck) {
            if (mc.id == e.detail) return text.get(mc.name_key);
        }
        return e.detail;
    }
    const auto* card = c.content().cards.find(e.detail);
    return card ? text.get(card->name_key) : e.detail;
}

json conditions_json(const Combat& c, const std::vector<combat::ActiveCondition>& conds) {
    json out = json::array();
    for (const auto& cond : conds) out.push_back(condition_name(c, cond.id));
    return out;
}

json party_json(const Combat& c) {
    const auto& content = c.content();
    json party = json::array();
    for (const auto& h : c.heroes()) {
        const auto& cls = content.classes.get(h.sheet.class_id);
        json j{{"nome", h.sheet.name},
               {"classe", content.text.get(cls.name_key)},
               {"pv", std::to_string(h.sheet.current_hp) + "/" + std::to_string(character::max_hp(h.sheet, cls))}};
        const char* status = nullptr;
        switch (h.status) {
            case combat::HeroStatus::Active: break;
            case combat::HeroStatus::Dying: status = "narrator.status.dying"; break;
            case combat::HeroStatus::Stable: status = "narrator.status.stable"; break;
            case combat::HeroStatus::Dead: status = "narrator.status.dead"; break;
            case combat::HeroStatus::Exhausted: status = "narrator.status.exhausted"; break;
        }
        if (status) j["estado"] = content.text.get(status);
        if (!h.conditions.empty()) j["condicoes"] = conditions_json(c, h.conditions);
        party.push_back(std::move(j));
    }
    return party;
}

json enemies_json(const Combat& c) {
    json enemies = json::array();
    for (const auto& m : c.monsters()) {
        if (!m.alive) continue;
        json j{{"nome", creature_name(c, m.id)}, {"pv", std::to_string(m.hp) + "/" + std::to_string(c.max_hp_of(m))}};
        if (!m.conditions.empty()) j["condicoes"] = conditions_json(c, m.conditions);
        enemies.push_back(std::move(j));
    }
    return enemies;
}

json base_situation(const Combat& c, std::string id, const char* moment, std::string context) {
    return json{{"id", std::move(id)},
                {"momento", moment},
                {"cenario", c.content().text.get(c.scenario().name_key)},
                {"contexto", std::move(context)},
                {"grupo", party_json(c)},
                {"inimigos", enemies_json(c)},
                {"eventos", json::array()},
                {"jogador", ""},
                {"acoes_permitidas", json::array()}};
}

}  // namespace

std::string event_key(const Combat& c, const Event& e) {
    std::string key(combat::event_kind_id(e.kind));
    switch (e.kind) {
        case Event::Kind::AttackRolled:
            return key + (e.critical ? ".critical" : e.flag ? ".hit" : e.roll == 1 ? ".fumble" : ".miss");
        case Event::Kind::DeathSave: return key + (e.flag ? ".success" : ".failure");
        case Event::Kind::Moved: return e.flag ? key + ".forced" : key;
        case Event::Kind::HalfUsed: return e.flag ? key + ".basic" : key;
        case Event::Kind::Healed: return e.actor == e.target ? key + ".self" : key;
        case Event::Kind::ConditionApplied: {
            const auto* def = c.content().rules.find_condition(e.detail);
            return def && def->effects.beneficial() ? key + ".beneficial" : key;
        }
        default: return key;
    }
}

std::string fill_event(const Combat& c, const Event& e, std::string text) {
    fill(text, "ator", creature_name(c, e.actor));
    fill(text, "alvo", creature_name(c, e.target));
    if (e.kind == Event::Kind::HalfUsed || e.kind == Event::Kind::MonsterCardDrawn) fill(text, "carta", card_name(c, e));
    if (e.kind == Event::Kind::ConditionApplied || e.kind == Event::Kind::ConditionResisted ||
        e.kind == Event::Kind::ConditionEnded) {
        fill(text, "condicao", condition_name(c, e.detail));
    }
    fill(text, "valor", std::to_string(e.value));
    fill(text, "valor2", std::to_string(e.value2));
    fill(text, "total", std::to_string(e.total));
    fill(text, "ca", std::to_string(e.target_number));
    return text;
}

std::string describe_event(const Combat& c, const Event& e) {
    const auto key = "narrator.event." + event_key(c, e);
    const auto& text = c.content().text;
    return text.contains(key) ? fill_event(c, e, text.get(key)) : std::string{};
}

json opening_situation(const Combat& c) {
    std::string context = c.content().text.get("narrator.context.opening");
    fill(context, "cenario", c.content().text.get(c.scenario().name_key));
    return base_situation(c, "abertura", "cena", std::move(context));
}

json round_situation(const Combat& c, std::size_t first_event, std::size_t end_event) {
    const auto& all = c.events();
    end_event = std::min(end_event, all.size());
    int round = c.round();
    bool end = false;
    for (std::size_t i = first_event; i < end_event; ++i) {
        if (all[i].kind == Event::Kind::RoundStarted) round = all[i].value;
        end = end || all[i].kind == Event::Kind::Victory || all[i].kind == Event::Kind::Defeat;
    }
    std::string context = c.content().text.get(end ? "narrator.context.end" : "narrator.context.round");
    fill(context, "rodada", std::to_string(round));
    auto s = base_situation(c, end ? "fim" : "rodada_" + std::to_string(round), "combate", std::move(context));
    auto& events = s["eventos"];
    for (std::size_t i = first_event; i < end_event; ++i) {
        auto line = describe_event(c, all[i]);
        if (!line.empty()) events.push_back(std::move(line));
    }
    return s;
}

}  // namespace rpg::narrator
