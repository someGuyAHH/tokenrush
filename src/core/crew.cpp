#include "crew.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "game.hpp"
#include "util.hpp"

namespace {

std::string who(const CrewMember& c) { return c.name + " (" + ROLES[(int)c.role].name + ")"; }

bool nameTaken(const GameState& g, const std::string& name) {
    auto same = [&](const CrewMember& c) { return c.name == name; };
    return std::any_of(g.crew.begin(), g.crew.end(), same) ||
           std::any_of(g.candidates.begin(), g.candidates.end(), same);
}

CrewMember randomCandidate(const GameState& g, std::mt19937& rng) {
    std::uniform_int_distribution<int> pickName(0, CREW_NAME_COUNT - 1);
    std::uniform_int_distribution<int> pickRole(0, ROLE_COUNT - 1);
    std::uniform_real_distribution<double> uni(0.0, 1.0);

    CrewMember c;
    for (int tries = 0; tries < 100; ++tries) {   // 32 names, at most 7 in use: this ends fast
        c.name = CREW_NAMES[pickName(rng)];
        if (!nameTaken(g, c.name)) break;
    }
    c.role = (Role)pickRole(rng);
    c.level = uni(rng) < 0.2 ? 2 : 1;
    if (uni(rng) >= 0.35) {   // about two in three have a trait
        std::uniform_int_distribution<int> pickTrait(1, TRAIT_COUNT - 1);
        c.trait = (Trait)pickTrait(rng);
    }
    return c;
}

void refreshCandidates(GameState& g, std::mt19937& rng) {
    g.candidates.clear();
    for (int i = 0; i < CANDIDATE_COUNT; ++i) g.candidates.push_back(randomCandidate(g, rng));
    g.candidateClock = CANDIDATE_REFRESH;
}

void payday(GameState& g, MessageLog& log) {
    long long paid = 0;
    std::vector<CrewMember> stayed;
    for (CrewMember c : g.crew) {
        long long w = wageOf(g, c);
        if (g.shards >= w) {
            g.shards -= w;
            paid += w;
            c.missedPay = false;
            stayed.push_back(c);
        } else if (c.trait == Trait::Loyal && !c.missedPay) {
            c.missedPay = true;
            log.push(Sev::Warn, who(c) + " wasn't paid, but is staying out of loyalty. Next time they walk.");
            stayed.push_back(c);
        } else {
            log.push(Sev::Bad, who(c) + " wasn't paid and quit.");
        }
    }
    g.crew = std::move(stayed);
    if (paid > 0) log.push(Sev::Info, "Payday: your crew took " + count(paid, "shard") + ".");
}

}  // namespace

void fillCandidates(GameState& g, std::mt19937& rng) {
    if (g.candidates.empty()) refreshCandidates(g, rng);
}

double effectiveLevel(const GameState& g, const CrewMember& c) {
    double e = (double)c.level * (1.0 + PROMO_EFFECT * c.stars) * (1.0 + RAISE_EFFECT * c.raises);
    switch (c.trait) {
        case Trait::Diligent: e *= 1.25; break;
        case Trait::Greedy:   e *= 1.50; break;
        case Trait::Frugal:   e *= 0.85; break;
        case Trait::NightOwl:
            // g.lastUpdate is "now" to within a second, without needing the clock passed in
            if (g.lastMine == 0 || std::difftime(g.lastUpdate, g.lastMine) >= NIGHT_OWL_IDLE) e *= 2.0;
            break;
        default: break;
    }
    if (c.sulk > 0) e *= 0.5;
    return e;
}

double roleLevels(const GameState& g, Role r) {
    double total = 0.0;
    for (const CrewMember& c : g.crew)
        if (c.role == r) total += effectiveLevel(g, c);
    return total;
}

int levelCap(const GameState& g, const CrewMember& c) { return crewLevelCap(g) + PROMO_LEVELS * c.stars; }

long long hireCost(const GameState& g, const CrewMember& c) {
    double scale = 1.0 + 0.5 * (double)g.crew.size();   // each extra hire is pricier
    return std::llround((double)ROLES[(int)c.role].hireCost * c.level * scale);
}

long long trainCost(const CrewMember& c) {
    long long cost = 6LL * c.level * c.level;
    return c.trait == Trait::QuickLearner ? std::max(1LL, cost / 2) : cost;
}

