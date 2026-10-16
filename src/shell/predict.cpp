// predict <what> <amount>: what the next stretch of play should bring, from your current rates.
// Times (1h, 30min, 2h30m) for things that pile up over time; counts (1000, 5k) for things that
// happen per mine. Every answer says what it's based on. Same rule as every command: lock,
// read, unlock, then print.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iostream>
#include <sstream>

#include "commands.hpp"
#include "core/actions.hpp"
#include "core/contracts.hpp"
#include "core/crew.hpp"
#include "core/disasters.hpp"
#include "core/game.hpp"
#include "core/projection.hpp"
#include "core/tasks.hpp"
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

// "45s", "12m", "3h 12m", "2d 5h": for spans that can run to days.
std::string span(long long t) {
    if (t < 60) return std::to_string(t) + "s";
    if (t < 3600) return std::to_string(t / 60) + "m" + (t % 60 && t < 600 ? " " + std::to_string(t % 60) + "s" : "");
    if (t < 86400) return std::to_string(t / 3600) + "h" + (t % 3600 >= 60 ? " " + std::to_string(t % 3600 / 60) + "m" : "");
    return std::to_string(t / 86400) + "d" + (t % 86400 >= 3600 ? " " + std::to_string(t % 86400 / 3600) + "h" : "");
}

// "around 21:41", "around 09:10 tomorrow", "around Oct 18, 14:00": wall-clock time t seconds from now.
std::string clockIn(long long t) {
    std::time_t now = std::time(nullptr), then = now + (std::time_t)t;
    std::tm a{}, b{};
#ifdef _WIN32
    localtime_s(&a, &now); localtime_s(&b, &then);
#else
    localtime_r(&now, &a); localtime_r(&then, &b);
#endif
    char hm[8], day[16];
    std::strftime(hm, sizeof hm, "%H:%M", &b);
    std::strftime(day, sizeof day, "%b %d", &b);
    int days = (b.tm_year - a.tm_year) * 366 + (b.tm_yday - a.tm_yday);
    if (days == 0) return std::string("around ") + hm;
    if (days == 1) return std::string("around ") + hm + " tomorrow";
    return std::string("around ") + day + ", " + hm;
}

constexpr long long WHEN_HORIZON = 30LL * 86400;

