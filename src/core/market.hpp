#pragma once
// The shard market. One price that wanders on its own, gets pushed around by your own
// trades, and drifts back toward SHARD_FAIR_PRICE. Buying costs a little above the price and
// selling pays a little below it (the spread); Heat widens the spread, Brokers narrow it.

#include <random>
#include <string>

#include "state.hpp"

inline constexpr long long MARKET_MAX_ORDER = 5000;   // shards per trade
// The market's stock: it only has so many shards for sale, and restocks a few a second (only while
// the game is open). What you sell goes back into it. Without this, a rich player bought the
// Orrery outright at any price; earned shards (Scouts, contracts, secrets) aren't limited.
inline constexpr long long MARKET_STOCK_MAX = 40'000;
inline constexpr long long MARKET_RESTOCK_PER_SEC = 8;   // 28,800 an hour

double marketSpread(const GameState& g);   // total spread as a fraction, e.g. 0.05 = 5%
double marketImpact(const GameState& g);   // how much one shard moves the price, e.g. 0.0025
double buyPrice(const GameState& g);       // what the next shard costs
double sellPrice(const GameState& g);      // what the next shard sells for

long long buyCost(const GameState& g, long long n);     // total tokens for n shards, impact included
long long sellValue(const GameState& g, long long n);   // total tokens for selling n shards
long long maxAffordable(const GameState& g);            // most shards your tokens (and the stock) allow in one order
std::string restockText();                              // "it restocks 8 a second (28.8K an hour)"

// True when buying n shards in one go would average more than 3x the current buy price:
// worth a "are you sure?" before spending a fortune on a price spike.
bool pricyOrder(const GameState& g, long long n);

Notes buyShards(GameState& g, long long n);
Notes sellShards(GameState& g, long long n);

void marketTick(GameState& g, MessageLog& log, std::mt19937& rng);   // once per second
