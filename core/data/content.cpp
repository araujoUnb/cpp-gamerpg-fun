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

monsters::GameMasterDef game_master_from_json(const json& j, const std::string& where) {
    only_keys(j, {"id", "name", "tactical", "weights"}, where);
    monsters::GameMasterDef gm;
    gm.id = get_string(j, "id", where);
    if (gm.id.empty()) fail(where, "'id' must not be empty");
    const std::string ctx = where + " [" + gm.id + "]";
    gm.name_key = get_string(j, "name", ctx);
    gm.tactical = get_bool(j, "tactical", ctx);
    if (gm.tactical) {
        const auto& w = field(j, "weights", ctx);
        require_object(w, ctx + ".weights");
        for (const auto& [key, _] : w.items()) {
            bool known = false;
            for (const auto& [name, member] : monsters::kGmWeightFields) known = known || name == key;
            if (!known) fail(ctx + ".weights", "unknown field " + quote_key(key));
        }
        for (const auto& [name, member] : monsters::kGmWeightFields) {
            const std::string key(name);
            const int v = get_int(w, key.c_str(), ctx + ".weights");
            if (v < -kMaxGmWeight || v > kMaxGmWeight) fail(ctx + ".weights", quote_key(key) + " out of range");
            gm.weights.*member = v;
        }
    } else if (j.contains("weights")) {
        fail(ctx, "'weights' only apply to a tactical game master");
    }
    return gm;
}

json to_json(const monsters::GameMasterDef& gm) {
    json j{{"id", gm.id}, {"name", gm.name_key}, {"tactical", gm.tactical}};
    if (gm.tactical) {
        json w = json::object();
        for (const auto& [name, member] : monsters::kGmWeightFields) w[std::string(name)] = gm.weights.*member;
        j["weights"] = w;
    }
    return j;
}

monsters::DifficultyDef difficulty_from_json(const json& j, const Content& content, const std::string& where) {
    only_keys(j, {"id", "name", "game_master", "blunder", "monster_hp_percent", "calibration"}, where);
    monsters::DifficultyDef d;
    d.id = get_string(j, "id", where);
    if (d.id.empty()) fail(where, "'id' must not be empty");
    const std::string ctx = where + " [" + d.id + "]";
    d.name_key = get_string(j, "name", ctx);
    d.game_master = get_string(j, "game_master", ctx);
    if (d.game_master != monsters::GameMasterDef::kClassic && !content.game_masters.find(d.game_master)) {
        fail(ctx, "unknown game master '" + d.game_master + "'");
    }
    d.blunder = get_int_or(j, "blunder", 0, ctx);
    if (d.blunder < 0 || d.blunder > 1000) fail(ctx, "'blunder' must be between 0 and 1000 (per mille)");
    d.monster_hp_percent = get_int_or(j, "monster_hp_percent", 100, ctx);
    if (d.monster_hp_percent < 10 || d.monster_hp_percent > 500) fail(ctx, "'monster_hp_percent' must be between 10 and 500");
    if (j.contains("calibration")) {
        const auto& c = j["calibration"];
        const std::string cctx = ctx + ".calibration";
        only_keys(c, {"target_win_percent", "floor_win_percent"}, cctx);
        d.target_win_percent = get_int(c, "target_win_percent", cctx);
        d.floor_win_percent = get_int(c, "floor_win_percent", cctx);
        if (d.target_win_percent < 1 || d.target_win_percent > 100 || d.floor_win_percent < 0 ||
            d.floor_win_percent > d.target_win_percent) {
            fail(cctx, "need 0 <= floor_win_percent <= target_win_percent <= 100");
        }
    }
    return d;
}

json to_json(const monsters::DifficultyDef& d) {
    json j{{"id", d.id},
           {"name", d.name_key},
           {"game_master", d.game_master},
           {"blunder", d.blunder},
           {"monster_hp_percent", d.monster_hp_percent}};
    if (d.target_win_percent > 0) {
        j["calibration"] = {{"target_win_percent", d.target_win_percent}, {"floor_win_percent", d.floor_win_percent}};
    }
    return j;
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
    for (const auto& file : json_files(data_dir / "game_masters")) {
        auto gm = game_master_from_json(read_json_file(file), file.filename().string());
        const auto id = gm.id;
        if (!c.game_masters.add(std::move(gm))) fail(file.filename().string(), "duplicate game master id '" + id + "'");
    }
    for (const auto& file : json_files(data_dir / "difficulties")) {
        auto d = difficulty_from_json(read_json_file(file), c, file.filename().string());
        const auto id = d.id;
        if (!c.difficulties.add(std::move(d))) fail(file.filename().string(), "duplicate difficulty id '" + id + "'");
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
    only_keys(map, {"radius", "obstacles", "difficult", "doors", "traps"}, mctx);
    const int radius = get_int(map, "radius", mctx);
    if (radius < 1 || radius > 30) fail(mctx, "'radius' must be between 1 and 30");
    s.map = combat::HexMap::hexagon(radius);

    // Each special hex has exactly one kind of terrain.
    std::set<combat::Hex> special;
    auto mark = [&](combat::Hex h, combat::Terrain t, const std::string& c) {
        if (!s.map.contains(h)) fail(c, "hex outside the map");
        if (!special.insert(h).second) fail(c, "hex [" + std::to_string(h.q) + ", " + std::to_string(h.r) + "] has two terrains");
        s.map.set_terrain(h, t);
    };
    const std::pair<const char*, combat::Terrain> kinds[] = {
        {"obstacles", combat::Terrain::Obstacle}, {"difficult", combat::Terrain::Difficult}, {"doors", combat::Terrain::Door}};
    for (const auto& [key, terrain] : kinds) {
        if (!map.contains(key)) continue;
        for (const auto& o : get_array(map, key, mctx)) mark(hex_from_json(o, mctx + "." + key), terrain, mctx + "." + key);
    }
    if (map.contains("traps")) {
        const std::string tctx = mctx + ".traps";
        for (const auto& t : get_array(map, "traps", mctx)) {
            only_keys(t, {"hex", "damage", "conditions"}, tctx);
            scenario::TrapDef trap;
            trap.hex = hex_from_json(field(t, "hex", tctx), tctx);
            trap.damage = get_int(t, "damage", tctx);
            if (trap.damage < 0) fail(tctx, "'damage' must not be negative");
            if (t.contains("conditions")) trap.conditions = condition_list_from_json(t["conditions"], content.rules, tctx);
            if (trap.damage == 0 && trap.conditions.empty()) fail(tctx, "a trap needs damage or conditions");
            mark(trap.hex, combat::Terrain::Trap, tctx);
            s.traps.push_back(std::move(trap));
        }
    }

    std::set<combat::Hex> used;
    auto place = [&](combat::Hex h, const std::string& c) {
        const auto t = s.map.terrain(h);
        if (!s.map.contains(h) || (t != combat::Terrain::Open && t != combat::Terrain::Difficult)) {
            fail(c, "hex [" + std::to_string(h.q) + ", " + std::to_string(h.r) + "] must be open ground");
        }
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
