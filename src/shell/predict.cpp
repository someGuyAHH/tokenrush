// predict <what> <amount>: what the next stretch of play should bring, from your current rates.
// Times (1h, 30min, 2h30m) for things that pile up over time; counts (1000, 5k) for things that
// happen per mine. Every answer says what it's based on. Same rule as every command: lock,
// read, unlock, then print.

#include <cctype>
#include <cmath>
#include <iostream>
#include <sstream>

#include "commands.hpp"
#include "core/actions.hpp"
#include "core/contracts.hpp"
#include "core/crew.hpp"
#include "core/disasters.hpp"
#include "core/game.hpp"
#include "core/util.hpp"
#include "term.hpp"

namespace {

// "1h", "30min", "2h30m", "90s", "45" (seconds): seconds, or -1 if it isn't a time.
// A lone "m" means minutes here: this is only called where a time is expected.
long long parseTime(const std::string& text) {
    std::string t = lower(trim(text));
    if (t.empty()) return -1;
    long long total = 0;
    size_t i = 0;
    while (i < t.size()) {
        size_t j = i;
        while (j < t.size() && (std::isdigit((unsigned char)t[j]) || t[j] == '.')) ++j;
        if (j == i) return -1;
        double v = std::stod(t.substr(i, j - i));
        size_t k = j;
        while (k < t.size() && std::isalpha((unsigned char)t[k])) ++k;
        std::string unit = t.substr(j, k - j);
        double mult = unit == "s" || unit == "sec" || unit.empty() ? 1 : unit == "m" || unit == "min" ? 60 : unit == "h" || unit == "hr" ? 3600 : unit == "d" ? 86400 : -1;
        if (mult < 0 || (unit.empty() && k < t.size())) return -1;
        total += (long long)std::llround(v * mult);
        i = k;
    }
    return total > 0 ? total : -1;
}

// A count ("1000", "5k", "2.5M"); "10min" or "1h" is a time instead (then mines = time x your pace).
bool parseCount(const std::string& text, double& n, long long& seconds) {
    std::string t = lower(trim(text));
    seconds = -1;
    if (t.size() > 1 && (t.back() == 's' || t.back() == 'h' || t.find("min") != std::string::npos || t.find('h') != std::string::npos)) {
        seconds = parseTime(t);
        return seconds > 0;
    }
    long long v;
    if (!parseAmount(t, v)) return false;
    n = (double)v;
    return true;
}

std::string tokensText(double v) { return compact((long long)std::min(9.0e18, std::max(0.0, v))); }
std::string countText(double v) { return std::abs(v) >= 100 ? num(std::llround(v)) : fixed(v, std::abs(v) >= 10 ? 0 : 1); }

void usage() {
    out() << paint(Style::Header, "=== PREDICT ===") << paint(Style::Muted, "   from how you're playing right now") << "\n"
          << "  Over a time (1h, 30min, 2h30m):\n"
          << "    " << padRight("predict tokens 1h", 28) << paint(Style::Muted, "your income x the time, by source") << "\n"
          << "    " << padRight("predict shards 1h", 28) << paint(Style::Muted, "what Scouts (and the Lucky Penny) find, minus wages") << "\n"
          << "    " << padRight("predict heat 30min", 28) << paint(Style::Muted, "where heat will have cooled to") << "\n"
          << "    " << padRight("predict paydays 1h", 28) << paint(Style::Muted, "how many, and the wages") << "\n"
          << "    " << padRight("predict events 1h", 28) << paint(Style::Muted, "and disasters: how many to expect") << "\n"
          << "  Over a number of mines (1000, 5k; or a time, at your pace):\n"
          << "    " << padRight("predict mines 1000", 28) << paint(Style::Muted, "tokens from that much mining by hand") << "\n"
          << "    " << padRight("predict jackpots 1000", 28) << paint(Style::Muted, "and bonuses: how many, and what they pay") << "\n"
          << "  Upgrades:\n"
          << "    " << padRight("predict pickaxe 10", 28) << paint(Style::Muted, "10 levels: the cost, the gain, how long they take to pay off") << "\n";
}

}  // namespace