// predict when <condition>: the first moment it holds, if you change nothing.
void predictWhen(GameState& g, const std::string& text) {
    TaskCheck c;
    auto prog = compileCondition(text, c);
    if (!prog) {
        out() << "  " << text << "\n";
        if (c.pos >= 0) out() << "  " << std::string((size_t)c.pos, ' ') << paint(Style::Bad, std::string((size_t)std::max(1, c.len), '^')) << "\n";
        out() << paint(Style::Warn, "  " + c.error) << "\n";
        return;
    }
    std::vector<std::string> values = conditionValues(*prog);
    if (std::find(values.begin(), values.end(), "price") != values.end()) {
        out() << "The shard price is random: there's no telling when it gets there. (Try a task: when price < ... do notify \"...\")\n";
        return;
    }
    Projection pr = projectionOf(g);
    GameState work = g;   // copied once; projectInto rewrites what moves
    auto holds = [&](long long t) { projectInto(g, pr, t, work); return conditionHolds(*prog, work); };
    if (holds(0)) { out() << paint(Style::Good, "Already true.") << "\n"; return; }
    bool moves = std::any_of(values.begin(), values.end(), [](const std::string& v) { return movesWithTime(v); });
    if (!moves) {
        out() << "Never by itself: " << (values.empty() ? std::string("it doesn't read anything that changes") : "nothing it reads changes unless you act") << ".\n";
        return;
    }

    // Second by second at first, then in growing steps; then narrow the step it turned true in.
    long long before = 0, t = 0, found = -1;
    while (t < WHEN_HORIZON) {
        t += t < 120 ? 1 : t < 3600 ? 10 : t < 86400 ? 60 : 300;
        if (holds(t)) { found = t; break; }
        before = t;
    }
    if (found < 0) {
        out() << "Not within 30 days at this rate.";
        double shardNet = pr.pennyPerSec - (g.crew.empty() ? 0.0 : (double)totalWages(g) / PAYDAY_SECONDS);
        for (double s : pr.scoutPerSec) shardNet += s;
        if (std::find(values.begin(), values.end(), "tokens") != values.end() && pr.tokensPerSec <= 0) out() << " You're not earning anything right now.";
        if (std::find(values.begin(), values.end(), "shards") != values.end() && shardNet < 0)
            out() << " Your shards are falling: about " << countText(-shardNet * PAYDAY_SECONDS) << " a payday more go out than come in.";
        out() << "\n";
        return;
    }
    long long lo = before, hi = found;   // holds(hi), not holds(lo)
    while (hi - lo > 1) {
        long long mid = (lo + hi) / 2;
        (holds(mid) ? hi : lo) = mid;
    }
    out() << "In about " << paint(Style::Event, span(hi)) << " (" << clockIn(hi) << ", if the game stays open).\n";
    std::string assumes = "  (If you change nothing: no purchases, events, contracts or disasters";
    bool spending = (g.autoUnlocked && g.autoOn) || std::any_of(g.tasks.begin(), g.tasks.end(), [](const Task& tk) { return tk.on; });
    bool readsTokens = std::find(values.begin(), values.end(), "tokens") != values.end();
    if (spending && readsTokens) assumes += ". Your Auto-Buyer or tasks spend as you go, so it may come later";
    out() << paint(Style::Muted, assumes + ".)") << "\n";
}

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
          << "  The other way round, when will something be true (the Tasks' conditions, plus earned, mines, jackpots):\n"
          << "    " << padRight("predict when tokens > 20t", 28) << paint(Style::Muted, "how long until then, and at what time") << "\n"
          << "    " << padRight("predict when tokens > 5 * cost.pickaxe", 28) << paint(Style::Muted, " when you can afford 5 Pickaxe levels") << "\n"
          << "  Upgrades:\n"
          << "    " << padRight("predict pickaxe 10", 28) << paint(Style::Muted, "10 levels: the cost, the gain, how long they take to pay off") << "\n";
}

}  // namespace

void doPredict(Shared& s, const std::string& args) {
    std::stringstream ss(args);
    std::string what, amount;
    ss >> what >> amount;
    what = lower(what);
    if (what == "when") {
        std::string rest = trim(args);
        rest = trim(rest.substr(std::min(rest.size(), (size_t)4)));
        if (rest.empty()) { out() << "Predict when what? Like: predict when tokens > 20t\n"; return; }
        std::lock_guard lk(s.m);
        predictWhen(s.g, rest);
        return;
    }
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
        double rate = projectionOf(g).tokensPerSec;   // the same rate predict when uses
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
        Projection pr = projectionOf(g);
        GameState after = g;
        projectInto(g, pr, t, after);
        long long paydays = g.crew.empty() ? 0 : (t >= g.paydayClock ? 1 + (t - g.paydayClock) / PAYDAY_SECONDS : 0);
        double wages = (double)paydays * (double)totalWages(g);
        double net = (double)(after.shards - g.shards), found = net + wages, penny = pr.pennyPerSec * (double)t;
        out() << "In " << duration(t) << ": about " << paint(Style::Shards, "+" + countText(found)) << " shards found";
        if (penny > 0) out() << " (" << countText(penny) << " of them from the Lucky Penny)";
        out() << ", " << paint(Style::Warn, "-" + num((long long)wages)) << " in wages (" << paydays << (paydays == 1 ? " payday" : " paydays") << "): ";
        out() << paint(net >= 0 ? Style::Good : Style::Bad, (net >= 0 ? "+" : "") + countText(net)) << " net, so about " << num(std::max(0LL, after.shards)) << ".\n";
        out() << paint(Style::Muted, "  (Scouts at their odds once they're home, what expeditions typically bring back; not contracts, the market or secret goals.)") << "\n";
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
