#include "contracts.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include "crew.hpp"
#include "game.hpp"
#include "util.hpp"

// ----- Standing -----
const TierInfo TIERS[] = {
    {"Burned",   -10, 0.00, "won't post jobs for you"},
    {"Wary",      -4, 0.80, "pay 20% less"},
    {"Neutral",   -1, 1.00, "normal pay"},
    {"Friendly",   3, 1.15, "pay 15% more"},
    {"Trusted",    6, 1.30, "pay 30% more, and now and then post a signature job"},
};
static_assert(std::size(TIERS) == (int)Tier::Count);

Tier tierOf(int standing) {
    int t = 0;
    for (int i = 0; i < (int)Tier::Count; ++i)
        if (standing >= TIERS[i].from) t = i;
    return (Tier)t;
}

Tier clientTier(const GameState& g, int client) { return tierOf(g.standing[(size_t)std::clamp(client, 0, CLIENT_COUNT - 1)]); }

int bestStanding(const GameState& g) { return *std::max_element(g.standing.begin(), g.standing.end()); }

namespace {

constexpr double SIGNATURE_CHANCE = 0.25;   // of a Trusted client's offers

// The raw stat a contract watches. Progress is "how much it grew since you accepted".
long long contractStat(const GameState& g, ContractKind k) {
    switch (k) {
        case ContractKind::Earn:     return g.lifetimeEarned;
        case ContractKind::Mine:     return g.manualMines;
        case ContractKind::Jackpots: return g.jackpots;
        case ContractKind::Streak:   return g.contractPeakStreak;   // (read directly in contractProgress)
        case ContractKind::BotEarn:  return g.botTotal;
        case ContractKind::Trade:    return g.shardsTraded;
        case ContractKind::Hold:     return 0;                      // endurance: nothing to count
        case ContractKind::CoolOff:  return g.heat;                 // progress is how far it fell
        case ContractKind::HandsOff: return g.manualMines;          // the rule: this mustn't move
        case ContractKind::Count:    break;
    }
    return 0;
}

bool mining(ContractKind k) { return k == ContractKind::Mine || k == ContractKind::Jackpots || k == ContractKind::Streak; }

// Which jobs a client of this style posts.
bool offers(ClientStyle st, ContractKind k) {
    switch (st) {
        case ClientStyle::Shady:     return k != ContractKind::CoolOff && k != ContractKind::Hold;
        case ClientStyle::Miners:    return mining(k);
        case ClientStyle::Merchants: return k == ContractKind::Earn || k == ContractKind::BotEarn || k == ContractKind::Trade ||
                                            k == ContractKind::Hold || k == ContractKind::CoolOff;
        case ClientStyle::SmallFry:  return k == ContractKind::Earn || k == ContractKind::Mine || k == ContractKind::Streak ||
                                            k == ContractKind::HandsOff || k == ContractKind::CoolOff;
        case ClientStyle::Count:     break;
    }
    return false;
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
    if (g.magnet >= 2) kinds.push_back(ContractKind::Jackpots);
    if (g.bot >= 1)    kinds.push_back(ContractKind::BotEarn);
    if (isUnlocked(g, Feature::Market)) {
        kinds.push_back(ContractKind::Trade);
        if (g.shards >= 40) kinds.push_back(ContractKind::Hold);
    }
    if (g.heat >= 2) kinds.push_back(ContractKind::CoolOff);
    if (g.drone + g.bot > 0 || !g.crew.empty()) kinds.push_back(ContractKind::HandsOff);   // something to live on meanwhile
    std::shuffle(kinds.begin(), kinds.end(), rng);

    // The first kind some client (one that still talks to you) wants, from one of them at random.
    Contract c;
    for (ContractKind k : kinds) {
        std::vector<int> who;
        for (int i = 0; i < CLIENT_COUNT; ++i)
            if (offers(CLIENTS[i].style, k) && clientTier(g, i) != Tier::Burned) who.push_back(i);
        if (who.empty()) continue;
        c.kind = k;
        c.client = who[std::uniform_int_distribution<size_t>(0, who.size() - 1)(rng)];
        break;
    }
    if (c.kind == ContractKind::Count) { c.kind = ContractKind::Earn; c.client = 3; }   // everyone burned: the intern, always
    const ClientInfo& client = CLIENTS[c.client];
    Tier tier = clientTier(g, c.client);

    c.big = client.style != ClientStyle::SmallFry && uni(rng) < 0.35;
    c.signature = tier == Tier::Trusted && uni(rng) < SIGNATURE_CHANCE;
    c.duration = c.kind == ContractKind::CoolOff ? roundTo30(c.big ? between(600, 900) : between(420, 600))
                                                 : roundTo30(c.big ? between(240, 420) : between(150, 240));
    if (c.signature) c.duration = roundTo30(c.duration * 1.5);
    c.expiresIn = (int)between(300, 600);

    // Size the job from how you've actually been playing, but never below what the formulas
    // expect, so a quiet stretch doesn't hand out trivial targets.
    double power = std::max(incomeEstimate(g), g.incomeRate);
    double minesPerSec = std::max(c.big ? between(0.40, 0.55) : between(0.25, 0.35),
                                  g.mineRate * (c.big ? between(1.2, 1.5) : between(0.9, 1.15)));
    double jackpotP = std::min(0.20, 0.01 + 0.005 * g.magnet);
    double value = power * c.duration;
    double kindPay = 1.0;   // some jobs are easier or harder than their duration says

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
        case ContractKind::Trade:
            // A few round trips of 50-100 shards (more and the price impact gets silly). Buying and
            // selling back costs about the spread on everything you trade.
            c.target = niceRound(c.big ? between(150, 400) : between(40, 120));
            kindPay = 0.8;
            break;
        case ContractKind::Hold:    // the cost is not spending them (crew, the Orrery) for a few minutes
            c.target = niceRound(std::max(20.0, (double)g.shards * (c.big ? between(0.85, 1.0) : between(0.55, 0.8))));
            kindPay = 0.6;
            break;
        case ContractKind::CoolOff:
            c.target = c.big ? 2 : 1;
            kindPay = 0.8;
            break;
        case ContractKind::HandsOff:   // you give up mining by hand for the whole job
            c.target = 1;
            kindPay = 1.2;
            break;
        case ContractKind::Count:
            break;
    }

