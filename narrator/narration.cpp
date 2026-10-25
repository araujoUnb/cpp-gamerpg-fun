#include "narrator/narration.hpp"

#include "data/json_helpers.hpp"
#include "narrator/text.hpp"

#include <algorithm>

namespace rpg::narrator {

using nlohmann::json;
using namespace data::detail;

namespace {

json string_enum(const json& values) {
    json out = json::array();
    if (values.is_array()) {
        for (const auto& v : values) {
            if (v.is_string()) out.push_back(v);
        }
    }
    return out;
}

// Schema of one allowed action, or null when its entry is malformed.
json action_schema(const json& allowed, const json& situation) {
    const std::string type = allowed.value("tipo", "");
    json props{{"tipo", json{{"const", type}}}, {"motivo", json{{"type", "string"}}}};
    json required = json::array({"tipo", "motivo"});
    auto add = [&](const char* key, json schema) {
        props[key] = std::move(schema);
        required.push_back(key);
    };
    if (type == "teste") {
        const auto attrs = string_enum(allowed.value("atributos", json::array()));
        const auto dc = allowed.value("cd", json::array());
        if (attrs.empty() || !dc.is_array() || dc.size() != 2 || !dc[0].is_number_integer() || !dc[1].is_number_integer()) {
            return nullptr;
        }
        json heroes = json::array();
        for (const auto& h : situation.value("grupo", json::array())) {
            if (h.is_object() && h.contains("nome")) heroes.push_back(h["nome"]);
        }
        if (!heroes.empty()) add("heroi", json{{"enum", heroes}});
        add("atributo", json{{"enum", attrs}});
        add("cd", json{{"type", "integer"}, {"minimum", dc[0]}, {"maximum", dc[1]}});
    } else if (type == "dar_item") {
        const auto items = string_enum(allowed.value("itens", json::array()));
        if (items.empty()) return nullptr;
        add("item", json{{"enum", items}});
    } else if (type == "dar_ouro") {
        const int max = allowed.value("maximo", 0);
        if (max < 1) return nullptr;
        add("quantia", json{{"type", "integer"}, {"minimum", 1}, {"maximum", max}});
    } else if (type == "marcar_flag") {
        const auto flags = string_enum(allowed.value("flags", json::array()));
        if (flags.empty()) return nullptr;
        add("flag", json{{"enum", flags}});
    } else if (type == "proximo_encontro") {
        const auto encounters = string_enum(allowed.value("encontros", json::array()));
        if (encounters.empty()) return nullptr;
        add("encontro", json{{"enum", encounters}});
    } else if (type != "abrir_loja") {
        return nullptr;
    }
    return json{{"type", "object"}, {"additionalProperties", false}, {"properties", props}, {"required", required}};
}

}  // namespace

Warnings load_warnings(const data::TextTable& text) {
    return {text.get("narrator.warning.not_allowed"), text.get("narrator.warning.extra_test"),
            text.get("narrator.warning.repeated")};
}

json reply_schema(const Config& cfg, const json& situation) {
    json schema = cfg.schema;
    if (!schema.is_object() || !schema.contains("properties")) return schema;
    auto& props = schema["properties"];
    if (props.contains("falas")) props["falas"]["maxItems"] = cfg.max_lines;

    json options = json::array();
    for (const auto& a : situation.value("acoes_permitidas", json::array())) {
        if (!a.is_object()) continue;
        auto s = action_schema(a, situation);
        if (!s.is_null()) options.push_back(std::move(s));
    }
    if (options.empty()) {
        props["acoes"] = json{{"type", "array"}, {"maxItems", 0}};
    } else {
        props["acoes"] = json{{"type", "array"}, {"maxItems", 3}, {"items", json{{"anyOf", options}}}};
    }
    return schema;
}

json request_body(const Config& cfg, const json& situation) {
    std::string user = cfg.request;
    const bool combat = situation.value("momento", "") == "combate";
    fill(user, "tamanho", combat ? cfg.size_combat : cfg.size_scene);
    fill(user, "situacao", situation.dump(1, ' ', false, json::error_handler_t::replace));
    return json{{"messages", json::array({json{{"role", "system"}, {"content", cfg.system}},
                                          json{{"role", "user"}, {"content", user}}})},
                {"response_format", json{{"type", "json_schema"},
                                         {"json_schema", json{{"name", "resposta"}, {"schema", reply_schema(cfg, situation)}}}}},
                // Qwen3 would otherwise think out loud before answering, which costs seconds on the CPU.
                {"chat_template_kwargs", json{{"enable_thinking", false}}},
                {"temperature", cfg.temperature},
                {"top_p", cfg.top_p},
                {"top_k", cfg.top_k},
                {"min_p", cfg.min_p},
                {"max_tokens", cfg.max_tokens},
                {"cache_prompt", true}};
}

std::optional<Narration> parse_reply(const Config& cfg, const json& situation, const std::string& reply,
                                     const Warnings& warnings) {
    const auto outer = json::parse(reply, nullptr, false);
    if (outer.is_discarded() || !outer.is_object()) return std::nullopt;
    const auto choices = outer.find("choices");
    if (choices == outer.end() || !choices->is_array() || choices->empty()) return std::nullopt;
    const auto& message = (*choices)[0].value("message", json::object());
    const auto content = message.find("content");
    if (content == message.end() || !content->is_string()) return std::nullopt;

    const auto answer = json::parse(content->get<std::string>(), nullptr, false);
    if (answer.is_discarded() || !answer.is_object()) return std::nullopt;
    const auto text = answer.find("narracao");
    if (text == answer.end() || !text->is_string() || text->get<std::string>().empty()) return std::nullopt;

    Narration n;
    n.situation = situation.value("id", "");
    n.text = text->get<std::string>();
    truncate_utf8(n.text, static_cast<std::size_t>(cfg.max_text_chars));
    if (const auto before = situation.find("narracoes_anteriores"); before != situation.end() && before->is_array()) {
        auto kept = drop_repeated(n.text, before->get<std::vector<std::string>>());
        if (kept.size() != n.text.size()) n.warnings.push_back(warnings.repeated);
        n.text = std::move(kept);
    }

    std::vector<std::string> heroes;
    for (const auto& h : situation.value("grupo", json::array())) {
        if (h.is_object() && h.contains("nome") && h["nome"].is_string()) heroes.push_back(h["nome"].get<std::string>());
    }
    if (const auto lines = answer.find("falas"); lines != answer.end() && lines->is_array()) {
        for (const auto& l : *lines) {
            if (n.lines.size() >= static_cast<std::size_t>(cfg.max_lines)) break;
            if (!l.is_object() || !l.contains("personagem") || !l.contains("texto")) continue;
            if (!l["personagem"].is_string() || !l["texto"].is_string()) continue;
            Line line{l["personagem"].get<std::string>(), l["texto"].get<std::string>()};
            if (line.speaker.empty() || line.text.empty()) continue;
            if (std::find(heroes.begin(), heroes.end(), line.speaker) != heroes.end()) continue;
            truncate_utf8(line.speaker, 60);
            truncate_utf8(line.text, static_cast<std::size_t>(cfg.max_line_chars));
            n.lines.push_back(std::move(line));
        }
    }

    if (const auto actions = answer.find("acoes"); actions != answer.end() && actions->is_array()) {
        const auto& allowed = situation.contains("acoes_permitidas") ? situation["acoes_permitidas"] : json::array();
        bool tested = false;
        for (const auto& a : *actions) {
            const std::string type = a.is_object() ? a.value("tipo", "") : "";
            bool ok = false;
            for (const auto& p : allowed) ok = ok || (p.is_object() && p.value("tipo", "") == type);
            if (ok && type == "teste" && tested) {
                n.warnings.push_back(warnings.extra_test);
            } else if (ok) {
                tested = tested || type == "teste";
                n.actions.push_back(a);
            } else {
                std::string w = warnings.not_allowed;
                fill(w, "tipo", type);
                n.warnings.push_back(std::move(w));
            }
        }
    }
    if (n.text.empty() && n.lines.empty() && n.actions.empty()) return std::nullopt;
    return n;
}

json to_json(const std::vector<Narration>& log) {
    json out = json::array();
    for (const auto& n : log) {
        json lines = json::array();
        for (const auto& l : n.lines) lines.push_back(json{{"personagem", l.speaker}, {"texto", l.text}});
        out.push_back(json{{"situacao", n.situation},
                           {"narracao", n.text},
                           {"falas", lines},
                           {"acoes", n.actions},
                           {"avisos", n.warnings},
                           {"depois_do_evento", n.after_event}});
    }
    return out;
}

std::vector<Narration> log_from_json(const json& j) {
    if (!j.is_array()) fail("narration log", "expected an array");
    std::vector<Narration> log;
    for (std::size_t i = 0; i < j.size(); ++i) {
        const std::string where = "narration log[" + std::to_string(i) + "]";
        const auto& e = j[i];
        only_keys(e, {"situacao", "narracao", "falas", "acoes", "avisos", "depois_do_evento"}, where);
        Narration n;
        n.situation = get_string(e, "situacao", where);
        n.text = get_string(e, "narracao", where);
        for (const auto& l : get_array(e, "falas", where)) {
            n.lines.push_back({get_string(l, "personagem", where), get_string(l, "texto", where)});
        }
        for (const auto& a : get_array(e, "acoes", where)) n.actions.push_back(a);
        n.warnings = string_list(get_array(e, "avisos", where), where);
        const int after = get_int(e, "depois_do_evento", where);
        if (after < 0) fail(where, "'depois_do_evento' must not be negative");
        n.after_event = static_cast<std::size_t>(after);
        log.push_back(std::move(n));
    }
    return log;
}

}  // namespace rpg::narrator
