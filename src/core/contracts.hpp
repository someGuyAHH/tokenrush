#pragma once
// Contracts are timed jobs from shady clients. Finish one before its timer runs out and you
// get tokens and shards (and your stake back). Miss it, or walk away, and you lose the stake
// and gain Heat. Heat widens the market spread and, from 3 up, invites audits that seize tokens.
// The timer only runs while the game is open.

#include <random>
#include <string>

#include "state.hpp"

inline constexpr int OFFER_COUNT       = 3;
inline constexpr int OFFER_EVERY       = 60;    // seconds between new offers while the board isn't full
inline constexpr int MAX_HEAT          = 10;
inline constexpr int HEAT_COOL_SECONDS = 900;   // per point of heat, before Fixers help
inline constexpr int AUDIT_MIN_HEAT    = 3;
inline constexpr int CONTRACT_WARN_AT  = 30;    // seconds left when the log warns you

long long contractProgress(const GameState& g, const Contract& c);
std::string contractText(const Contract& c);    // "Mine 60 times"

int heatCoolSeconds(const GameState& g);
double auditChance(const GameState& g);         // per second
double auditBlockChance(const GameState& g);    // chance a Fixer makes an audit go away
double auditShare(const GameState& g);          // fraction of tokens an audit takes

Notes acceptContract(GameState& g, int offerIdx);
Notes abandonContract(GameState& g, int contractIdx = 0);

void fillOfferBoard(GameState& g, std::mt19937& rng);                   // only if the board is empty (startup)
void checkContract(GameState& g, MessageLog& log);                     // pays out every job whose target is reached
void pay(GameState& g, MessageLog& log, const Contract& c);           // rewards for a finished job (already removed)
void contractsTick(GameState& g, MessageLog& log, std::mt19937& rng);  // once per second