    double tokensPay = 1.0, shardsPay = 1.0;
    switch (client.style) {
        case ClientStyle::Shady:     tokensPay = 1.4; break;
        case ClientStyle::Miners:    shardsPay = 1.25; break;
        case ClientStyle::Merchants: tokensPay = 0.8; shardsPay = 1.6; break;
        default:                     break;
    }
    double pay = kindPay * TIERS[(int)tier].pay * (c.signature ? 2.0 : 1.0);
    double brokerBonus = 1.0 + std::min(2.0, 0.10 * roleLevels(g, Role::Broker));
    if (c.big) {
        c.rewardTokens = niceRound(value * 0.5 * tokensPay * pay);
        c.rewardShards = std::llround(between(4, 8) * brokerBonus * shardsPay * pay);
        c.stake = c.signature ? 0 : std::clamp<long long>(std::llround(value * 0.1 / SHARD_FAIR_PRICE), 3, 60);
    } else {
        c.rewardTokens = niceRound(value * 0.25 * tokensPay * pay);
        c.rewardShards = std::max(1LL, std::llround(between(1, 3) * brokerBonus * shardsPay * pay));
        c.stake = 0;
    }
    return c;
}

// Moves standing with a client, and says so if it crossed into another tier.
std::string shiftStanding(GameState& g, int client, int delta) {
    int& st = g.standing[(size_t)client];
    Tier before = tierOf(st);
    st = std::clamp(st + delta, STANDING_MIN, STANDING_MAX);
    Tier after = tierOf(st);
    if (after == before) return "";
    std::string name = CLIENTS[client].name;
    name[0] = (char)std::toupper((unsigned char)name[0]);
    switch (after) {
        case Tier::Burned:   return " " + name + " won't work with you anymore.";
        case Tier::Wary:     return " " + name + (after < before ? " is getting wary of you." : " is willing to talk again.");
        case Tier::Neutral:  return " " + name + (after < before ? " has cooled on you." : " has forgiven you.");
        case Tier::Friendly: return " " + name + (after > before ? " likes working with you." : " doesn't quite trust you anymore.");
        case Tier::Trusted:  return " " + name + " trusts you now: signature jobs ahead.";
        case Tier::Count:    break;
    }
    return "";
}