long long promoteCost(const CrewMember& c) {
    double cost = PROMO_BASE * std::pow(PROMO_GROWTH, (double)c.stars);
    if (c.trait == Trait::Ambitious) cost *= 0.7;
    return std::min(toTokens(std::round(cost)), TOKEN_CAP);
}

long long wageOf(const GameState& g, const CrewMember& c) {
    long long w = c.level;
    if (c.trait == Trait::Greedy) w *= 2;
    if (c.trait == Trait::Frugal) w = std::max(1LL, (w + 1) / 2);
    if (infected(g, Strain::Mutiny)) w *= 2;
    return w + c.raises;
}

long long totalWages(const GameState& g) {
    long long total = 0;
    for (const CrewMember& c : g.crew) total += wageOf(g, c);
    return total;
}

double scoutChance(const GameState& g, const CrewMember& c) { return std::min(0.95, 0.25 + 0.10 * effectiveLevel(g, c)); }
int scoutMaxFind(const GameState& g, const CrewMember& c) { return std::max(1, (int)std::lround(effectiveLevel(g, c))); }

long long minerSwing(const GameState& g, const CrewMember& c) {
    return std::max(1LL, toTokens(expectedMine(g) * (0.15 + 0.10 * effectiveLevel(g, c))));
}

std::string traitName(const CrewMember& c) { return TRAITS[(int)c.trait].name; }

std::string starsText(const CrewMember& c) {
    if (c.stars <= 5) return std::string((size_t)c.stars, '*');
    return "*" + std::to_string(c.stars);   // "*12" rather than a wall of stars
}

std::string crewEffectText(const GameState& g, const CrewMember& c) {
    double e = effectiveLevel(g, c);
    std::string s;
    switch (c.role) {
        case Role::Miner:  s = "+" + compact(minerSwing(g, c)) + " tokens every " + std::to_string(MINER_EVERY) + "s"; break;
        case Role::Broker: s = "spread -" + percent(0.006 * e) + ", impact -" + percent(std::min(0.5, 0.08 * e), 0) +
                               ", contract shards +" + percent(std::min(2.0, 0.10 * e), 0); break;
        case Role::Scout:  s = percent(scoutChance(g, c), 0) + "/min to find " +
                               (scoutMaxFind(g, c) == 1 ? std::string("a shard") : "1-" + std::to_string(scoutMaxFind(g, c)) + " shards"); break;
        case Role::Fixer:  s = "heat cools faster, blocks " + percent(std::min(0.75, 0.15 * e), 0) + " of audits"; break;
        case Role::Count:  break;
    }
    if (c.sulk > 0) s += " (sulking, half effort)";
    return s;
}

Notes hire(GameState& g, int idx) {
    if (!isUnlocked(g, Feature::Crew)) return {{Sev::Warn, lockedText(g, Feature::Crew)}};
    if (idx < 0 || idx >= (int)g.candidates.size()) return {{Sev::Warn, "No such candidate."}};
    if ((int)g.crew.size() >= crewCap(g)) return {{Sev::Warn, "Your crew is full (" + std::to_string(crewCap(g)) + "). Fire someone first."}};

    CrewMember c = g.candidates[(size_t)idx];
    long long cost = hireCost(g, c);
    if (g.shards < cost) return {{Sev::Warn, "Hiring " + c.name + " costs " + num(cost) + " shards. You have " + num(g.shards) + "."}};

    g.shards -= cost;
    if (g.crew.empty()) g.paydayClock = PAYDAY_SECONDS;   // a fresh crew gets a full pay period
    g.crew.push_back(c);
    g.candidates.erase(g.candidates.begin() + idx);
    g.crewHired += 1;
    return {{Sev::Good, who(c) + " joined your crew."}};
}

Notes fire(GameState& g, int idx) {
    if (idx < 0 || idx >= (int)g.crew.size()) return {{Sev::Warn, "No such crew member."}};
    CrewMember c = g.crew[(size_t)idx];
    g.crew.erase(g.crew.begin() + idx);
    return {{Sev::Info, who(c) + " packed up and left."}};
}

