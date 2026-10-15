// The shard economy screens: the market, contracts and the crew.
// Same rule as everywhere in the CLI: lock, call the core, unlock, then print.

#include <algorithm>
#include <iostream>

#include "commands.hpp"
#include "core/contracts.hpp"
#include "core/crew.hpp"
#include "core/game.hpp"
#include "core/market.hpp"
#include "core/util.hpp"
#include "term.hpp"

namespace {

std::string header(const std::string& title) { return paint(Style::Header, "=== " + title + " ==="); }

// "3" -> 2 (a 0-based index). Returns -1 for anything that isn't a positive number.
int parseIndex(const std::string& arg) {
    long long n;
    if (!parseLongLong(arg, n) || n < 1 || n > 1000) return -1;
    return (int)n - 1;
}

// "Odette  Scout  L8 **  Night Owl" (fixed widths, before painting)
std::string crewRow(const CrewMember& c) {
    std::string row = padRight(c.name, 8) + padRight(ROLES[(int)c.role].name, 7) +
                      padRight("L" + std::to_string(c.level) + (c.stars ? " " + starsText(c) : ""), 9);
    return row + padRight(traitName(c), 14);
}

std::string offerRow(const Contract& c) {
    std::string tag = c.big ? paint(Style::Warn, "[BIG] ") : "      ";
    std::string terms = "reward " + paint(Style::Tokens, num(c.rewardTokens)) + " + " +
                        paint(Style::Shards, count(c.rewardShards, "shard"));
    if (c.stake > 0) terms += ", stake " + paint(Style::Shards, num(c.stake));
    return tag + padRight(contractText(c) + " in " + duration(c.duration), 44) + terms;
}

}  // namespace

// ----- Market -----
void doMarket(Shared& s) {
    std::lock_guard lk(s.m);
    const GameState& g = s.g;

    out() << header("MARKET") << "\n";
    out() << "Price " << paint(Style::Bold, fixed(g.shardPrice, 1))
              << "   Buy at " << fixed(buyPrice(g), 1) << "   Sell at " << fixed(sellPrice(g), 1)
              << "   (spread " << percent(marketSpread(g)) << ")\n";

    if (g.priceHistory.size() >= 2) {
        auto [lo, hi] = std::minmax_element(g.priceHistory.begin(), g.priceHistory.end());
        out() << "Last " << duration((long long)g.priceHistory.size() * 5) << ": "
                  << paint(Style::Shards, sparkline(g.priceHistory))
                  << paint(Style::Muted, "  low " + fixed(*lo, 1) + "  high " + fixed(*hi, 1)) << "\n";
    } else {
        out() << paint(Style::Muted, "(the chart fills in as the market moves)") << "\n";
    }
    out() << paint(Style::Muted, "Prices drift back toward " + fixed(SHARD_FAIR_PRICE, 0) +
                                     ". Every shard you buy pushes the price up a little, every one you sell pushes it down.") << "\n";

    out() << "You have " << paint(Style::Shards, count(g.shards, "shard")) << " and "
              << paint(Style::Tokens, num(g.tokens) + " tokens") << " (enough for " << num(maxAffordable(g)) << ").\n";
    out() << "10 shards cost " << num(buyCost(g, 10)) << "; selling 10 pays " << num(sellValue(g, 10)) << ".\n";

    if (g.heat > 0) out() << paint(Style::Heat, "Heat " + std::to_string(g.heat) + " is adding " + percent(0.02 * g.heat) + " to the spread.") << "\n";
    out() << "Use 'buy <n|max>' or 'sell <n|all>'.\n";
}

void doBuy(Shared& s, const std::string& arg) {
    long long n = 0;
    bool ask = false;   // a big order that would fill far above the current price
    {
        std::lock_guard lk(s.m);
        if (arg == "max") n = maxAffordable(s.g);
        else if (!parseAmount(arg, n)) { out() << "Usage: buy <n|max>  (2000 and 2k both work)\n"; return; }
        if (arg == "max" && n == 0) { out() << "You can't afford a single shard.\n"; return; }

        ask = pricyOrder(s.g, n);
        if (ask) {
            long long cost = buyCost(s.g, n);
            out() << paint(Style::Warn, count(n, "shard") + " would cost " + num(cost) + " tokens, about " +
                                            num(cost / n) + " each (the price is " + fixed(s.g.shardPrice, 1) + " now).") << "\n";
        }
    }
    if (ask) {   // ask without holding the lock
        out() << "Type yes to buy anyway: " << std::flush;
        std::string line;
        if (!std::getline(in(), line) || lower(trim(line)) != "yes") { out() << "Cancelled.\n"; return; }
    }

    Notes out;
    {
        std::lock_guard lk(s.m);
        if (arg == "max") n = maxAffordable(s.g);   // the balance may have moved while you typed
        out = n > 0 ? buyShards(s.g, n) : Notes{{Sev::Warn, "You can't afford a single shard."}};
    }
    printNotes(out);
}

