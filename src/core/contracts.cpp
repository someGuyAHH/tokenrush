#include "contracts.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "crew.hpp"
#include "game.hpp"
#include "util.hpp"

namespace {

// The raw stat a contract watches. Progress is "how much it grew since you accepted".
long long contractStat(const GameState& g, ContractKind k) {
    switch (k) {
        case ContractKind::Earn:     return g.lifetimeEarned;
        case ContractKind::Mine:     return g.manualMines;
        case ContractKind::Jackpots: return g.jackpots;
        case ContractKind::Streak:   return g.contractPeakStreak;   // (read directly in contractProgress)
        case ContractKind::BotEarn:  return g.botTotal;
        case ContractKind::Count:    break;
    }
    return 0;
}

// Two significant figures, so targets read like 1,200 instead of 1,187.
long long niceRound(double v) {
    if (v < 100.0) return std::max(1LL, std::llround(v));
    double mag = std::pow(10.0, std::floor(std::log10(v)) - 1.0);
    return toTokens(std::round(v / mag) * mag);
}

int roundTo30(double seconds) { return std::max(30, (int)std::lround(seconds / 30.0) * 30); }

Contract makeOffer(const GameState& g, std::mt19937& rng) {
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    auto between = [&](double lo, double hi) { return lo + (hi - lo) * uni(rng); };

    std::vector<ContractKind> kinds = {ContractKind::Earn, ContractKind::Mine, ContractKind::Streak};
    if (g.magnet >= 2)    kinds.push_back(ContractKind::Jackpots);
    if (g.bot >= 1)       kinds.push_back(ContractKind::BotEarn);

    Contract c;
    c.kind = kinds[std::uniform_int_distribution<size_t>(0, kinds.size() - 1)(rng)];
    c.client = std::uniform_int_distribution<int>(0, CLIENT_COUNT - 1)(rng);
    c.big = uni(rng) < 0.35;
    c.duration = roundTo30(c.big ? between(240, 420) : between(150, 240));
    c.expiresIn = (int)between(300, 600);

    // Size the job from how you've actually been playing, but never below what the formulas
    // expect, so a quiet stretch doesn't hand out trivial targets.
    double power = std::max(incomeEstimate(g), g.incomeRate);
    double minesPerSec = std::max(c.big ? between(0.40, 0.55) : between(0.25, 0.35),
                                  g.mineRate * (c.big ? between(1.2, 1.5) : between(0.9, 1.15)));
    double jackpotP = std::min(0.20, 0.01 + 0.005 * g.magnet);

    switch (c.kind) {
        case ContractKind::Earn:
            c.target = niceRound(std::max(100.0, power * c.duration * (c.big ? between(1.2, 1.5) : between(0.85, 1.1))));
            break;
        case ContractKind::Mine:
            c.target = niceRound(c.duration * minesPerSec);
            break;
        case ContractKind::Jackpots:
            c.target = std::max(1LL, std::llround(c.duration * minesPerSec * jackpotP));
            break;
        case ContractKind::Streak:
            c.target = std::llround(c.big ? between(45, 70) : between(25, 35));
            break;
        case ContractKind::BotEarn:
            c.target = niceRound(std::max(50.0, botRatePerSec(g) * c.duration * (c.big ? between(1.4, 1.7) : between(0.9, 1.0))));
            break;
        case ContractKind::Count:
            break;
    }

    double value = power * c.duration;
    double brokerBonus = 1.0 + std::min(2.0, 0.10 * roleLevels(g, Role::Broker));
    if (c.big) {
        c.rewardTokens = niceRound(value * 0.5);
        c.rewardShards = std::llround(between(4, 8) * brokerBonus);
        c.stake = std::clamp<long long>(std::llround(value * 0.1 / SHARD_FAIR_PRICE), 3, 60);
    } else {
        c.rewardTokens = niceRound(value * 0.25);
        c.rewardShards = std::llround(between(1, 3) * brokerBonus);
        c.stake = 0;
    }
    return c;
}

// Removes contract `idx` as a failure: the stake is gone and heat goes up.
Note fail(GameState& g, size_t idx, const std::string& why) {
    Contract c = g.contracts[idx];   // a copy: erase() would leave a reference dangling
    g.contracts.erase(g.contracts.begin() + (std::ptrdiff_t)idx);
    int heatGain = c.big ? 2 : 1;
    g.heat = std::min(MAX_HEAT, g.heat + heatGain);
    g.heatClock = heatCoolSeconds(g);
    g.contractsFailed += 1;

    std::string msg = why + " " + contractText(c) + ".";
    if (c.stake > 0) msg += " Lost your " + num(c.stake) + "-shard stake.";
    msg += " Heat +" + std::to_string(heatGain) + " (now " + std::to_string(g.heat) + "). Market spreads widen.";
    return {Sev::Bad, msg};
}

}  // namespace