Notes train(GameState& g, int idx) {
    if (!isUnlocked(g, Feature::Crew)) return {{Sev::Warn, lockedText(g, Feature::Crew)}};
    if (idx < 0 || idx >= (int)g.crew.size()) return {{Sev::Warn, "No such crew member."}};
    CrewMember& c = g.crew[(size_t)idx];
    if (c.level >= levelCap(g, c)) {
        return {{Sev::Info, c.name + " is at their level cap (" + std::to_string(levelCap(g, c)) + "). Promote them to go further" +
                            std::string(crewLevelCap(g) < CREW_LEVEL_LIMIT ? ", or finish the Orrery's Gearwork." : ".")}};
    }
    long long cost = trainCost(c);
    if (g.shards < cost) return {{Sev::Warn, "Training " + c.name + " costs " + num(cost) + " shards. You have " + num(g.shards) + "."}};

    g.shards -= cost;
    c.level += 1;
    return {{Sev::Good, who(c) + " is now level " + std::to_string(c.level) + ". (Wage is now " +
                        count(wageOf(g, c), "shard") + " per payday.)"}};
}

Notes promote(GameState& g, int idx) {
    if (!isUnlocked(g, Feature::Crew)) return {{Sev::Warn, lockedText(g, Feature::Crew)}};
    if (idx < 0 || idx >= (int)g.crew.size()) return {{Sev::Warn, "No such crew member."}};
    CrewMember& c = g.crew[(size_t)idx];
    if (c.level < levelCap(g, c)) {
        return {{Sev::Info, c.name + " can only be promoted at their level cap (" + std::to_string(levelCap(g, c)) +
                            "). Train them first."}};
    }
    long long cost = promoteCost(c);
    if (g.shards < cost) return {{Sev::Warn, "Promoting " + c.name + " costs " + num(cost) + " shards. You have " + num(g.shards) + "."}};

    g.shards -= cost;
    c.stars += 1;
    return {{Sev::Good, who(c) + " was promoted to " + std::string((size_t)c.stars, '*') + " (" + std::to_string(c.stars) +
                        (c.stars == 1 ? " star" : " stars") + "): level cap " + std::to_string(levelCap(g, c)) + ", every level worth +" +
                        std::to_string((int)std::lround(100 * PROMO_EFFECT * c.stars)) + "%. Next promotion: " +
                        num(promoteCost(c)) + " shards."}};
}

void crewTick(GameState& g, MessageLog& log, std::time_t t, std::mt19937& rng) {
    if (g.candidates.empty()) fillCandidates(g, rng);
    else if (--g.candidateClock <= 0) {
        refreshCandidates(g, rng);
        if (isUnlocked(g, Feature::Crew)) log.push(Sev::Info, "New faces are looking for work. (crew)");
    }

    if (g.crew.empty()) { g.paydayClock = PAYDAY_SECONDS; return; }

    ++g.crewClock;
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    EventFx fx = fxNow(g, t);

    for (CrewMember& c : g.crew)
        if (c.sulk > 0) --c.sulk;

    for (const CrewMember& c : g.crew) {
        switch (c.role) {
            case Role::Miner:
                if (g.crewClock % MINER_EVERY == 0) {
                    long long gain = toTokens((double)minerSwing(g, c) * fx.mining);
                    earn(g, gain, Source::Crew);
                    g.crewMined = addTokens(g.crewMined, gain);
                    g.crewMinedSinceReport = addTokens(g.crewMinedSinceReport, gain);
                }
                break;
            case Role::Scout:
                if (g.crewClock % SCOUT_EVERY == 0 && uni(rng) < scoutChance(g, c)) {
                    std::uniform_int_distribution<int> found(1, scoutMaxFind(g, c));
                    int n = found(rng);
                    g.shards += n;
                    log.push(Sev::Good, who(c) + " found " + count(n, "shard") + ".");
                }
                break;
            case Role::Broker:   // passive: see market.cpp
            case Role::Fixer:    // passive: see contracts.cpp
            case Role::Count:
                break;
        }
    }

    if (g.crewClock % CREW_REPORT_EVERY == 0 && g.crewMinedSinceReport > 0) {
        log.push(Sev::Info, "Your crew mined +" + num(g.crewMinedSinceReport) + " tokens in the last minute.");
        g.crewMinedSinceReport = 0;
    }

    if (--g.paydayClock <= 0) {
        payday(g, log);
        g.paydayClock = PAYDAY_SECONDS;
    }
}
