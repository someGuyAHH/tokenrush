#include "contracts.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include "actions.hpp"
#include "crew.hpp"
#include "game.hpp"
#include "market.hpp"
#include "stars.hpp"
#include "util.hpp"

// ----- Standing -----
const TierInfo TIERS[] = {
    {"Burned",   -10, 0.00, "won't post jobs for you (until a bribe or a make-amends job)"},
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

// A shard costs about the same at every stage, so job shards can't follow income one for one the
// way tokens do (that would flood the market). They grow with each doubling of income instead:
// x1 at 10K tokens/s, about x8 at 1M/s, x17 at 500M/s.
double contractShardScale(double power) { return 1.0 + std::max(0.0, std::log2(std::max(power, 1.0) / 1e4)); }

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

std::string capitalized(std::string s) {
    if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
    return s;
}

// A make-amends job from them on the board or running.
bool amendsOut(const GameState& g, int client) {
    for (const auto* list : {&g.offers, &g.contracts})
        for (const Contract& c : *list)
            if (c.amends && c.client == client) return true;
    return false;
}

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

    Contract c;
    c.kind = ContractKind::Count;   // nothing picked yet

    // Now and then someone you burned offers to bury the hatchet: a make-amends job.
    std::vector<int> sore;
    for (int i = 0; i < CLIENT_COUNT; ++i)
        if (clientTier(g, i) == Tier::Burned && g.playSeconds >= g.amendsAfter[(size_t)i] && !amendsOut(g, i)) sore.push_back(i);
    if (!sore.empty() && isUnlocked(g, Feature::Contracts) && uni(rng) < AMENDS_CHANCE) {
        int who = sore[std::uniform_int_distribution<size_t>(0, sore.size() - 1)(rng)];
        for (ContractKind k : kinds) {
            if (!offers(CLIENTS[who].style, k)) continue;
            c.kind = k;
            c.client = who;
            c.amends = true;
            break;
        }
    }

    // The first kind some client (one that still talks to you) wants, from one of them at random.
    for (ContractKind k : kinds) {
        if (c.amends) break;
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

    c.big = c.amends || (client.style != ClientStyle::SmallFry && uni(rng) < 0.35);   // amends: they want it done properly
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
    // ...but never past what mining by hand allows (MINES_PER_SECOND), with room left for luck and
    // breathing: a player already near the limit can't be stretched any further.
    minesPerSec = std::min(minesPerSec, MINE_JOB_MAX_RATE);
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
        {
            // What the Bot makes when it runs (not right now: a broken Bot or pulled breakers would
            // make this a 50-token job), or what it really made lately, whichever is more.
            double rate = std::max(botRateIfRunning(g), recentSourceRates(g)[(size_t)Source::Bot]);
            c.target = niceRound(std::max(50.0, rate * c.duration * (c.big ? between(1.4, 1.7) : between(0.9, 1.0))));
        }
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
    shardsPay *= brokerBonus * contractShardScale(power);
    if (c.big) {
        c.rewardTokens = niceRound(value * 0.7 * tokensPay * pay);
        c.rewardShards = std::llround(between(4, 8) * shardsPay * pay);
        long long stakeCap = std::max(60LL, c.rewardShards / 2);   // up to 60, or half of what it pays
        c.stake = c.signature ? 0 : std::clamp<long long>(std::llround(value * 0.1 / SHARD_FAIR_PRICE), 3, stakeCap);
    } else {
        c.rewardTokens = niceRound(value * 0.35 * tokensPay * pay);
        c.rewardShards = std::max(1LL, std::llround(between(1, 3) * shardsPay * pay));
        c.stake = 0;
    }
    if (c.amends) c.rewardTokens = c.rewardShards = 0;   // (Burned pays nothing anyway; this skips niceRound's minimum of 1)
    // Forged Papers: the same job and the same pay, with a quarter as long again to do it. (Not for
    // endurance jobs, where more time would only mean lasting longer. Failing costs more: fail().)
    if (gadgetOn(g, Gadget::ForgedPapers) && !isEndurance(c)) c.duration = roundTo30(c.duration * 1.25);
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
    int caught = gadgetOn(g, Gadget::ForgedPapers) ? 2 : 1;   // forged papers make a failure look worse
    int heatGain = (c.big ? 2 : 1) * caught;
    g.heat = std::min(MAX_HEAT, g.heat + heatGain);
    g.heatClock = heatCoolSeconds(g);
    g.contractsFailed += 1;

    std::string msg = why + " " + contractText(c) + ".";
    if (c.stake > 0) msg += " Lost your " + num(c.stake) + "-shard stake.";
    msg += " Heat +" + std::to_string(heatGain) + " (now " + std::to_string(g.heat) + ").";
    bool forgiving = CLIENTS[c.client].style == ClientStyle::SmallFry;
    if (c.amends && clientTier(g, c.client) == Tier::Burned) {   // (bribed meanwhile: an ordinary failure)
        g.standing[(size_t)c.client] = STANDING_MIN;
        g.amendsAfter[(size_t)c.client] = g.playSeconds + AMENDS_COOLDOWN;
        msg += " " + capitalized(CLIENTS[c.client].name) + " won't offer to make up again for a while.";
    } else {
        msg += shiftStanding(g, c.client, (forgiving ? -1 : c.big ? -3 : -2) * caught);
    }
    if (caught > 1) msg += " (Forged papers: twice the trouble.)";
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

// ----- Tips -----
const char* const TIP_NAMES[] = {"Easy", "Doable", "Tight", "Unlikely", "Out of reach"};
static_assert(std::size(TIP_NAMES) == (int)TipLevel::Count);

namespace {

// How long it would take at your pace, against the time there is.
TipLevel byPace(double pace, double time) {
    double r = time > 0 ? pace / time : 99.0;
    if (r <= 0.5) return TipLevel::Easy;
    if (r <= 0.85) return TipLevel::Doable;
    if (r <= 1.1) return TipLevel::Tight;
    if (r <= 2.0) return TipLevel::Unlikely;
    return TipLevel::OutOfReach;
}

// Mining by hand: how many mines a second it asks of you. The MINE button takes four at most.
TipLevel byMineRate(double perSec) {
    if (perSec <= 0.35) return TipLevel::Easy;
    if (perSec <= 0.7) return TipLevel::Doable;
    if (perSec <= 1.2) return TipLevel::Tight;
    if (perSec <= 3.0) return TipLevel::Unlikely;
    return TipLevel::OutOfReach;
}

std::string every(double perSec) {   // "a mine every 2s", "2 mines a second"
    if (perSec >= 1.0) return fixed(perSec, 1) + " mines a second";
    return "a mine every " + fixed(1.0 / std::max(perSec, 0.001), perSec >= 0.1 ? 1 : 0) + "s";
}

std::string paceText(double pace, double time) {
    if (pace > 3600.0 * 24) return "at your pace it would take forever";
    return "at your pace about " + duration(std::llround(pace)) + " of the " + duration(std::llround(time)) + " you have";
}

}  // namespace

void judgeContract(const GameState& g, Contract& c, bool running) {
    double time = running ? c.secondsLeft : c.duration;
    long long left = running ? std::max(0LL, contractGoal(c) - contractProgress(g, c)) : contractGoal(c);
    double lately = g.mineRate;   // your manual mines a second, over the last couple of minutes
    std::string latelyText = lately >= 0.05 ? " (lately " + every(lately) + ")" : " (you haven't been mining lately)";
    TipLevel level = TipLevel::Doable;
    std::string why;

    switch (c.kind) {
        case ContractKind::Earn: {
            double rate = std::max({g.incomeRate, recentEarnRate(g), 0.001});
            level = byPace((double)left / rate, time);
            why = paceText((double)left / rate, time) + ".";
            break;
        }
        case ContractKind::Mine: {
            double need = (double)left / std::max(time, 1.0);
            level = byMineRate(need);
            why = "needs " + every(need) + latelyText + ".";
            break;
        }
        case ContractKind::Jackpots: {
            double mines = (double)left / std::max(jackpotChance(g), 0.001);
            double need = mines / std::max(time, 1.0);
            level = byMineRate(need);
            if (level < TipLevel::Tight) level = (TipLevel)((int)level + 1);   // luck decides, so never quite easy
            why = "about " + num(std::llround(mines)) + " mines at your odds (" + every(need) + "), and luck decides.";
            break;
        }
        case ContractKind::Streak: {
            double need = (double)left / std::max(time, 1.0);
            level = byMineRate(need * 0.6);   // steady mining is easier than fast mining
            why = "keep mining at least every " + std::to_string(streakTimeout(g)) + "s for " + num(left) + " more mines.";
            break;
        }
        case ContractKind::BotEarn: {
            double rate = botRatePerSec(g);
            if (rate <= 0.0) { level = TipLevel::OutOfReach; why = g.botBroken ? "the Mining Bot is broken: repair it first." : "the Mining Bot isn't running."; break; }
            level = byPace((double)left / rate, time);
            why = paceText((double)left / rate, time) + ". More Bot levels help.";
            break;
        }
        case ContractKind::Trade: {
            long long batch = std::min<long long>(std::max(1LL, left), 50);
            long long batchCost = buyCost(g, batch);
            long long spread = toTokens((double)left * g.shardPrice * marketSpread(g));
            if (!isUnlocked(g, Feature::Market)) { level = TipLevel::OutOfReach; why = "the Market isn't open yet."; break; }
            if (g.tokens + sellValue(g, g.shards) < batchCost && g.shards < batch) {
                level = TipLevel::OutOfReach;
                why = "you can't afford even a batch of " + num(batch) + " shards right now.";
                break;
            }
            level = spread * 4 < g.tokens ? TipLevel::Easy : spread < g.tokens ? TipLevel::Doable : TipLevel::Tight;
            why = "buy and sell back in batches of " + num(batch) + "; costs about " + num(spread) + " tokens in spread.";
            break;
        }
        case ContractKind::Hold: {
            long long have = running ? g.shards : g.shards - c.stake;
            long long spare = have - c.target;
            bool payday = !g.crew.empty() && g.paydayClock < time;
            if (spare < 0) {
                level = running ? TipLevel::OutOfReach : TipLevel::Unlikely;
                why = "you'd need " + num(-spare) + " more shards on hand" + (running ? "." : " to take it.");
            } else if (payday && spare < totalWages(g)) {
                level = TipLevel::Tight;
                why = "payday in " + duration(g.paydayClock) + " takes " + num(totalWages(g)) + " shards and would drop you below " +
                      num(c.target) + ": buy a few first.";
            } else {
                level = spare > totalWages(g) * 2 + 20 ? TipLevel::Easy : TipLevel::Doable;
                why = "just don't spend below " + num(c.target) + " shards (" + num(spare) + " to spare" +
                      (payday ? ", payday included" : "") + ").";
            }
            break;
        }
        case ContractKind::CoolOff: {
            if (infected(g, Strain::Fever)) {
                level = TipLevel::Unlikely;
                why = "Fever stops heat cooling by itself: only a bribe or a big job will do it.";
                break;
            }
            long long drop = running ? left : c.target;
            double cooling = drop <= 0 ? 0.0 : (double)g.heatClock + (double)(drop - 1) * heatCoolSeconds(g);
            level = byPace(cooling, time);
            why = "heat cools a point every " + duration(heatCoolSeconds(g)) + " by itself" +
                  (level >= TipLevel::Tight ? "; a bribe, or finishing a big job, speeds it up." : ".");
            break;
        }
        case ContractKind::HandsOff: {
            double passive = passiveRatePerSec(g) + botRatePerSec(g);
            level = passive > 0 ? TipLevel::Easy : TipLevel::Doable;
            why = "just don't mine by hand" + std::string(passive > 0 ? ": Drones and the Bot make about " + perSecond(passive) + "/s meanwhile."
                                                                       : ". You'll earn almost nothing meanwhile.");
            break;
        }
        case ContractKind::Count:
            break;
    }

    if (!running && g.shards < c.stake) {   // can't even take it yet
        level = std::max(level, TipLevel::Unlikely);
        why = "the stake is " + num(c.stake) + " shards and you have " + num(g.shards) + ". Otherwise: " + why;
    }
    if (running && c.secondsLeft <= 0) { level = TipLevel::OutOfReach; why = "time's up."; }
    c.tipLevel = (int)level;
    c.tip = why;
}

void judgeAll(GameState& g) {
    for (Contract& c : g.offers) judgeContract(g, c, false);
    for (Contract& c : g.contracts) judgeContract(g, c, true);
    g.tipClock = TIP_EVERY;
}

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
    double fog = gadgetOn(g, Gadget::FogMachine) ? 2.0 : 1.0;   // it keeps you hot
    return (int)std::lround(HEAT_COOL_SECONDS / (1.0 + 0.5 * roleLevels(g, Role::Fixer)) * fog);
}

int auditMinHeat(const GameState&) { return AUDIT_MIN_HEAT; }

double auditChance(const GameState& g) {
    if (g.heat < auditMinHeat(g)) return 0.0;
    return (double)(g.heat - auditMinHeat(g) + 1) / 1500.0;
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
    judgeContract(g, c, true);
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
    while ((int)g.offers.size() < OFFER_COUNT) {
        g.offers.push_back(makeOffer(g, rng));
        judgeContract(g, g.offers.back(), false);
    }
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
    // The Fog Machine: running hot pays, 10% more for every point of heat when you finish.
    double fog = gadgetOn(g, Gadget::FogMachine) ? 1.0 + 0.10 * g.heat : 1.0;
    long long tokensPaid = toTokens((double)c.rewardTokens * fog);
    long long shardsPaid = std::llround((double)c.rewardShards * fog);
    earn(g, tokensPaid, Source::Contracts);
    g.rateSkip += tokensPaid;   // a payout isn't a sign you got faster
    g.shards += shardsPaid + c.stake;
    g.contractsDone += 1;

    if (c.big && g.heat >= 8) spotSecret(g, Secret::GlassCannon);
    if (g.playSeconds - g.lastContractAt <= 10) spotStar(g, Constellation::Twins);
    g.lastContractAt = g.playSeconds;
    if (c.signature) g.signatureClients |= 1u << c.client;
    if (c.amends) {   // no pay: what you get back is the client
        g.standing[(size_t)c.client] = std::max(g.standing[(size_t)c.client], AMENDS_STANDING);
        log.push(Sev::Good, "AMENDS MADE with " + std::string(CLIENTS[c.client].name) + ": " + contractText(c) +
                                (c.stake > 0 ? ". Stake returned. " : ". ") + capitalized(CLIENTS[c.client].name) +
                                " is Wary of you, but will post jobs for you again.");
        return;
    }
    std::string msg = "CONTRACT DONE for " + std::string(CLIENTS[c.client].name) + ": " + contractText(c) + ". +" +
                      num(tokensPaid) + " tokens, +" + count(shardsPaid, "shard");
    if (fog > 1.0) msg += " (+" + std::to_string(10 * g.heat) + "% from the Fog Machine)";
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
    // Their rival takes it badly, but only so far: a rival's grudge makes them Wary at worst. Burning
    // a client takes failing them yourself.
    int rival = CLIENTS[c.client].rival;
    if (g.standing[(size_t)rival] > TIERS[(int)Tier::Wary].from) {
        std::string moved = shiftStanding(g, rival, -1);
        if (!moved.empty()) msg += moved.substr(0, moved.size() - 1) + " (" + CLIENTS[c.client].name + "'s rival).";
    }
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
        log.push(Sev::Info, "Things are quieter. Heat cooled to " + std::to_string(g.heat) + ".", LogKind::Heat);
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
    if (--g.tipClock <= 0) judgeAll(g);   // the tips follow your pace, a few seconds at a time
    for (Contract& o : g.offers) --o.expiresIn;
    std::erase_if(g.offers, [](const Contract& o) { return o.expiresIn <= 0; });

    if (g.offers.empty()) {
        fillOfferBoard(g, rng);
    } else if ((int)g.offers.size() < OFFER_COUNT && --g.offerClock <= 0) {
        g.offers.push_back(makeOffer(g, rng));
        judgeContract(g, g.offers.back(), false);
        g.offerClock = OFFER_EVERY;
        if (isUnlocked(g, Feature::Contracts)) {
            const Contract& o = g.offers.back();
            if (o.amends)
                log.push(Sev::Event, "A MAKE-AMENDS job from " + std::string(CLIENTS[o.client].name) +
                                         " is on the board: no pay, but deliver it and they'll work with you again. (contracts)", LogKind::Offers);
            else
                log.push(Sev::Info, std::string(o.signature ? "A SIGNATURE job from " : "A new contract offer from ") +
                                    CLIENTS[o.client].name + " is on the board. (contracts)", LogKind::Offers);
        }
    }
}

// ----- Coming back from Burned -----
long long bribeCost(const GameState& g, int client) {
    if (client < 0 || client >= CLIENT_COUNT || clientTier(g, client) != Tier::Burned) return 0;
    int st = g.standing[(size_t)client];
    double power = std::max(incomeEstimate(g), g.incomeRate);
    double depth = 1.0 + 0.2 * (TIERS[(int)Tier::Wary].from - 1 - st);   // x1 just Burned (-5), x2 at -10
    double again = std::pow(2.0, std::min(g.bribes[(size_t)client], BRIBE_DOUBLINGS_MAX));
    return niceRound(BRIBE_BASE * contractShardScale(power) * depth * again);
}

Notes bribeClient(GameState& g, int client) {
    if (client < 0 || client >= CLIENT_COUNT) return {{Sev::Warn, "No such client. 'clients' lists them."}};
    std::string name = CLIENTS[client].name;
    if (clientTier(g, client) != Tier::Burned)
        return {{Sev::Info, capitalized(name) + " still works with you: no bribe needed."}};
    long long cost = bribeCost(g, client);
    if (g.shards < cost) return {{Sev::Warn, "Bribing " + name + " takes " + count(cost, "shard") + ". You have " + num(g.shards) + "."}};
    g.shards -= cost;
    g.bribes[(size_t)client] += 1;
    g.standing[(size_t)client] = AMENDS_STANDING;
    std::erase_if(g.offers, [client](const Contract& o) { return o.amends && o.client == client; });   // nothing left to make up for
    return {{Sev::Good, "Paid " + name + " " + count(cost, "shard") + " to forget about it. " + capitalized(name) +
                            " is Wary of you, but will post jobs for you again. (Another bribe to them would cost twice as much.)"}};
}

bool clientPosts(int client, ContractKind k) { return offers(CLIENTS[client].style, k); }

int findClient(const std::string& word) {
    std::string w = lower(trim(word));
    if (w.empty()) return -1;
    if (long long n = 0; parseLongLong(w, n)) return n >= 1 && n <= CLIENT_COUNT ? (int)n - 1 : -1;
    for (int i = 0; i < CLIENT_COUNT; ++i)   // the id, or the name with or without "the"/"a"
        if (w == CLIENTS[i].id || w == lower(CLIENTS[i].name)) return i;
    for (int i = 0; i < CLIENT_COUNT; ++i)
        if (lower(CLIENTS[i].name).find(w) != std::string::npos) return i;
    return -1;
}
