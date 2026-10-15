#include "crew.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
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

// Someone left: the Stats tab remembers them.
void remember(GameState& g, const CrewMember& c, const std::string& how) {
    g.formerCrew.push_back({c.name, c.role, c.level, c.stars, c.trait, how, g.playSeconds});
    if (g.formerCrew.size() > FORMER_CREW_MAX) g.formerCrew.erase(g.formerCrew.begin());
}

// A keepsake for the shelf, picked by who they are (the same person always leaves the same thing).
std::string leaveKeepsake(GameState& g, const CrewMember& c, const std::string& why) {
    static const char* const items[ROLE_COUNT][6] = {
        {"Lucky Wrench", "Dented Helmet", "Lamp That Never Dies", "Notched Pickaxe Handle", "Lump of Fool's Gold", "Coffee-Stained Map of the Mine"},
        {"Battered Ledger", "Pen That Only Writes Numbers", "Ticker-Tape Bracelet", "Lucky Calculator", "Very Firm Handshake (Framed)", "Rubber Stamp: SOLD"},
        {"Compass That Points at Shards", "Boots Full of Sand", "Pocket Telescope", "Map With One X on It", "Jar of Interesting Pebbles", "Well-Chewed Pencil"},
        {"Burner Phone", "Fake Mustache", "Envelope (Empty Now)", "Little Black Book", "Spare Set of Fingerprints", "Business Card: 'A Friend'"},
    };
    size_t pick = std::hash<std::string>{}(c.name + ROLES[(int)c.role].id) % 6;
    std::string item = c.name + "'s " + items[(int)c.role][pick];
    g.keepsakes.push_back({item, c.name, c.role, why, g.playSeconds});
    if (g.keepsakes.size() > KEEPSAKE_MAX) g.keepsakes.erase(g.keepsakes.begin());
    return item;
}

// Someone's back from an expedition: what did they find?
void comeBack(GameState& g, CrewMember& c, MessageLog& log, std::mt19937& rng) {
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    const ExpeditionInfo& e = EXPEDITIONS[std::clamp(c.trip, 0, EXPEDITION_COUNT - 1)];
    double luck = 0.6 + 0.8 * uni(rng);
    long long shards = std::llround((double)expeditionLoot(g, c, c.trip) * luck);
    std::string found;

    bool injured = uni(rng) < injuryChance(g, c.trip);
    if (injured) {
        shards /= 2;
        c.hurt = HURT_SECONDS;
    }
    if (uni(rng) < e.curioChance) {
        std::vector<int> missing;
        for (int i = 0; i < CURIO_COUNT; ++i)
            if (!inShop(i) && !hasBit(g.curiosOwned, i)) missing.push_back(i);
        if (missing.empty()) {
            shards += shards / 2;   // nothing left to find: they sold it on the way back
        } else {
            int cur = missing[std::uniform_int_distribution<size_t>(0, missing.size() - 1)(rng)];
            g.curiosOwned |= 1u << cur;
            found += ", found " + theCurio(cur) + " (on your shelf)";
        }
    }
    if (c.trait == Trait::None && uni(rng) < e.traitChance) {
        c.trait = (Trait)std::uniform_int_distribution<int>(1, TRAIT_COUNT - 1)(rng);
        found += ", came back changed: now " + std::string(TRAITS[(int)c.trait].name);
    }
    if (c.level < levelCap(g, c) && uni(rng) < e.levelChance) {
        c.level += 1;
        found += ", learned a thing or two (now level " + std::to_string(c.level) + ")";
    }
    g.shards += shards;
    g.expeditionsDone += 1;
    std::string msg = who(c) + " is back from " + e.name + ": +" + count(shards, "shard") + found + ".";
    if (injured) msg += " They got hurt out there: half effort for " + duration(HURT_SECONDS) + ".";
    log.push(injured ? Sev::Warn : Sev::Good, msg);
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
            spotSecret(g, Secret::LoyalToAFault);
            stayed.push_back(c);
        } else {
            remember(g, c, "quit (unpaid)");
            std::string item = leaveKeepsake(g, c, "quit when you couldn't pay");
            log.push(Sev::Bad, who(c) + " wasn't paid and quit. They left something behind: " + item + " (on your shelf).");
        }
    }
    g.crew = std::move(stayed);
    if (paid > 0) log.push(Sev::Info, "Payday: your crew took " + count(paid, "shard") + ".");
}

}  // namespace

void fillCandidates(GameState& g, std::mt19937& rng) {
    if (g.candidates.empty()) refreshCandidates(g, rng);
}

// ----- Expeditions -----
const ExpeditionInfo EXPEDITIONS[EXPEDITION_COUNT] = {
    {"short", "a short expedition", 600,  1.0, 0.02, 0.05, 0.00, 0.05},
    {"long",  "a long expedition",  1800, 3.5, 0.08, 0.15, 0.10, 0.12},
    {"deep",  "a deep expedition",  3600, 8.0, 0.20, 0.25, 0.30, 0.25},
};