long long contractProgress(const GameState& g, const Contract& c) {
    if (c.kind == ContractKind::Streak) return g.contractPeakStreak;   // a peak, not a delta
    return std::max(0LL, contractStat(g, c.kind) - c.start);
}

std::string contractText(const Contract& c) {
    if (c.target == 1) return CONTRACT_KINDS[(int)c.kind].single;
    std::string fmt = CONTRACT_KINDS[(int)c.kind].format;
    size_t at = fmt.find("%s");
    // Big targets read better short: "Earn 32M tokens" rather than "Earn 32,000,000 tokens".
    if (at != std::string::npos) fmt.replace(at, 2, c.target >= 1'000'000 ? compact(c.target) : num(c.target));
    return fmt;
}

int heatCoolSeconds(const GameState& g) {
    return (int)std::lround(HEAT_COOL_SECONDS / (1.0 + 0.5 * roleLevels(g, Role::Fixer)));
}

double auditChance(const GameState& g) {
    if (g.heat < AUDIT_MIN_HEAT) return 0.0;
    return (double)(g.heat - AUDIT_MIN_HEAT + 1) / 1500.0;
}

double auditBlockChance(const GameState& g) { return std::min(0.75, 0.15 * roleLevels(g, Role::Fixer)); }
double auditShare(const GameState& g) { return std::min(0.20, 0.02 * g.heat); }

Notes acceptContract(GameState& g, int idx) {
    if (!isUnlocked(g, Feature::Contracts)) return {{Sev::Warn, lockedText(g, Feature::Contracts)}};
    int slots = contractSlots(g);
    if ((int)g.contracts.size() >= slots) {
        return {{Sev::Warn, slots == 1 ? "You already have a contract. Finish it or 'abandon' it."
                                       : "Both contract slots are taken. Finish or abandon one first."}};
    }
    if (idx < 0 || idx >= (int)g.offers.size()) return {{Sev::Warn, "No such offer."}};

    Contract c = g.offers[(size_t)idx];
    for (const Contract& running : g.contracts) {
        // Two jobs watching the same stat would count the same work twice.
        if (running.kind == c.kind) return {{Sev::Warn, "You're already on a job like that. Pick a different kind."}};
    }
    if (g.shards < c.stake) {
        return {{Sev::Warn, "This job needs a stake of " + count(c.stake, "shard") + ". You have " + num(g.shards) +
                            ". (Buy some on the market.)"}};
    }

    g.shards -= c.stake;
    if (c.kind == ContractKind::Streak) g.contractStreak = g.contractPeakStreak = 0;   // a fresh streak, not the one you're on
    c.start = contractStat(g, c.kind);
    c.secondsLeft = c.duration;
    g.contracts.push_back(c);
    g.offers.erase(g.offers.begin() + idx);
    if (g.offerClock <= 0) g.offerClock = OFFER_EVERY;

    std::string msg = "Accepted from " + std::string(CLIENTS[c.client]) + ": " + contractText(c) +
                      " within " + duration(c.duration) + ".";
    if (c.stake > 0) msg += " Staked " + count(c.stake, "shard") + ".";
    return {{Sev::Good, msg}};
}

