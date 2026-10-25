#pragma once

#include "cards/hero_cards.hpp"
#include "character/character_sheet.hpp"
#include "combat/events.hpp"
#include "combat/hex.hpp"
#include "data/content.hpp"
#include "dice/d20.hpp"
#include "dice/rng.hpp"
#include "scenario/scenario_def.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rpg::combat {

struct ActiveCondition {
    std::string id;
    int rounds = 1;      // turns of the affected creature left
    bool fresh = true;   // applied during the creature's own turn: does not tick at its end

    friend bool operator==(const ActiveCondition&, const ActiveCondition&) = default;
};

enum class HeroStatus { Active, Dying, Stable, Dead, Exhausted };

struct HeroState {
    character::CharacterSheet sheet;  // current HP lives here
    cards::HeroCards cards;
    Hex hex;
    std::vector<ActiveCondition> conditions;
    HeroStatus status = HeroStatus::Active;
    int death_successes = 0;
    int death_failures = 0;
    bool acted = false;        // had a turn this round
    bool long_rested = false;  // this round

    bool on_board() const { return status != HeroStatus::Dead && status != HeroStatus::Exhausted; }
};

struct MonsterState {
    std::string id;    // "bandit#1"
    std::string type;
    Hex hex;
    int hp = 0;
    std::vector<ActiveCondition> conditions;
    bool alive = true;
    bool acted = false;
};

struct MonsterDeckState {
    std::string type;
    std::vector<int> draw;     // indices into MonsterDef::deck, next card at the back
    std::vector<int> discard;
    int current = -1;          // drawn this round, -1 when none
};

// A choice the player (or an AI) has to make. Exactly one of the option lists
// is filled, depending on the kind. Options are listed in a stable order.
struct Decision {
    enum class Kind { SelectCards, PlayTurn, Move, Target, LongRestLoss, ShortRest };

    Kind kind = Kind::SelectCards;
    std::string actor;  // hero id

    std::vector<cards::RoundSelection> selections;  // SelectCards
    std::vector<cards::TurnPlay> plays;             // PlayTurn
    std::vector<Hex> hexes;                         // Move: destinations, including staying put
    std::vector<std::string> targets;               // Target: creature ids, "" = stop choosing
    std::vector<std::string> cards;                 // LongRestLoss
    // ShortRest: option 0 = no, option 1 = yes

    // Set for Move and Target: the action being resolved.
    std::optional<cards::ActionDef> action;

    std::size_t option_count() const;
};

enum class Outcome { Ongoing, Victory, Defeat };

// A full battle, run as a state machine.
//
// The engine advances on its own (monster turns, rolls, end of round) until a
// hero decision is needed, then stops with pending() set. decide(i) applies
// option i and advances again. Decisions with a single option are taken
// automatically and never appear. All randomness comes from the seeded Rng,
// so the same seed and the same choices give the same battle.
//
// `content` must outlive the Combat.
class Combat {
public:
    // Heroes must be valid for their class, with a valid deck, one per hero start.
    // Throws std::invalid_argument otherwise.
    // `game_master` is the id of a profile in content.game_masters; "classic"
    // always exists. Throws std::invalid_argument for an unknown one.
    Combat(const data::Content& content, scenario::ScenarioDef scenario, std::vector<character::CharacterSheet> party,
           std::uint64_t seed, std::string_view game_master = monsters::GameMasterDef::kClassic);

    Outcome outcome() const { return outcome_; }
    bool finished() const { return outcome_ != Outcome::Ongoing; }

    // nullptr once the combat is over.
    const Decision* pending() const { return pending_ ? &*pending_ : nullptr; }
    // Throws std::out_of_range for an invalid option and std::logic_error when nothing is pending.
    void decide(std::size_t option);

    const std::vector<Event>& events() const { return events_; }
    int round() const { return round_; }
    const scenario::ScenarioDef& scenario() const { return scenario_; }
    const std::vector<HeroState>& heroes() const { return heroes_; }
    const std::vector<MonsterState>& monsters() const { return monsters_; }
    const data::Content& content() const { return *content_; }
    const monsters::GameMasterDef& game_master() const { return gm_; }

    // Initiative of a creature this round, or nullopt if it has none.
    std::optional<int> initiative_of(const std::string& id) const;

    // Saves the whole battle, including a turn in progress and the event log.
    // Loading it and making the same choices continues the battle exactly.
    nlohmann::json to_json() const;
    // Throws data::DataError when the save is malformed or does not match the content.
    static Combat from_json(const data::Content& content, const nlohmann::json& j);

private:
    explicit Combat(const data::Content& content) : content_(&content), rng_(0) {}

    enum class Phase { Selecting, Acting, EndOfRound, Finished };
    enum class Stage { None, ChoosePlay, Executing, LongRestLoss };

    struct TurnOrderEntry {
        bool hero = true;
        std::size_t index = 0;
        int initiative = 0;
        int tiebreak = 0;
    };

