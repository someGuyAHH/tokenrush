#include "choices.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "contracts.hpp"
#include "crew.hpp"
#include "game.hpp"
#include "market.hpp"
#include "util.hpp"

namespace {

// 1234567 -> 1200000: offers read better in round numbers.
long long roundish(double v) {
    if (!(v >= 1.0)) return 1;
    double mag = std::pow(10.0, std::floor(std::log10(v)) - 1.0);
    return std::max(1LL, toTokens(std::round(v / mag) * mag));
}

double income(const GameState& g) { return std::max(g.incomeRate, incomeEstimate(g)); }

long long shadyShards(const GameState& g) {
    return roundish(std::clamp(income(g) * 120.0 / std::max(1.0, g.shardPrice), 50.0, (double)MARKET_MAX_ORDER));
}
long long shadyCost(const GameState& g, long long n) { return toTokens(std::ceil((double)n * buyPrice(g) * 0.6)); }
long long bribeCost(const GameState& g) { return roundish(std::max(1000.0, income(g) * 180.0)); }

// The Dealer's curios you don't have yet and could pay for right now.
std::vector<int> dealerStock(const GameState& g) {
    std::vector<int> out;
    for (int i = 0; i < CURIO_COUNT; ++i)
        if (CURIOS[i].set == DEALER_SET && !hasBit(g.curiosOwned, i) && g.tokens >= dealerPrice(i)) out.push_back(i);
    return out;
}

CrewMember* crewNamed(GameState& g, const std::string& name) {
    for (CrewMember& c : g.crew)
        if (c.name == name) return &c;
    return nullptr;
}

bool eligible(const GameState& g, ChoiceKind k) {
    switch (k) {
        case ChoiceKind::ShadyBroker:
            return isUnlocked(g, Feature::Market) && isUnlocked(g, Feature::Contracts) && g.heat < MAX_HEAT &&
                   g.tokens >= shadyCost(g, shadyShards(g));
        case ChoiceKind::Investor: return g.tokens >= 1000 && g.investment.amount == 0;
        case ChoiceKind::Rival:    return g.tokens >= 2000 && g.race.stake == 0;
        case ChoiceKind::Raise:
            return isUnlocked(g, Feature::Crew) &&
                   std::any_of(g.crew.begin(), g.crew.end(), [](const CrewMember& c) { return c.raises < MAX_RAISES && !isAway(c); });
        case ChoiceKind::Bribe:
            return isUnlocked(g, Feature::Contracts) && g.heat >= AUDIT_MIN_HEAT && g.tokens >= bribeCost(g);
        case ChoiceKind::Dealer:
            return isUnlocked(g, Feature::Curios) && !dealerStock(g).empty();
        case ChoiceKind::None: break;
    }
    return false;
}

void resetClock(GameState& g, std::mt19937& rng) {
    std::uniform_int_distribution<int> next(CHOICE_EVERY_MIN, CHOICE_EVERY_MAX);
    g.choiceClock = gadgetOn(g, Gadget::DebugDuck) ? next(rng) / 2 : next(rng);
}

void tickInvestment(GameState& g, MessageLog& log) {
    Investment& inv = g.investment;
    if (inv.amount <= 0 || --inv.secondsLeft > 0) return;
    if (inv.scam) {
        log.push(Sev::Bad, "The Investor never came back. " + num(inv.amount) + " tokens, gone.");
    } else {
        g.tokens = addTokens(g.tokens, inv.amount);         // your own money back isn't income...
        earn(g, inv.payback - inv.amount, Source::Other);   // ...the profit is
        log.push(Sev::Good, "The Investor paid you back: " + num(inv.payback) + " tokens (+" + num(inv.payback - inv.amount) + ").");
    }
    inv = Investment{};
}

}  // namespace

long long dealerPrice(int curio) { return toTokens((double)CURIOS[curio].cost * DEALER_MARKUP); }

bool hasChoice(const GameState& g) { return g.choice.kind != ChoiceKind::None; }
int choiceSeconds(const GameState& g) { return gadgetOn(g, Gadget::DebugDuck) ? 20 : CHOICE_SECONDS; }