Notes abandonContract(GameState& g, int idx) {
    if (g.contracts.empty()) return {{Sev::Info, "You don't have a contract."}};
    if (idx < 0 || idx >= (int)g.contracts.size()) return {{Sev::Warn, "No such contract."}};
    return {fail(g, (size_t)idx, "You walked away from")};
}

void fillOfferBoard(GameState& g, std::mt19937& rng) {
    if (!g.offers.empty()) return;
    while ((int)g.offers.size() < OFFER_COUNT) g.offers.push_back(makeOffer(g, rng));
    g.offerClock = OFFER_EVERY;
}

void checkContract(GameState& g, MessageLog& log) {
    for (size_t i = g.contracts.size(); i-- > 0;) {
        if (contractProgress(g, g.contracts[i]) < g.contracts[i].target) continue;
        Contract c = g.contracts[i];
        g.contracts.erase(g.contracts.begin() + (std::ptrdiff_t)i);
        pay(g, log, c);
    }
}

void pay(GameState& g, MessageLog& log, const Contract& c) {
    earn(g, c.rewardTokens, Source::Contracts);
    g.rateSkip += c.rewardTokens;   // a payout isn't a sign you got faster
    g.shards += c.rewardShards + c.stake;
    g.contractsDone += 1;

    std::string msg = "CONTRACT DONE: " + contractText(c) + ". +" + num(c.rewardTokens) + " tokens, +" +
                      count(c.rewardShards, "shard");
    if (c.stake > 0) msg += ", stake returned";
    if (c.big && g.heat > 0) {   // pulling off a big job buys you some goodwill
        g.heat -= 1;
        msg += ". Heat -1";
    }
    log.push(Sev::Good, msg + ".");
}

void contractsTick(GameState& g, MessageLog& log, std::mt19937& rng) {
    std::uniform_real_distribution<double> uni(0.0, 1.0);

    // Heat cools down slowly on its own (not with the Fever strain).
    if (g.heat <= 0 || infected(g, Strain::Fever)) {
        g.heatClock = heatCoolSeconds(g);
    } else if (--g.heatClock <= 0) {
        g.heat -= 1;
        g.heatClock = heatCoolSeconds(g);
        log.push(Sev::Info, "Things are quieter. Heat cooled to " + std::to_string(g.heat) + ".");
    }

    // Audits
    if (uni(rng) < auditChance(g)) {
        if (uni(rng) < auditBlockChance(g)) {
            log.push(Sev::Good, "An auditor came sniffing around. Your Fixer made them go away.");
        } else {
            long long seized = (long long)std::floor((double)g.tokens * auditShare(g));
            g.tokens -= seized;
            g.audits += 1;
            g.heat -= 1;
            log.push(Sev::Bad, "AUDIT! " + num(seized) + " tokens seized. Heat -1 (now " + std::to_string(g.heat) + ").");
        }
    }

    // The jobs you're working on
    checkContract(g, log);
    for (size_t i = g.contracts.size(); i-- > 0;) {
        Contract& c = g.contracts[i];
        --c.secondsLeft;
        if (c.secondsLeft == CONTRACT_WARN_AT) {
            log.push(Sev::Warn, std::to_string(CONTRACT_WARN_AT) + "s left: " + contractText(c) + " (" +
                                num(contractProgress(g, c)) + "/" + num(c.target) + ").");
        }
        if (c.secondsLeft <= 0) log.push(fail(g, i, "CONTRACT FAILED:"));
    }

    // The offer board
    for (Contract& o : g.offers) --o.expiresIn;
    std::erase_if(g.offers, [](const Contract& o) { return o.expiresIn <= 0; });

    if (g.offers.empty()) {
        fillOfferBoard(g, rng);
    } else if ((int)g.offers.size() < OFFER_COUNT && --g.offerClock <= 0) {
        g.offers.push_back(makeOffer(g, rng));
        g.offerClock = OFFER_EVERY;
        if (isUnlocked(g, Feature::Contracts)) log.push(Sev::Info, "A new contract offer is on the board. (contracts)");
    }
}