// Removes contract `idx` as a failure: the stake is gone, heat goes up and the client remembers.
Note fail(GameState& g, size_t idx, const std::string& why) {
    Contract c = g.contracts[idx];   // a copy: erase() would leave a reference dangling
    g.contracts.erase(g.contracts.begin() + (std::ptrdiff_t)idx);
    int heatGain = c.big ? 2 : 1;
    g.heat = std::min(MAX_HEAT, g.heat + heatGain);
    g.heatClock = heatCoolSeconds(g);
    g.contractsFailed += 1;

    std::string msg = why + " " + contractText(c) + ".";
    if (c.stake > 0) msg += " Lost your " + num(c.stake) + "-shard stake.";
    msg += " Heat +" + std::to_string(heatGain) + " (now " + std::to_string(g.heat) + ").";
    bool forgiving = CLIENTS[c.client].style == ClientStyle::SmallFry;
    msg += shiftStanding(g, c.client, forgiving ? -1 : c.big ? -3 : -2);
    return {Sev::Bad, msg};
}

// An endurance job whose rule was broken.
bool broken(const GameState& g, const Contract& c) {
    switch (c.kind) {
        case ContractKind::Hold:     return g.shards < c.target;
        case ContractKind::HandsOff: return g.manualMines != c.start;
        default:                     return false;
    }
}

}  // namespace

bool isEndurance(const Contract& c) { return c.kind != ContractKind::Count && CONTRACT_KINDS[(int)c.kind].endurance; }

long long contractProgress(const GameState& g, const Contract& c) {
    if (isEndurance(c)) return std::max(0, c.duration - c.secondsLeft);   // time survived
    if (c.kind == ContractKind::Streak) return g.contractPeakStreak;      // a peak, not a delta
    if (c.kind == ContractKind::CoolOff) return std::max(0LL, c.start - g.heat);
    return std::max(0LL, contractStat(g, c.kind) - c.start);
}

long long contractGoal(const Contract& c) { return isEndurance(c) ? c.duration : c.target; }

std::string contractText(const Contract& c) {
    if (c.kind == ContractKind::Count) return "an unknown job";
    if (c.target == 1) return CONTRACT_KINDS[(int)c.kind].single;
    std::string fmt = CONTRACT_KINDS[(int)c.kind].format;
    size_t at = fmt.find("%s");
    // Big targets read better short: "Earn 32M tokens" rather than "Earn 32,000,000 tokens".
    if (at != std::string::npos) fmt.replace(at, 2, c.target >= 1'000'000 ? compact(c.target) : num(c.target));
    return fmt;
}