bool offerChoice(GameState& g, ChoiceKind kind, std::mt19937& rng) {
    if (hasChoice(g) || !eligible(g, kind)) return false;
    Choice c;
    c.kind = kind;
    c.secondsLeft = choiceSeconds(g);
    switch (kind) {
        case ChoiceKind::ShadyBroker: c.shards = shadyShards(g); c.tokens = shadyCost(g, c.shards); break;
        case ChoiceKind::Investor:    c.tokens = g.tokens / 10; break;
        case ChoiceKind::Rival:       c.tokens = g.tokens / 20; break;
        case ChoiceKind::Bribe:       c.tokens = bribeCost(g); break;
        case ChoiceKind::Raise: {
            std::vector<const CrewMember*> askers;
            for (const CrewMember& m : g.crew) if (m.raises < MAX_RAISES && !isAway(m)) askers.push_back(&m);
            std::uniform_int_distribution<size_t> pick(0, askers.size() - 1);
            c.crewName = askers[pick(rng)]->name;
            break;
        }
        case ChoiceKind::Dealer: {
            std::vector<int> stock = dealerStock(g);
            c.curio = stock[std::uniform_int_distribution<size_t>(0, stock.size() - 1)(rng)];
            c.tokens = dealerPrice(c.curio);
            break;
        }
        case ChoiceKind::None: return false;
    }
    g.choice = c;
    return true;
}

std::string choiceText(const GameState& g) {
    const Choice& c = g.choice;
    switch (c.kind) {
        case ChoiceKind::ShadyBroker:
            return "A shady broker offers " + num(c.shards) + " shards for " + num(c.tokens) +
                   " tokens (60% of the price). Cash only, no questions. Heat +1.";
        case ChoiceKind::Investor:
            return "An investor wants to borrow " + num(c.tokens) + " tokens. They promise " +
                   num(toTokens((double)c.tokens * 1.3)) + " back in 10 minutes... probably.";
        case ChoiceKind::Rival:
            return "A rival miner bets " + num(c.tokens) + " tokens you can't mine " + std::to_string(RACE_MINES) +
                   " times in " + std::to_string(RACE_SECONDS) + " seconds. Win and you double it.";
        case ChoiceKind::Raise:
            return c.crewName + " wants a raise: +1 shard per payday, and they'll work 25% harder. Say no and they'll sulk for a while.";
        case ChoiceKind::Bribe:
            return "An auditor hints that " + num(c.tokens) + " tokens would make 2 heat disappear.";
        case ChoiceKind::Dealer:
            return "A dealer in a long coat opens it up: " + std::string(CURIOS[c.curio].name) + ", sold in no shop. " +
                   num(c.tokens) + " tokens, half again what it's worth. Now or never.";
        case ChoiceKind::None: break;
    }
    return "";
}

Notes answerChoice(GameState& g, bool yes, std::mt19937& rng) {
    Choice c = g.choice;
    if (c.kind == ChoiceKind::None) return {{Sev::Info, "Nobody is asking you anything right now."}};
    g.choice = Choice{};
    resetClock(g, rng);

    g.refusedInRow = yes ? 0 : g.refusedInRow + 1;
    if (!yes) {
        if (c.kind == ChoiceKind::Raise) {
            if (CrewMember* m = crewNamed(g, c.crewName)) {
                m->sulk = 600;
                return {{Sev::Warn, c.crewName + " didn't take it well. Half effort for the next 10 minutes."}};
            }
        }
        return {{Sev::Info, "You said no."}};
    }

    auto cantAfford = [&] { return Notes{{Sev::Warn, "You can't afford that anymore. The offer's gone."}}; };
    switch (c.kind) {
        case ChoiceKind::ShadyBroker:
            if (g.tokens < c.tokens) return cantAfford();
            g.tokens -= c.tokens;
            g.shards += c.shards;
            g.shardsTraded += c.shards;
            g.shardsBought += c.shards;
            g.tokensOnBuys = addTokens(g.tokensOnBuys, c.tokens);
            g.heat = std::min(MAX_HEAT, g.heat + 1);
            return {{Sev::Good, "Deal. +" + num(c.shards) + " shards. Heat is now " + std::to_string(g.heat) + "."}};
        case ChoiceKind::Investor: {
            if (g.tokens < c.tokens) return cantAfford();
            std::uniform_real_distribution<double> uni(0.0, 1.0);
            g.tokens -= c.tokens;
            g.investment = {c.tokens, toTokens((double)c.tokens * 1.3), INVEST_SECONDS, uni(rng) < 0.2};
            return {{Sev::Info, "You lent " + num(c.tokens) + " tokens. Check back in 10 minutes."}};
        }
        case ChoiceKind::Rival:
            if (g.tokens < c.tokens) return cantAfford();
            g.tokens -= c.tokens;
            g.race = {c.tokens, g.manualMines, RACE_MINES, RACE_SECONDS};
            return {{Sev::Event, "GO! Mine " + std::to_string(RACE_MINES) + " times in " + std::to_string(RACE_SECONDS) + " seconds!"}};
        case ChoiceKind::Raise:
            if (CrewMember* m = crewNamed(g, c.crewName)) {
                m->raises += 1;
                return {{Sev::Good, c.crewName + " beams. Wage +1, and they'll work harder."}};
            }
            return {{Sev::Info, c.crewName + " isn't on your crew anymore."}};
        case ChoiceKind::Bribe:
            if (g.tokens < c.tokens) return cantAfford();
            g.tokens -= c.tokens;
            g.heat = std::max(0, g.heat - 2);
            return {{Sev::Good, "The auditor pockets it and looks the other way. Heat is now " + std::to_string(g.heat) + "."}};
        case ChoiceKind::Dealer: {
            if (hasBit(g.curiosOwned, c.curio)) return {{Sev::Info, "You already have one. The dealer shrugs and vanishes."}};
            if (g.tokens < c.tokens) return cantAfford();
            g.tokens -= c.tokens;
            g.curiosOwned |= 1u << c.curio;
            Notes out = {{Sev::Good, std::string(CURIOS[c.curio].name) + ": yours. \"" + CURIOS[c.curio].flavor + "\""}};
            bool setDone = true;
            for (int i = 0; i < CURIO_COUNT; ++i)
                if (CURIOS[i].set == DEALER_SET && !hasBit(g.curiosOwned, i)) setDone = false;
            if (setDone) out.push_back({Sev::Event, std::string("*** Set complete: ") + CURIO_SETS[DEALER_SET] + " ***"});
            return out;
        }
        case ChoiceKind::None: break;
    }
    return {};
}

