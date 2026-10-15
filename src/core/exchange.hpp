#pragma once
// Between profiles: `pay` sends tokens to another profile, `borrow` asks one for some. Both
// games have to be running (on the same computer, with the same profiles folder), and only
// tokens move: never shards.
//
// There's no server: the games meet in a folder next to the profiles, <data>/exchange/.
//   <name>/alive          each running game's heartbeat, rewritten every 2 seconds
//   <name>/<id>.pay       tokens for <name>: the payer has already taken them out
//   <name>/<id>.ask       <name> is asked for a loan
// The receiver claims a payment by renaming it (<id>.taken), then credits it. A payer who
// hears nothing for TRANSFER_TIMEOUT seconds takes it back by renaming it too (<id>.back),
// then refunds itself. A rename is atomic, so exactly one of the two wins: a payment is never
// both delivered and refunded, and never lost.
//
// Each side keeps a balance per profile (what they owe you, or you them): a payment first pays
// off what you owe them, then lends what they asked for, and the rest is a gift.

#include <filesystem>
#include <string>
#include <vector>

#include "state.hpp"

inline constexpr int TRANSFER_TIMEOUT = 20;    // seconds before an unclaimed payment comes back
inline constexpr int ASK_SECONDS      = 300;   // how long a borrow request stands
inline constexpr int ALIVE_SECONDS    = 8;     // a heartbeat older than this means the game isn't running

// Joins the exchange as `me` (a profile's name). Without this (--save-dir), pay and borrow say no.
// Returns false (with why) if the folder can't be made. `warning` says if `me` looks open elsewhere.
bool exchangeStart(const std::filesystem::path& dataRoot, const std::string& me, std::string& why, std::string& warning);
void exchangeStop(GameState& g);                       // gives back what nobody claimed, leaves the exchange
bool exchangeOpen();
std::vector<std::string> onlineProfiles();            // the others that are running right now

Notes payProfile(GameState& g, const std::string& who, long long amount);   // amount 0: what they asked for, or what you owe
Notes askToBorrow(GameState& g, const std::string& who, long long amount);
long long balanceWith(const GameState& g, const std::string& who);          // + they owe you, - you owe them
void exchangeTick(GameState& g, MessageLog& log);      // once per second: heartbeat, the inbox, payments on their way
