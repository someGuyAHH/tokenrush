#pragma once
// Contracts are timed jobs from shady clients. Finish one before its timer runs out and you
// get tokens and shards (and your stake back). Miss it, or walk away, and you lose the stake
// and gain Heat. Heat widens the market spread and, from 3 up, invites audits that seize tokens.
// The timer only runs while the game is open.
//
// Most jobs are won by reaching a target (mine 60 times). Endurance jobs (CONTRACT_KINDS[k].endurance)
// are won by lasting until the timer runs out without breaking their rule (hold 400 shards,
// don't mine by hand).
//
// Every client remembers how you've treated them: standing goes up when you deliver and down
// when you don't, and their rival takes it badly when you work for them. Standing sets the pay
// (and, at the bottom, whether they post jobs for you at all; at the top, signature jobs).

#include <random>
#include <string>

#include "state.hpp"

inline constexpr int OFFER_COUNT       = 3;
inline constexpr int OFFER_EVERY       = 60;    // seconds between new offers while the board isn't full
inline constexpr int MAX_HEAT          = 10;
inline constexpr int HEAT_COOL_SECONDS = 900;   // per point of heat, before Fixers help
inline constexpr int AUDIT_MIN_HEAT    = 3;
inline constexpr int CONTRACT_WARN_AT  = 30;    // seconds left when the log warns you

// ----- Standing -----
inline constexpr int STANDING_MIN = -10;
inline constexpr int STANDING_MAX = 10;
enum class Tier : int { Burned, Wary, Neutral, Friendly, Trusted, Count };
struct TierInfo {
    const char* name;
    int from;              // the lowest standing in this tier
    double pay;            // reward multiplier
    const char* desc;
};
extern const TierInfo TIERS[];   // indexed by Tier
Tier tierOf(int standing);
Tier clientTier(const GameState& g, int client);
int bestStanding(const GameState& g);   // the highest standing with anyone (for the goal)

long long contractProgress(const GameState& g, const Contract& c);
long long contractGoal(const Contract& c);       // what progress has to reach: the target, or the duration for endurance jobs
bool isEndurance(const Contract& c);
std::string contractText(const Contract& c);     // "Mine 60 times"
std::string progressText(const GameState& g, const Contract& c);   // "51/180", or "2m 10s of 5m 00s" for endurance jobs

// ----- Tips -----
// Every offer and running job carries a tip: how hard it looks from how you've actually been
// playing lately, and why. They're judged again every TIP_EVERY seconds, so they follow your
// pace, your wallet and your heat as they change.
inline constexpr int TIP_EVERY = 5;
enum class TipLevel : int { Easy, Doable, Tight, Unlikely, OutOfReach, Count };
extern const char* const TIP_NAMES[];          // "Easy", ... "Out of reach"
void judgeContract(const GameState& g, Contract& c, bool running);   // fills c.tipLevel and c.tip
void judgeAll(GameState& g);                    // every offer and running job

int heatCoolSeconds(const GameState& g);
int auditMinHeat(const GameState& g);           // AUDIT_MIN_HEAT (the same for everyone now)
double auditChance(const GameState& g);         // per second
double auditBlockChance(const GameState& g);    // chance a Fixer makes an audit go away
double auditShare(const GameState& g);          // fraction of tokens an audit takes

Notes acceptContract(GameState& g, int offerIdx);
Notes abandonContract(GameState& g, int contractIdx = 0);

void fillOfferBoard(GameState& g, std::mt19937& rng);                   // only if the board is empty (startup)
void checkContract(GameState& g, MessageLog& log);                     // pays out finished jobs, fails broken endurance jobs
void pay(GameState& g, MessageLog& log, const Contract& c);           // rewards for a finished job (already removed)
void contractsTick(GameState& g, MessageLog& log, std::mt19937& rng);  // once per second