void checkRace(GameState& g, MessageLog& log) {
    Race& r = g.race;
    if (r.stake <= 0 || g.manualMines - r.startMines < r.target) return;
    g.tokens = addTokens(g.tokens, r.stake);   // your stake back...
    earn(g, r.stake, Source::Other);           // ...and theirs
    log.push(Sev::Event, "You won the race with " + std::to_string(r.secondsLeft) + "s to spare! +" + num(r.stake) + " tokens.");
    r = Race{};
}

void choicesTick(GameState& g, MessageLog& log, std::mt19937& rng) {
    tickInvestment(g, log);

    if (g.race.stake > 0) {
        checkRace(g, log);
        if (g.race.stake > 0 && --g.race.secondsLeft <= 0) {
            log.push(Sev::Bad, "Time! " + std::to_string(g.manualMines - g.race.startMines) + "/" + std::to_string(g.race.target) +
                               ". The rival walks off with your " + num(g.race.stake) + " tokens.");
            g.race = Race{};
        }
    }

    if (hasChoice(g)) {
        if (--g.choice.secondsLeft <= 0) {
            log.push(Sev::Info, g.choice.kind == ChoiceKind::Raise ? g.choice.crewName + " gave up waiting for an answer."
                                                                    : "The offer expired.");
            g.choice = Choice{};
            resetClock(g, rng);
        }
        return;
    }

    if (g.choiceClock <= 0) {   // first tick of the session: the first offer comes a little sooner
#ifdef TOKENRUSH_TEST_OFFERS     // test builds only (never in build.py): the first offer after 3 seconds
        std::uniform_int_distribution<int> first(3, 3);
#else
        std::uniform_int_distribution<int> first(CHOICE_EVERY_MIN / 2, CHOICE_EVERY_MAX);
#endif
        g.choiceClock = first(rng);
        return;
    }
    if (--g.choiceClock > 0) return;

    std::vector<ChoiceKind> pool;
    for (ChoiceKind k : {ChoiceKind::ShadyBroker, ChoiceKind::Investor, ChoiceKind::Rival, ChoiceKind::Raise, ChoiceKind::Bribe,
                         ChoiceKind::Dealer})
        if (eligible(g, k)) pool.push_back(k);
    resetClock(g, rng);
    if (pool.empty()) return;
    std::uniform_int_distribution<size_t> pick(0, pool.size() - 1);
    if (offerChoice(g, pool[pick(rng)], rng))
        log.push(Sev::Event, "OFFER: " + choiceText(g) + " (yes/no, " + std::to_string(choiceSeconds(g)) + "s)");
}