void doSell(Shared& s, const std::string& arg) {
    Notes out;
    {
        std::lock_guard lk(s.m);
        long long n = 0;
        if (arg == "all") n = std::min(s.g.shards, MARKET_MAX_ORDER);
        else if (!parseAmount(arg, n)) out = {{Sev::Warn, "Usage: sell <n|all>  (2000 and 2k both work)"}};
        if (out.empty()) out = (arg == "all" && n == 0) ? Notes{{Sev::Warn, "You have no shards."}}
                                                        : sellShards(s.g, n);
    }
    printNotes(out);
}

// ----- Contracts -----
void doContracts(Shared& s) {
    std::lock_guard lk(s.m);
    const GameState& g = s.g;

    out() << header("CONTRACTS") << "   Heat " << paint(Style::Heat, heatBar(g.heat, MAX_HEAT)) << " " << g.heat
              << paint(Style::Muted, "   done " + num(g.contractsDone) + ", failed " + num(g.contractsFailed)) << "\n";

    int slots = contractSlots(g);
    for (size_t i = 0; i < g.contracts.size(); ++i) {
        const Contract& c = g.contracts[i];
        long long p = std::min(contractProgress(g, c), c.target);
        std::string tag = slots > 1 ? "Active " + std::to_string(i + 1) : "Active";
        out() << paint(Style::Bold, tag) << " (for " << CLIENTS[c.client] << "): " << contractText(c) << "\n"
                  << "  " << bar(p, c.target, 20) << " " << num(p) << "/" << num(c.target) << "   "
                  << paint(c.secondsLeft <= CONTRACT_WARN_AT ? Style::Warn : Style::Plain, duration(c.secondsLeft) + " left") << "\n"
                  << "  Pays " << paint(Style::Tokens, num(c.rewardTokens) + " tokens") << " + "
                  << paint(Style::Shards, count(c.rewardShards, "shard"));
        if (c.stake > 0) out() << " and your " << num(c.stake) << "-shard stake back";
        out() << ".\n";
    }
    if (g.contracts.empty()) out() << paint(Style::Muted, "No active contract.") << "\n";
    else if ((int)g.contracts.size() < slots) out() << paint(Style::Muted, "You have room for one more job.") << "\n";

    out() << paint(Style::Bold, "Offers") << ":\n";
    if (g.offers.empty()) out() << paint(Style::Muted, "  (the board is empty; check back soon)") << "\n";
    for (size_t i = 0; i < g.offers.size(); ++i) {
        const Contract& o = g.offers[i];
        out() << "  " << (i + 1) << ") " << offerRow(o) << "\n"
                  << "       " << paint(Style::Muted, std::string("from ") + CLIENTS[o.client] + ", leaves the board in " + duration(o.expiresIn)) << "\n";
    }
    out() << paint(Style::Muted, "Fail or abandon a job and you lose the stake and gain heat (big jobs: +2). "
                                     "Heat widens market spreads, and from " + std::to_string(AUDIT_MIN_HEAT) + " up, audits can seize tokens.") << "\n";
    out() << (slots > 1 ? "Use 'accept <n>' or 'abandon <n>'.\n" : "Use 'accept <n>' or 'abandon'.\n");
}

void doAccept(Shared& s, const std::string& arg) {
    int idx = parseIndex(arg);
    Notes out;
    {
        std::lock_guard lk(s.m);
        out = idx < 0 ? Notes{{Sev::Warn, "Usage: accept <n>  (see 'contracts')"}} : acceptContract(s.g, idx);
    }
    printNotes(out);
}