void doPredict(Shared& s, const std::string& args) {
    std::stringstream ss(args);
    std::string what, amount;
    ss >> what >> amount;
    what = lower(what);
    if (what.empty() || amount.empty()) { usage(); return; }
    if (what.size() > 1 && what.back() == 's' && what != "tokens" && what != "shards" && what != "bonuses" && what != "jackpots" &&
        what != "mines" && what != "paydays" && what != "events" && what != "disasters")
        what.pop_back();   // "pickaxes" -> "pickaxe"

    std::lock_guard lk(s.m);
    GameState& g = s.g;
    auto needTime = [&](long long& secs) {
        secs = parseTime(amount);
        if (secs <= 0) out() << "'" << amount << "' isn't a time: try 1h, 30min or 2h30m.\n";
        return secs > 0;
    };

    if (what == "tokens" || what == "token" || what == "income") {
        long long t;
        if (!needTime(t)) return;
        // The last minute's steady income (contract payouts aside), or the 5-minute average if there's
        // no minute yet; the average lags for a while after loading.
        auto by = recentSourceRates(g);
        double steady = 0;
        for (int i = 0; i < SOURCE_COUNT; ++i) if (i != (int)Source::Contracts) steady += by[(size_t)i];
        double rate = steady > 0 ? steady : g.incomeRate;
        out() << "In " << duration(t) << " at your recent " << paint(Style::Tokens, perSecond(rate)) << " tokens/s: "
              << paint(Style::Tokens, "+" + tokensText(rate * (double)t)) << " tokens (you'd have about " << tokensText((double)g.tokens + rate * (double)t) << ").\n";
        std::string split;
        for (int i = 0; i < SOURCE_COUNT; ++i)
            if (by[(size_t)i] > 0) split += (split.empty() ? "" : ", ") + std::string(sourceName((Source)i)) + " +" + tokensText(by[(size_t)i] * (double)t);
        if (!split.empty()) out() << paint(Style::Muted, "  At this minute's pace: " + split + ".") << "\n";
        out() << paint(Style::Muted, "  (An average: events, contracts, disasters and what you buy change it.)") << "\n";
        return;
    }
    if (what == "shards" || what == "shard") {
        long long t;
        if (!needTime(t)) return;
        double perMin = 0;
        for (const CrewMember& c : g.crew)
            if (c.role == Role::Scout && !isAway(c)) perMin += scoutChance(g, c) * (scoutMinFind(g, c) + scoutMaxFind(g, c)) / 2.0 * (60.0 / SCOUT_EVERY);
        double penny = 0;
        if (gadgetOn(g, Gadget::LuckyPenny)) {
            double mines = g.mineRate * 60.0 + (hasResonance(g) && g.bot > 0 && !g.botBroken ? 60.0 / botIntervalOf(g) : 0.0);
            penny = mines * jackpotChance(g) * 2.0;
        }
        double found = (perMin + penny) * (double)t / 60.0;
        long long paydays = g.crew.empty() ? 0 : (t >= g.paydayClock ? 1 + (t - g.paydayClock) / PAYDAY_SECONDS : 0);
        double wages = (double)paydays * (double)totalWages(g);
        out() << "In " << duration(t) << ": about " << paint(Style::Shards, "+" + countText(found)) << " shards found";
        if (penny > 0) out() << " (" << countText(penny * (double)t / 60.0) << " of them from the Lucky Penny)";
        out() << ", " << paint(Style::Warn, "-" + num((long long)wages)) << " in wages (" << paydays << (paydays == 1 ? " payday" : " paydays") << "): ";
        double net = found - wages;
        out() << paint(net >= 0 ? Style::Good : Style::Bad, (net >= 0 ? "+" : "") + countText(net)) << " net, so about " << num(std::max(0LL, g.shards + (long long)std::llround(net))) << ".\n";
        out() << paint(Style::Muted, "  (Scouts at home, at their odds; not counting contracts, expeditions, the market or secret goals.)") << "\n";
        return;
    }
    if (what == "heat") {
        long long t;
        if (!needTime(t)) return;
        if (!isUnlocked(g, Feature::Contracts)) { out() << lockedText(g, Feature::Contracts) << "\n"; return; }
        if (infected(g, Strain::Fever)) { out() << "Fever: your heat doesn't cool on its own. It stays at " << g.heat << ".\n"; return; }
        int cool = heatCoolSeconds(g);
        long long drops = g.heat == 0 ? 0 : t >= g.heatClock ? 1 + (t - g.heatClock) / cool : 0;
        int after = (int)std::max(0LL, g.heat - drops);
        out() << "Heat " << g.heat << " now; in " << duration(t) << " about " << paint(after >= AUDIT_MIN_HEAT ? Style::Warn : Style::Good, std::to_string(after))
              << " (a point every " << duration(cool) << ", the next in " << duration(g.heatClock) << ").";
        if (g.heat > 0) out() << " Down to 0 in " << duration(g.heatClock + (long long)(g.heat - 1) * cool) << ".";
        out() << "\n" << paint(Style::Muted, "  (If nothing adds heat meanwhile: Shady clients, failed jobs.)") << "\n";
        return;
    }
    if (what == "payday" || what == "paydays" || what == "wages") {
        long long t;
        if (!needTime(t)) return;
        if (g.crew.empty()) { out() << "No crew, no paydays.\n"; return; }
        long long n = t >= g.paydayClock ? 1 + (t - g.paydayClock) / PAYDAY_SECONDS : 0;
        out() << "In " << duration(t) << ": " << n << (n == 1 ? " payday" : " paydays") << " (the next in " << duration(g.paydayClock) << "), "
              << paint(Style::Shards, num(n * totalWages(g))) << " shards in wages at today's " << num(totalWages(g)) << " each.\n";
        return;
    }
    if (what == "event" || what == "events" || what == "disaster" || what == "disasters") {
        long long t;
        if (!needTime(t)) return;
        double events = (double)t * EVENT_CHANCE_PER_SEC * (gadgetOn(g, Gadget::LightningRod) ? 2.0 : 1.0);
        out() << "In " << duration(t) << ": about " << paint(Style::Event, countText(events)) << (std::lround(events) == 1 ? " event" : " events");
        if (isUnlocked(g, Feature::Crew)) {
            double every = (DISASTER_EVERY_MIN + DISASTER_EVERY_MAX) / 2.0 * (gadgetOn(g, Gadget::LightningRod) ? 0.8 : 1.0);
            out() << " and " << paint(Style::Bad, countText((double)t / every)) << " disasters (one every " << duration((long long)every) << " or so)";
        }
        out() << ".\n" << paint(Style::Muted, "  (They're random: that's the average.)") << "\n";
        return;
    }
    if (what == "mine" || what == "mines" || what == "jackpot" || what == "jackpots" || what == "bonus" || what == "bonuses") {
        double n = 0;
        long long secs = -1;
        if (!parseCount(amount, n, secs)) { out() << "'" << amount << "' isn't a number of mines (1000, 5k) or a time (10min, 1h).\n"; return; }
        std::string pace;
        if (secs > 0) {
            n = g.mineRate * (double)secs;
            pace = " (" + duration(secs) + " at your pace of " + fixed(g.mineRate, 2) + " mines/s)";
            if (n < 1) { out() << "At your recent pace (" << fixed(g.mineRate, 2) << " mines/s) that's no mining at all. Try a number: predict " << what << " 1000.\n"; return; }
        }
        double base = baseYield(g), streak = 2.0;   // a full streak
        double hand = handMult(g);
        if (what == "mine" || what == "mines") {
            double each = (base * 1.5 + bonusChance(g) * base * bonusSize(g)) * streak * hand + jackpotChance(g) * base * jackpotSize(g) * hand;
            out() << countText(n) << " mines" << pace << ": about " << paint(Style::Tokens, "+" + tokensText(each * n)) << " tokens ("
                  << tokensText(each) << " a mine, with a full streak).\n";
            return;
        }
        bool jack = what[0] == 'j';
        double p = jack ? jackpotChance(g) : bonusChance(g);
        double size = jack ? jackpotSize(g) : bonusSize(g);
        double count = n * p, spread = std::sqrt(n * p * (1.0 - p));
        double each = base * size * hand;
        out() << countText(n) << " mines" << pace << " at a " << percent(p) << (jack ? " jackpot" : " bonus") << " chance: about "
              << paint(Style::Event, countText(count)) << (jack ? " jackpots" : " bonuses") << " (give or take " << countText(spread) << "), each adding "
              << tokensText(each) << " tokens (" << compact((long long)size) << "x your base yield): " << paint(Style::Tokens, "+" + tokensText(each * count)) << " in all.\n";
        if (jack && gadgetOn(g, Gadget::LuckyPenny)) out() << paint(Style::Muted, "  The Lucky Penny: about " + countText(count * 2) + " shards from them too.") << "\n";
        return;
    }
    static const char* const UPGRADES[] = {"pickaxe", "magnet", "drone", "bot"};
    for (int i = 0; i < 4; ++i) {
        if (what != UPGRADES[i] && !(i == 3 && what == "miningbot")) continue;
        long long n;
        if (!parseAmount(amount, n) || n > 100000) { out() << "How many levels? predict " << what << " 10\n"; return; }
        long long cost = upgradesCost(g, (size_t)i, (int)n);
        double now = std::max(1e-9, incomeEstimate(g));
        GameState after = g;
        *upgrades(after)[(size_t)i].lvl += (int)n;
        double ratio = incomeEstimate(after) / now;
        double gainPerSec = g.incomeRate * (ratio - 1.0);
        out() << num(n) << " " << upgrades(g)[(size_t)i].name << (n == 1 ? " level" : " levels") << ": " << paint(Style::Tokens, tokensText((double)cost))
              << " tokens, for about " << paint(Style::Good, "+" + percent(ratio - 1.0, ratio - 1.0 >= 0.1 ? 0 : 2)) << " income (+" << perSecond(gainPerSec) << "/s).";
        if (gainPerSec > 0) out() << " Pays for itself in " << duration((long long)std::min(1e9, (double)cost / gainPerSec)) << ".";
        if (cost > g.tokens) out() << paint(Style::Muted, " (You have " + tokensText((double)g.tokens) + ".)");
        out() << "\n";
        return;
    }
    out() << "Can't predict '" << what << "'.\n";
    usage();
}
