#pragma once
// Choice events: now and then someone makes you an offer, and you answer yes or no.
//
//   Shady Broker       cheap shards, but heat +1             (Market and Contracts open)
//   The Investor       lend 10% of your tokens: usually 1.3x back in 10 minutes, sometimes nothing
//   Rival Miner        bet 5% of your tokens you can out-mine them in 60 seconds
//   Raise Request      a crew member wants a raise           (Crew open)
//   Bribe the Auditor  pay to make 2 heat go away            (heat 3+)
//   Curio Dealer       a curio no shop sells, at 1.5x what it's worth   (Curios open)
//
// They're rare (one every 10-15 minutes), never stack, and expire after CHOICE_SECONDS.
// Letting one expire is the same as saying no, except nobody takes it personally.

#include <random>
#include <string>

#include "state.hpp"

inline constexpr int CHOICE_SECONDS   = 45;
inline constexpr int CHOICE_EVERY_MIN = 600;
inline constexpr int CHOICE_EVERY_MAX = 900;
inline constexpr int INVEST_SECONDS   = 600;
inline constexpr int RACE_SECONDS     = 60;
inline constexpr long long RACE_MINES = 40;
inline constexpr double DEALER_MARKUP = 1.5;
long long dealerPrice(int curio);

void choicesTick(GameState& g, MessageLog& log, std::mt19937& rng);   // once per second
void checkRace(GameState& g, MessageLog& log);                         // after mining: did you just win?

bool hasChoice(const GameState& g);
std::string choiceText(const GameState& g);       // the offer, in a sentence or two
Notes answerChoice(GameState& g, bool yes, std::mt19937& rng);

// Forces an offer onto the table (tests, and the simulator). False if it isn't possible now.
bool offerChoice(GameState& g, ChoiceKind kind, std::mt19937& rng);