void doAbandon(Shared& s, const std::string& arg) {
    int idx = arg.empty() ? 0 : parseIndex(arg);
    {
        std::lock_guard lk(s.m);
        if (s.g.contracts.empty()) { out() << "You don't have a contract.\n"; return; }
        if (arg.empty() && s.g.contracts.size() > 1) { out() << "You have two jobs: 'abandon 1' or 'abandon 2'.\n"; return; }
        if (idx < 0 || idx >= (int)s.g.contracts.size()) { out() << "No such contract.\n"; return; }
        out() << "Abandon: " << contractText(s.g.contracts[(size_t)idx]) << "\n";
    }
    out() << paint(Style::Warn, "Abandoning counts as a failure (stake lost, heat up). Type yes to confirm: ") << std::flush;
    std::string line;
    if (!std::getline(in(), line) || lower(trim(line)) != "yes") { out() << "Kept the contract.\n"; return; }

    Notes out;
    {
        std::lock_guard lk(s.m);
        out = abandonContract(s.g, idx);   // re-checks: it may have finished or failed while you typed
    }
    printNotes(out);
}

// ----- Crew -----
void doCrew(Shared& s) {
    std::lock_guard lk(s.m);
    const GameState& g = s.g;

    out() << header("CREW (" + std::to_string(g.crew.size()) + "/" + std::to_string(crewCap(g)) + ")")
              << "   Shards: " << paint(Style::Shards, num(g.shards));
    if (!g.crew.empty())
        out() << "   Payday in " << duration(g.paydayClock) << ": " << count(totalWages(g), "shard");
    out() << "\n";

    if (g.crew.empty()) out() << paint(Style::Muted, "  Nobody yet. Hire someone below.") << "\n";
    for (size_t i = 0; i < g.crew.size(); ++i) {
        const CrewMember& c = g.crew[i];
        out() << "  " << (i + 1) << ") " << paint(Style::Bold, crewRow(c)) << crewEffectText(g, c);
        if (c.level < levelCap(g, c)) out() << paint(Style::Muted, "   train: " + num(trainCost(c)) + " shards");
        else                          out() << paint(Style::Muted, "   promote: " + num(promoteCost(c)) + " shards");
        out() << "\n";
    }

    out() << paint(Style::Bold, "Looking for work") << paint(Style::Muted, " (new faces in " + duration(g.candidateClock) + ")") << ":\n";
    for (size_t i = 0; i < g.candidates.size(); ++i) {
        const CrewMember& c = g.candidates[i];
        out() << "  " << (i + 1) << ") " << crewRow(c) << "hire: " << paint(Style::Shards, num(hireCost(g, c)) + " shards")
                  << paint(Style::Muted, "   " + std::string(c.trait != Trait::None ? TRAITS[(int)c.trait].desc : ROLES[(int)c.role].perk)) << "\n";
    }
    out() << paint(Style::Muted, "Each payday every member takes about their level in shards. Anyone you can't pay quits.") << "\n";
    out() << paint(Style::Muted, "At their level cap, a member can be promoted: a higher cap, and every level counts for more.") << "\n";
    out() << "Use 'hire <n>', 'fire <n>', 'train <n>' or 'promote <n>'.\n";
}

void doHire(Shared& s, const std::string& arg) {
    int idx = parseIndex(arg);
    Notes out;
    {
        std::lock_guard lk(s.m);
        out = idx < 0 ? Notes{{Sev::Warn, "Usage: hire <n>  (see 'crew')"}} : hire(s.g, idx);
    }
    printNotes(out);
}

void doFire(Shared& s, const std::string& arg) {
    int idx = parseIndex(arg);
    Notes out;
    {
        std::lock_guard lk(s.m);
        out = idx < 0 ? Notes{{Sev::Warn, "Usage: fire <n>  (see 'crew')"}} : fire(s.g, idx);
    }
    printNotes(out);
}

void doTrain(Shared& s, const std::string& arg) {
    int idx = parseIndex(arg);
    Notes out;
    {
        std::lock_guard lk(s.m);
        out = idx < 0 ? Notes{{Sev::Warn, "Usage: train <n>  (see 'crew')"}} : train(s.g, idx);
    }
    printNotes(out);
}

void doPromote(Shared& s, const std::string& arg) {
    int idx = parseIndex(arg);
    Notes notes;
    {
        std::lock_guard lk(s.m);
        notes = idx < 0 ? Notes{{Sev::Warn, "Usage: promote <n>  (see 'crew')"}} : promote(s.g, idx);
    }
    printNotes(notes);
}
