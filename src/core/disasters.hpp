#pragma once
// Disasters: the dark sibling of random events. Once the Crew is open, every 20-35 minutes of
// play something goes wrong. It always starts with a warning, and most can be stopped while it
// lasts (respondDisaster): put the fire out, chase the thieves, brace the Orrery. Some cost a
// little to stop, some cost effort. If it hits:
//
//   Power Surge     half the Drones burn out for a while (temporary: they come back by themselves)
//   Bot Breakdown   the Mining Bot stops until you repair it
//   Vault Fire      part of your shards burn (gone)
//   Thieves         part of your tokens are stolen (gone)
//   Quake           an Orrery stage cracks: its perk is off until you repair it
//   Short Circuit   an equipped gadget breaks until you repair it
//
// Fixers soften all of it: less damage, cheaper repairs. Timers only run while the game is open.

#include <random>
#include <string>
#include <vector>

#include "state.hpp"

inline constexpr int DISASTER_EVERY_MIN = 1200;   // seconds of play between disasters
inline constexpr int DISASTER_EVERY_MAX = 2100;
inline constexpr int CHASE_MINES        = 15;     // mines by hand that catch the thieves
inline constexpr int PAUSE_SECONDS      = 60;     // Drones and Bot off after pulling the breakers
inline constexpr int UNPLUG_SECONDS     = 120;    // gadgets off after unplugging them
inline constexpr int SURGE_SECONDS      = 600;    // how long burnt-out Drones stay down (before Fixers)

enum class DisasterKind : int { Surge, BotBreak, Fire, Thieves, Quake, Short, Count };
inline constexpr int DISASTER_KIND_COUNT = (int)DisasterKind::Count;
struct DisasterInfo {
    const char* name;
    const char* warning;   // what you notice
    int warnSeconds;
    const char* verb;      // the command that stops it ("douse")
    const char* action;    // what that does, for the screens ("put it out")
};
extern const DisasterInfo DISASTERS[];   // indexed by DisasterKind

bool disasterPending(const GameState& g);         // a warning is running
std::string disasterText(const GameState& g);     // the warning, what stopping it costs, and what happens if you don't
double fixerSoften(const GameState& g);           // damage and repair bills are multiplied by this (1 without Fixers)
Notes respondDisaster(GameState& g);              // stop it (or start the chase)
void checkDisaster(GameState& g, MessageLog& log);   // after mining: did the chase just succeed?
void disastersTick(GameState& g, MessageLog& log, std::mt19937& rng);   // once per second

// What the disasters can leave broken, and what fixing it costs.
struct Repair {
    enum Kind { Bot, Stage, Gadget } kind = Bot;
    int index = 0;         // the Orrery stage or the gadget
    std::string what;      // "the Mining Bot", "the Orrery's Second Ring", "the Tuning Fork"
    std::string effect;    // what's off until it's fixed
    long long tokens = 0;
    long long shards = 0;
};
std::vector<Repair> repairs(const GameState& g);   // everything broken right now, in a stable order
Notes repair(GameState& g, int idx);                // one of repairs(); -1 = all you can afford
bool anythingBroken(const GameState& g);

// Forces a disaster's warning (tests, the simulator). False if it can't happen right now.
bool startDisaster(GameState& g, DisasterKind k, std::mt19937& rng);
