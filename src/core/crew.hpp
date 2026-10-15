#pragma once
// The crew: named hires who work in the background. You hire them with shards, and every
// payday each one wants about (level) shards. Anyone you can't pay walks out.

#include <ctime>
#include <random>
#include <string>

#include "state.hpp"

// How many crew you can have and how far they can train depend on the Orrery:
// see crewCap() and crewLevelCap() in game.hpp.
inline constexpr int CANDIDATE_COUNT   = 3;
inline constexpr int CANDIDATE_REFRESH = 300;   // seconds until new candidates show up
inline constexpr int PAYDAY_SECONDS    = 300;
inline constexpr int MINER_EVERY       = 5;     // seconds between a Miner's swings
inline constexpr int SCOUT_EVERY       = 60;    // seconds between a Scout's searches
inline constexpr int CREW_REPORT_EVERY = 60;

// Promotions: a crew member at their level cap can be promoted for shards. Each star raises
// their cap by PROMO_LEVELS and makes every level PROMO_EFFECT more effective. The price
// grows by PROMO_GROWTH per star, so there's no hard limit, just a point where it stops paying.
inline constexpr int    PROMO_LEVELS = 3;
inline constexpr double PROMO_EFFECT = 0.25;
inline constexpr double PROMO_BASE   = 300.0;   // shards for the first star
inline constexpr double PROMO_GROWTH = 3.0;
inline constexpr int    MAX_RAISES   = 3;
inline constexpr double RAISE_EFFECT = 0.25;
inline constexpr int    NIGHT_OWL_IDLE = 30;    // seconds without mining before a Night Owl wakes up

// How much a crew member actually counts for: their level times promotions, trait, raises,
// sulking and (for Night Owls) whether you're mining. Role effects are built from this.
// ----- Expeditions -----
// A member can go off exploring: they don't work while away (wages still come out on payday),
// and come back with shards, sometimes something better, sometimes an injury.
inline constexpr int EXPEDITION_COUNT = 3;
inline constexpr int HURT_SECONDS = 900;   // half effort after a rough trip
struct ExpeditionInfo {
    const char* id;
    const char* name;      // "a deep expedition"
    int seconds;
    double loot;           // how many shards, relative to a short trip
    double curioChance;    // finds a curio no shop sells
    double traitChance;    // comes back with a trait (if they had none)
    double levelChance;    // learns a thing or two: +1 level (if below the cap)
    double injuryChance;   // before Fixers
};
extern const ExpeditionInfo EXPEDITIONS[EXPEDITION_COUNT];
int expeditionByName(const std::string& word);                 // "short", "long", "deep" (or 1-3); -1 if neither
long long expeditionLoot(const GameState& g, const CrewMember& c, int trip);   // the typical shards they bring back
double injuryChance(const GameState& g, int trip);             // with your Fixers' help
Notes sendExpedition(GameState& g, int crewIdx, int trip);
bool isAway(const CrewMember& c);

double effectiveLevel(const GameState& g, const CrewMember& c);
double roleLevels(const GameState& g, Role r);   // sum of effective levels of everyone with that role
int levelCap(const GameState& g, const CrewMember& c);          // crewLevelCap() plus promotions
long long hireCost(const GameState& g, const CrewMember& c);    // in shards
long long trainCost(const CrewMember& c);                       // in shards
long long promoteCost(const CrewMember& c);                     // in shards
long long wageOf(const GameState& g, const CrewMember& c);      // shards per payday
long long totalWages(const GameState& g);
double scoutChance(const GameState& g, const CrewMember& c);
int scoutMaxFind(const GameState& g, const CrewMember& c);      // a find is scoutMinFind..this many shards
int scoutMinFind(const GameState& g, const CrewMember& c);
long long minerSwing(const GameState& g, const CrewMember& c);  // tokens per swing, events ignored
std::string traitName(const CrewMember& c);                     // "" for no trait
std::string crewEffectText(const GameState& g, const CrewMember& c);   // "+7.1M tokens every 5s"
std::string starsText(const CrewMember& c);                     // "***" (empty without promotions)

void fillCandidates(GameState& g, std::mt19937& rng);   // only if nobody is waiting (startup)

Notes hire(GameState& g, int candidateIdx);
Notes fire(GameState& g, int crewIdx);
Notes train(GameState& g, int crewIdx);
Notes promote(GameState& g, int crewIdx);

void crewTick(GameState& g, MessageLog& log, std::time_t t, std::mt19937& rng);   // once per second