bool isAway(const CrewMember& c) { return c.away > 0; }

int expeditionByName(const std::string& word) {
    std::string w = lower(trim(word));
    for (int i = 0; i < EXPEDITION_COUNT; ++i)
        if (w == EXPEDITIONS[i].id || w == std::to_string(i + 1)) return i;
    return -1;
}

long long expeditionLoot(const GameState& g, const CrewMember& c, int trip) {
    CrewMember home = c;
    home.away = 0;   // how good they are when they're working
    double scout = c.role == Role::Scout ? 1.5 : 1.0;   // finding things is their job
    return std::max(1LL, std::llround(EXPEDITIONS[trip].loot * (8.0 + 4.0 * effectiveLevel(g, home)) * scout));
}

double injuryChance(const GameState& g, int trip) {
    return EXPEDITIONS[trip].injuryChance * (1.0 - std::min(0.6, 0.06 * roleLevels(g, Role::Fixer)));
}

Notes sendExpedition(GameState& g, int idx, int trip) {
    if (!isUnlocked(g, Feature::Crew)) return {{Sev::Warn, lockedText(g, Feature::Crew)}};
    if (idx < 0 || idx >= (int)g.crew.size()) return {{Sev::Warn, "No such crew member."}};
    if (trip < 0 || trip >= EXPEDITION_COUNT) return {{Sev::Warn, "Short, long or deep?"}};
    CrewMember& c = g.crew[(size_t)idx];
    if (isAway(c)) return {{Sev::Info, c.name + " is already away (back in " + duration(c.away) + ")."}};
    c.away = EXPEDITIONS[trip].seconds;
    c.trip = trip;
    return {{Sev::Good, who(c) + " set off on " + EXPEDITIONS[trip].name + ". Back in " + duration(c.away) +
                        " (only while the game is open). Their wages still come out on payday."}};
}

double effectiveLevel(const GameState& g, const CrewMember& c) {
    if (isAway(c)) return 0.0;   // off exploring
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
    if (c.hurt > 0) e *= 0.5;
    return e * fxNow(g, g.lastUpdate).crew;   // a Payday Rush doubles everyone
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
    if (isAway(c)) return std::string("away on ") + EXPEDITIONS[std::clamp(c.trip, 0, EXPEDITION_COUNT - 1)].name + ", back in " + duration(c.away);
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
    if (c.hurt > 0) s += " (hurt, half effort for " + duration(c.hurt) + ")";
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
    if (isAway(g.crew[(size_t)idx])) return {{Sev::Warn, g.crew[(size_t)idx].name + " is away on an expedition. You can let them go when they're back."}};
    CrewMember c = g.crew[(size_t)idx];
    g.crew.erase(g.crew.begin() + idx);
    remember(g, c, "fired");
    return {{Sev::Info, who(c) + " packed up and left."}};
}

Notes train(GameState& g, int idx) {
    if (!isUnlocked(g, Feature::Crew)) return {{Sev::Warn, lockedText(g, Feature::Crew)}};
    if (idx < 0 || idx >= (int)g.crew.size()) return {{Sev::Warn, "No such crew member."}};
    CrewMember& c = g.crew[(size_t)idx];
    if (isAway(c)) return {{Sev::Warn, c.name + " is away on an expedition (back in " + duration(c.away) + ")."}};
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
    if (isAway(c)) return {{Sev::Warn, c.name + " is away on an expedition (back in " + duration(c.away) + ")."}};
    if (c.level < levelCap(g, c)) {
        return {{Sev::Info, c.name + " can only be promoted at their level cap (" + std::to_string(levelCap(g, c)) +
                            "). Train them first."}};
    }
    long long cost = promoteCost(c);
    if (g.shards < cost) return {{Sev::Warn, "Promoting " + c.name + " costs " + num(cost) + " shards. You have " + num(g.shards) + "."}};

    g.shards -= cost;
    c.stars += 1;
    Notes extra;
    if (c.stars == 3) {
        std::string item = leaveKeepsake(g, c, "a thank-you for their third star");
        extra.push_back({Sev::Event, c.name + " is so proud of their third star they gave you something: " + item + " (on your shelf)."});
    }
    Notes out = {{Sev::Good, who(c) + " was promoted to " + std::string((size_t)c.stars, '*') + " (" + std::to_string(c.stars) +
                        (c.stars == 1 ? " star" : " stars") + "): level cap " + std::to_string(levelCap(g, c)) + ", every level worth +" +
                        std::to_string((int)std::lround(100 * PROMO_EFFECT * c.stars)) + "%. Next promotion: " +
                        num(promoteCost(c)) + " shards."}};
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
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

    for (CrewMember& c : g.crew) {
        if (c.sulk > 0) --c.sulk;
        if (c.hurt > 0) --c.hurt;
        if (c.away > 0 && --c.away == 0) comeBack(g, c, log, rng);
    }

    for (const CrewMember& c : g.crew) {
        if (isAway(c)) continue;   // exploring, not working
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