std::string progressText(const GameState& g, const Contract& c) {
    long long p = std::min(contractProgress(g, c), contractGoal(c));
    if (isEndurance(c)) return duration(p) + " of " + duration(c.duration);
    return (c.target >= 1'000'000 ? compact(p) : num(p)) + "/" + (c.target >= 1'000'000 ? compact(c.target) : num(c.target));
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
        bool clash = (running.kind == ContractKind::HandsOff && mining(c.kind)) || (c.kind == ContractKind::HandsOff && mining(running.kind));
        if (clash) return {{Sev::Warn, "One of those jobs wants you mining and the other wants you not to. Pick something else."}};
    }
    if (g.shards < c.stake) {
        return {{Sev::Warn, "This job needs a stake of " + count(c.stake, "shard") + ". You have " + num(g.shards) +
                            ". (Buy some on the market.)"}};
    }
    if (c.kind == ContractKind::Hold && g.shards - c.stake < c.target) {
        return {{Sev::Warn, "You'd need " + count(c.target + c.stake, "shard") + " on hand to take this (the stake comes out first). You have " +
                            num(g.shards) + "."}};
    }

    g.shards -= c.stake;
    if (c.kind == ContractKind::Streak) g.contractStreak = g.contractPeakStreak = 0;   // a fresh streak, not the one you're on
    c.start = contractStat(g, c.kind);
    c.secondsLeft = c.duration;
    g.contracts.push_back(c);
    g.offers.erase(g.offers.begin() + idx);
    if (g.offerClock <= 0) g.offerClock = OFFER_EVERY;

    std::string msg = "Accepted from " + std::string(CLIENTS[c.client].name) + ": " + contractText(c) +
                      " within " + duration(c.duration) + ".";
    if (isEndurance(c)) msg = "Accepted from " + std::string(CLIENTS[c.client].name) + ": " + contractText(c) + " for " +
                              duration(c.duration) + ". It fails the moment you break the rule (" + CONTRACT_KINDS[(int)c.kind].rule + ").";
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
        const Contract& c = g.contracts[i];
        if (isEndurance(c)) {
            if (broken(g, c)) {
                std::string why = std::string("CONTRACT FAILED (") + CONTRACT_KINDS[(int)c.kind].rule + "):";
                log.push(fail(g, i, why));
                continue;
            }
            if (c.secondsLeft > 0) continue;   // still holding out
        } else if (contractProgress(g, c) < c.target) {
            continue;
        }
        Contract done = c;
        g.contracts.erase(g.contracts.begin() + (std::ptrdiff_t)i);
        pay(g, log, done);
    }
}

void pay(GameState& g, MessageLog& log, const Contract& c) {
    earn(g, c.rewardTokens, Source::Contracts);
    g.rateSkip += c.rewardTokens;   // a payout isn't a sign you got faster
    g.shards += c.rewardShards + c.stake;
    g.contractsDone += 1;

    if (c.big && g.heat >= 8) spotSecret(g, Secret::GlassCannon);
    std::string msg = "CONTRACT DONE for " + std::string(CLIENTS[c.client].name) + ": " + contractText(c) + ". +" +
                      num(c.rewardTokens) + " tokens, +" + count(c.rewardShards, "shard");
    if (c.stake > 0) msg += ", stake returned";
    if (CLIENTS[c.client].style == ClientStyle::Shady) {   // their money is never clean
        g.heat = std::min(MAX_HEAT, g.heat + 1);
        g.heatClock = heatCoolSeconds(g);
        msg += ". Heat +1 (their money is never clean)";
    } else if (c.big && g.heat > 0) {   // pulling off a big job buys you some goodwill
        g.heat -= 1;
        msg += ". Heat -1";
    }
    msg += ".";
    msg += shiftStanding(g, c.client, c.big || c.signature ? 2 : 1);
    msg += shiftStanding(g, CLIENTS[c.client].rival, -1);
    log.push(Sev::Good, msg);
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
            log.push(Sev::Warn, std::to_string(CONTRACT_WARN_AT) + "s left: " + contractText(c) + " (" + progressText(g, c) +
                                (isEndurance(c) ? "; hold on" : "") + ").");
        }
        if (c.secondsLeft <= 0 && !isEndurance(c)) log.push(fail(g, i, "CONTRACT FAILED:"));
    }
    checkContract(g, log);   // endurance jobs whose time just ran out are won

    // The offer board
    for (Contract& o : g.offers) --o.expiresIn;
    std::erase_if(g.offers, [](const Contract& o) { return o.expiresIn <= 0; });

    if (g.offers.empty()) {
        fillOfferBoard(g, rng);
    } else if ((int)g.offers.size() < OFFER_COUNT && --g.offerClock <= 0) {
        g.offers.push_back(makeOffer(g, rng));
        g.offerClock = OFFER_EVERY;
        if (isUnlocked(g, Feature::Contracts)) {
            const Contract& o = g.offers.back();
            log.push(Sev::Info, std::string(o.signature ? "A SIGNATURE job from " : "A new contract offer from ") +
                                CLIENTS[o.client].name + " is on the board. (contracts)");
        }
    }
}