    struct TurnProgress {
        Stage stage = Stage::None;
        std::optional<cards::TurnPlay> play;
        std::vector<cards::PlannedStep> steps;
        std::size_t step = 0;
        std::size_t action = 0;
        bool action_started = false;
        bool announced = false;            // HalfUsed emitted for the current step
        std::vector<std::string> chosen;  // targets already picked for the current action
    };

    // Engine loop.
    void advance();
    bool set_decision(Decision d);  // false (and nothing pending) when it was auto-resolved
    void apply(const Decision& d, std::size_t option);

    // Phases.
    bool step_selecting();
    void start_acting();
    bool step_acting();
    bool step_hero_turn(std::size_t hero);
    bool step_hero_action(std::size_t hero);
    void finish_hero_turn(std::size_t hero);
    void complete_action();
    std::optional<std::size_t> monster_focus(std::size_t monster, int range) const;
    Hex monster_destination(std::size_t monster, std::size_t target, int steps, int range, bool jump) const;
    void run_monster_turn(std::size_t monster);
    dice::RollMode monster_roll_mode(const MonsterState& m, const cards::ActionDef& a) const;

    // Tactical game master (game_master.cpp).
    // The profile with that id; "classic" works even when the content lacks it.
    static std::optional<monsters::GameMasterDef> find_game_master(const data::Content& content, std::string_view id);
    bool hero_heals(const HeroState& h) const;
    // Score of attacking each hero with `a`, minimum long for heroes that cannot be targeted.
    std::vector<long> gm_target_scores(std::size_t monster, const cards::ActionDef& a) const;
    // Where to end `move`, given the attack that follows it in the card (if any).
    Hex gm_destination(std::size_t monster, const cards::ActionDef& move, const cards::ActionDef* attack) const;
    // Heroes in reach of `a` from where the monster stands, best target first.
    std::vector<std::size_t> gm_target_order(std::size_t monster, const cards::ActionDef& a) const;
    bool step_end_of_round();
    void begin_round();

    // Rules helpers.
    const character::ClassDef& class_of(const HeroState& h) const;
    const monsters::MonsterDef& def_of(const MonsterState& m) const;
    bool has_effect(const std::vector<ActiveCondition>& conds, bool character::ConditionEffects::*flag) const;
    int sum_effect(const std::vector<ActiveCondition>& conds, int character::ConditionEffects::*field) const;
    bool hero_occupies(Hex h) const;
    bool monster_occupies(Hex h) const;
    bool occupied(Hex h) const { return hero_occupies(h) || monster_occupies(h); }
    // Monsters avoid traps and cannot open doors.
    bool monster_can_enter(Hex h) const;
    // Within range and in line of sight.
    bool in_reach(Hex from, Hex to, int range) const;
    // Moves a creature along `path` hex by hex, opening doors (heroes) and
    // springing traps; stops early if the creature falls.
    void walk(bool hero, std::size_t index, const std::vector<Hex>& path, bool jump, bool forced);
    void trigger_trap(Hex h, bool hero, std::size_t index);
    std::vector<Hex> hero_move_options(std::size_t hero, const cards::ActionDef& a) const;
    std::vector<std::string> hero_target_options(std::size_t hero, const cards::ActionDef& a) const;

    void hero_attacks_monster(std::size_t hero, std::size_t monster, const cards::ActionDef& a);
    void monster_attacks_hero(std::size_t monster, std::size_t hero, const cards::ActionDef& a);
    void damage_monster(std::size_t monster, int amount);
    void damage_hero(std::size_t hero, int amount, bool critical);
    void heal_hero(const std::string& healer, std::size_t hero, int amount);
    void heal_monster(std::size_t monster, int amount);
    void apply_conditions(const std::string& target, std::vector<ActiveCondition>& conds,
                          const std::vector<cards::ConditionApply>& list, bool own_turn,
                          const monsters::MonsterDef* immune);
    void tick_conditions(const std::string& who, std::vector<ActiveCondition>& conds);
    void force_move(std::size_t monster, Hex attacker, int steps, bool push);
    void knock_out_selection(std::size_t hero);
    void check_outcome();

    void emit(Event e) { events_.push_back(std::move(e)); }
    std::optional<std::size_t> hero_index(const std::string& id) const;
    std::optional<std::size_t> monster_index(const std::string& id) const;

    const data::Content* content_;
    monsters::GameMasterDef gm_;
    scenario::ScenarioDef scenario_;
    dice::Rng rng_;
    std::vector<HeroState> heroes_;
    std::vector<MonsterState> monsters_;
    std::vector<MonsterDeckState> decks_;

    int round_ = 0;
    Phase phase_ = Phase::Selecting;
    Outcome outcome_ = Outcome::Ongoing;
    std::vector<TurnOrderEntry> order_;
    std::size_t turn_ = 0;
    std::size_t cursor_ = 0;  // hero being asked during Selecting / EndOfRound
    bool end_resolved_ = false;  // death saves, ticks and decks done for this round
    TurnProgress progress_;
    std::optional<Decision> pending_;
    std::vector<Event> events_;
};

}  // namespace rpg::combat
