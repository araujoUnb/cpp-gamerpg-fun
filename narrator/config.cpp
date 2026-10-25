#include "narrator/config.hpp"

#include "data/json_helpers.hpp"
#include "data/json_io.hpp"

namespace rpg::narrator {

using namespace data::detail;

Config load_config(const std::filesystem::path& data_dir) {
    const auto file = data_dir / "narrator" / "mestre.json";
    const std::string where = file.string();
    const auto j = data::read_json_file(file);
    only_keys(j, {"sistema", "pedido", "tamanho", "geracao", "tempo_limite_ms", "narracoes_anteriores", "limites"}, where);

    Config c;
    c.system = get_string(j, "sistema", where);
    c.request = get_string(j, "pedido", where);
    if (c.request.find("{situacao}") == std::string::npos) fail(where, "'pedido' must contain {situacao}");

    const auto& size = field(j, "tamanho", where);
    const auto size_where = where + ": tamanho";
    only_keys(size, {"combate", "cena"}, size_where);
    c.size_combat = get_string(size, "combate", size_where);
    c.size_scene = get_string(size, "cena", size_where);

    const auto& gen = field(j, "geracao", where);
    const auto gen_where = where + ": geracao";
    only_keys(gen, {"temperatura", "top_p", "top_k", "min_p", "max_tokens"}, gen_where);
    auto number = [&](const char* key, double lo, double hi) {
        const auto& v = field(gen, key, gen_where);
        if (!v.is_number() || v.get<double>() < lo || v.get<double>() > hi) {
            fail(gen_where, quote_key(key) + " must be a number from " + std::to_string(lo) + " to " + std::to_string(hi));
        }
        return v.get<double>();
    };
    c.temperature = number("temperatura", 0.0, 2.0);
    c.top_p = number("top_p", 0.0, 1.0);
    c.min_p = number("min_p", 0.0, 1.0);
    c.top_k = get_int(gen, "top_k", gen_where);
    c.max_tokens = get_int(gen, "max_tokens", gen_where);

    c.timeout_ms = get_int(j, "tempo_limite_ms", where);
    c.history = get_int(j, "narracoes_anteriores", where);

    const auto& lim = field(j, "limites", where);
    const auto lim_where = where + ": limites";
    only_keys(lim, {"narracao_caracteres", "falas", "fala_caracteres"}, lim_where);
    c.max_text_chars = get_int(lim, "narracao_caracteres", lim_where);
    c.max_lines = get_int(lim, "falas", lim_where);
    c.max_line_chars = get_int(lim, "fala_caracteres", lim_where);

    if (c.max_tokens < 1 || c.timeout_ms < 1 || c.history < 0 || c.max_text_chars < 1 || c.max_lines < 0 ||
        c.max_line_chars < 1) {
        fail(where, "limits must be positive");
    }

    c.schema = data::read_json_file(data_dir / "narrator" / "resposta.schema.json");
    return c;
}

}  // namespace rpg::narrator
