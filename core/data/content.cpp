#include "data/content.hpp"

#include "data/card_io.hpp"
#include "data/json_helpers.hpp"
#include "data/json_io.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace rpg::data {

using namespace detail;
namespace fs = std::filesystem;

namespace {

std::vector<fs::path> json_files(const fs::path& dir) {
    if (!fs::is_directory(dir)) throw DataError(dir.string() + ": not a directory");
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

combat::Hex hex_from_json(const json& j, const std::string& where) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number_integer() || !j[1].is_number_integer()) {
        fail(where, "a hex is written as [q, r]");
    }
    return {j[0].get<int>(), j[1].get<int>()};
}

}  // namespace

monsters::MonsterDef monster_from_json(const json& j, const character::Ruleset& rules, const std::string& where) {
    only_keys(j, {"id", "name", "armor_class", "hit_points", "attack_bonus", "damage_bonus", "attributes",
                  "immunities", "deck", "visual"}, where);
    monsters::MonsterDef m;
    m.id = get_string(j, "id", where);
    if (m.id.empty()) fail(where, "'id' must not be empty");
    const std::string ctx = where + " [" + m.id + "]";
    m.name_key = get_string(j, "name", ctx);
    m.armor_class = get_int(j, "armor_class", ctx);
    m.hit_points = get_int(j, "hit_points", ctx);
    m.attack_bonus = get_int(j, "attack_bonus", ctx);
    m.damage_bonus = get_int(j, "damage_bonus", ctx);
    if (m.hit_points < 1) fail(ctx, "'hit_points' must be at least 1");
    if (m.armor_class < 1) fail(ctx, "'armor_class' must be at least 1");
    m.attributes = attributes_from_json(field(j, "attributes", ctx), ctx + ".attributes");

    if (j.contains("immunities")) {
        m.immunities = string_list(j["immunities"], ctx + ".immunities");
        for (const auto& c : m.immunities) {
            if (!rules.find_condition(c)) fail(ctx, "unknown condition '" + c + "' in immunities");
        }
    }

    std::set<std::string> ids;
    const auto& deck = get_array(j, "deck", ctx);
    if (deck.empty()) fail(ctx, "'deck' needs at least one card");
    for (std::size_t i = 0; i < deck.size(); ++i) {
        const std::string cctx = ctx + ".deck[" + std::to_string(i) + "]";
        const auto& c = deck[i];
        only_keys(c, {"id", "name", "initiative", "actions", "reshuffle"}, cctx);
        monsters::MonsterCard card;
        card.id = get_string(c, "id", cctx);
        card.name_key = get_string(c, "name", cctx);
        card.initiative = get_int(c, "initiative", cctx);
        card.reshuffle = get_bool_or(c, "reshuffle", false, cctx);
        if (!rules.cards.initiative.contains(card.initiative)) fail(cctx, "'initiative' out of range");
        if (!ids.insert(card.id).second) fail(cctx, "duplicate card id '" + card.id + "'");
        const auto& actions = get_array(c, "actions", cctx);
        if (actions.empty()) fail(cctx, "a card needs at least one action");
        for (std::size_t k = 0; k < actions.size(); ++k) {
            card.actions.push_back(action_from_json(actions[k], rules, cctx + ".actions[" + std::to_string(k) + "]"));
        }
        m.deck.push_back(std::move(card));
    }

    if (j.contains("visual")) {
        const auto& v = j["visual"];
        only_keys(v, {"color", "portrait"}, ctx + ".visual");
        m.visual.color = get_string_or(v, "color", ctx + ".visual");
        m.visual.portrait = get_string_or(v, "portrait", ctx + ".visual");
    }
    return m;
}

Content load_content(const fs::path& data_dir, const std::string& language) {
    Content c;
    c.rules = ruleset_from_json(read_json_file(data_dir / "rules" / "ruleset.json"), "ruleset.json");
    c.classes = load_classes(data_dir / "classes", c.rules);
    c.cards = load_cards(data_dir / "cards", c.rules, c.classes);
    for (const auto& file : json_files(data_dir / "monsters")) {
        auto m = monster_from_json(read_json_file(file), c.rules, file.filename().string());
        try {
            c.monsters.add(std::move(m));
        } catch (const std::invalid_argument& e) {
            fail(file.filename().string(), e.what());
        }
    }
    c.text = load_text_table(data_dir / "lang" / (language + ".json"));
    return c;
}

scenario::ScenarioDef scenario_from_json(const json& j, const Content& content, const std::string& where) {
    only_keys(j, {"id", "name", "map", "hero_starts", "monsters"}, where);
    scenario::ScenarioDef s;
    s.id = get_string(j, "id", where);
    const std::string ctx = where + " [" + s.id + "]";
    s.name_key = get_string(j, "name", ctx);

    const std::string mctx = ctx + ".map";
    const auto& map = field(j, "map", ctx);
    only_keys(map, {"radius", "obstacles"}, mctx);
    const int radius = get_int(map, "radius", mctx);
    if (radius < 1 || radius > 30) fail(mctx, "'radius' must be between 1 and 30");
    std::set<combat::Hex> obstacles;
    if (map.contains("obstacles")) {
        for (const auto& o : get_array(map, "obstacles", mctx)) {
            const auto h = hex_from_json(o, mctx + ".obstacles");
            if (combat::distance(h, {}) > radius) fail(mctx, "obstacle outside the map");
            obstacles.insert(h);
        }
    }
    s.map = combat::HexMap::hexagon(radius, std::move(obstacles));

    std::set<combat::Hex> used;
    auto place = [&](combat::Hex h, const std::string& c) {
        if (!s.map.walkable(h)) fail(c, "hex [" + std::to_string(h.q) + ", " + std::to_string(h.r) + "] is not walkable");
        if (!used.insert(h).second) {
            fail(c, "hex [" + std::to_string(h.q) + ", " + std::to_string(h.r) + "] is used twice");
        }
    };
    for (const auto& h : get_array(j, "hero_starts", ctx)) {
        s.hero_starts.push_back(hex_from_json(h, ctx + ".hero_starts"));
        place(s.hero_starts.back(), ctx + ".hero_starts");
    }
    if (s.hero_starts.empty()) fail(ctx, "'hero_starts' must not be empty");

    for (const auto& m : get_array(j, "monsters", ctx)) {
        const std::string sctx = ctx + ".monsters";
        only_keys(m, {"type", "hex"}, sctx);
        scenario::MonsterSpawn spawn{get_string(m, "type", sctx), hex_from_json(field(m, "hex", sctx), sctx)};
        if (!content.monsters.find(spawn.type)) fail(sctx, "unknown monster type '" + spawn.type + "'");
        place(spawn.hex, sctx);
        s.monsters.push_back(std::move(spawn));
    }
    if (s.monsters.empty()) fail(ctx, "'monsters' must not be empty");
    return s;
}

scenario::ScenarioDef load_scenario(const fs::path& file, const Content& content) {
    return scenario_from_json(read_json_file(file), content, file.filename().string());
}

}  // namespace rpg::data
