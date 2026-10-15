#include "exchange.hpp"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

#include "util.hpp"

namespace fs = std::filesystem;

namespace {

// Set by exchangeStart before the workers start, cleared by exchangeStop after they've stopped.
std::atomic<bool> g_open{false};
fs::path g_dir;            // <data>/exchange
std::string g_me;          // this profile's name
std::string g_session;     // tells this game's heartbeat from another window's
std::time_t g_lastBeat = 0;

std::string key(const std::string& name) { return lower(trim(name)); }
fs::path inbox(const std::string& name) { return g_dir / key(name); }

std::string newId() {
    static std::mt19937_64 rng{std::random_device{}() ^ (unsigned long long)std::time(nullptr)};
    std::ostringstream os;
    os << std::hex << rng();
    return os.str();
}

// Written to a temporary name first, so nobody ever reads half a file.
bool writeAtomic(const fs::path& path, const std::string& text) {
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << text;
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    return !ec;
}

std::map<std::string, std::string> readKeys(const fs::path& path) {
    std::map<std::string, std::string> kv;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return kv;
}

long long toLong(const std::string& s) {
    long long v = 0;
    try { v = std::stoll(s); } catch (...) {}
    return v;
}

struct Alive {
    std::string name, session;
    std::time_t at = 0;
};

bool readAlive(const fs::path& dir, Alive& out) {
    std::error_code ec;
    if (!fs::exists(dir / "alive", ec)) return false;
    auto kv = readKeys(dir / "alive");
    out.name = kv["name"];
    out.session = kv["session"];
    out.at = (std::time_t)toLong(kv["at"]);
    return !out.name.empty();
}

void beat() {
    std::ostringstream os;
    os << "name=" << g_me << "\nsession=" << g_session << "\nat=" << (long long)std::time(nullptr) << "\n";
    writeAtomic(inbox(g_me) / "alive", os.str());
    g_lastBeat = std::time(nullptr);
}

// `who`, if it's another profile that's running now: its real spelling in `real`.
bool findOnline(const std::string& who, std::string& real, std::string& why) {
    if (!g_open) { why = "Paying and borrowing work between profiles (not with --save-dir)."; return false; }
    if (trim(who).empty()) { why = "Who? Give a profile's name."; return false; }
    if (key(who) == key(g_me)) { why = "That's this profile."; return false; }
    for (const std::string& name : onlineProfiles())
        if (key(name) == key(who)) { real = name; return true; }
    why = trim(who) + " isn't running right now: both games have to be open (any other profile, in another window).";
    return false;
}

void adjust(GameState& g, const std::string& who, long long delta) {
    if (delta == 0) return;
    for (auto it = g.balances.begin(); it != g.balances.end(); ++it) {
        if (key(it->first) != key(who)) continue;
        it->second += delta;
        if (it->second == 0) g.balances.erase(it);
        return;
    }
    g.balances.push_back({who, delta});
}

// " Beta owes you 5B now." / " You owe Beta 2B now." / " You're square with Beta." (only if a debt just moved)
std::string standing(const GameState& g, const std::string& who, bool settled) {
    long long b = balanceWith(g, who);
    if (b > 0) return " " + who + " owes you " + compact(b) + " now.";
    if (b < 0) return " You owe " + who + " " + compact(-b) + " now.";
    return settled ? " You're square with " + who + "." : "";
}

void eraseAsk(std::vector<LoanAsk>& asks, const std::string& who, long long paid) {
    for (auto it = asks.begin(); it != asks.end(); ++it) {
        if (key(it->who) != key(who)) continue;
        it->amount -= paid;
        if (it->amount <= 0) asks.erase(it);
        return;
    }
}

}  // namespace

bool exchangeStart(const fs::path& dataRoot, const std::string& me, std::string& why, std::string& warning) {
    g_dir = dataRoot / "exchange";
    g_me = trim(me);
    g_session = newId();
    std::error_code ec;
    fs::create_directories(inbox(g_me), ec);
    if (ec) { why = "Can't make " + inbox(g_me).string() + ": " + ec.message(); return false; }
    Alive a;
    if (readAlive(inbox(g_me), a) && std::time(nullptr) - a.at <= ALIVE_SECONDS)
        warning = g_me + " seems to be open in another window too. Both would save over each other: close one.";
    for (const auto& e : fs::directory_iterator(inbox(g_me), ec))   // left over from a game that crashed mid-claim
        if (e.path().extension() == ".taken" || e.path().extension() == ".tmp") fs::remove(e.path(), ec);
    beat();
    g_open = true;
    return true;
}

bool exchangeOpen() { return g_open; }

std::vector<std::string> onlineProfiles() {
    std::vector<std::string> out;
    if (!g_open) return out;
    std::error_code ec;
    std::time_t now = std::time(nullptr);
    for (const auto& e : fs::directory_iterator(g_dir, ec)) {
        Alive a;
        if (!e.is_directory(ec) || !readAlive(e.path(), a)) continue;
        if (key(a.name) == key(g_me) || now - a.at > ALIVE_SECONDS) continue;
        out.push_back(a.name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

long long balanceWith(const GameState& g, const std::string& who) {
    for (const auto& [name, b] : g.balances) if (key(name) == key(who)) return b;
    return 0;
}

Notes payProfile(GameState& g, const std::string& who, long long amount) {
    std::string real, why;
    if (!findOnline(who, real, why)) return {{Sev::Warn, why}};
    long long asked = 0;
    for (const LoanAsk& a : g.asksIn) if (key(a.who) == key(real)) asked = a.amount;
    long long owe = std::max(0LL, -balanceWith(g, real));
    if (amount <= 0) amount = asked > 0 ? asked : owe;
    if (amount <= 0) return {{Sev::Warn, "How much? 'pay " + real + " <amount>', like 5B."}};
    if (amount > g.tokens) return {{Sev::Warn, "You have " + num(g.tokens) + " tokens."}};

    // Pay off what you owe them first, then lend what they asked for; the rest is a gift.
    long long repay = std::min(amount, owe);
    long long lend = std::min(amount - repay, asked);
    Transfer t;
    t.id = newId();
    t.to = real;
    t.amount = amount;
    t.settles = repay + lend;
    t.sent = std::time(nullptr);
    std::ostringstream os;
    os << "from=" << g_me << "\namount=" << amount << "\nsettles=" << t.settles << "\n";
    if (!writeAtomic(inbox(real) / (t.id + ".pay"), os.str())) return {{Sev::Bad, "Couldn't reach " + real + "'s game (the exchange folder isn't writable)."}};
    g.tokens -= amount;
    g.outgoing.push_back(t);
    if (lend > 0) eraseAsk(g.asksIn, real, lend);
    std::string what = repay > 0 && repay == amount ? " (paying back what you owe them)" : lend > 0 ? " (a loan: they asked)" : "";
    return {{Sev::Info, "Sending " + num(amount) + " tokens to " + real + what + "..."}};
}

Notes askToBorrow(GameState& g, const std::string& who, long long amount) {
    std::string real, why;
    if (!findOnline(who, real, why)) return {{Sev::Warn, why}};
    if (amount <= 0) return {{Sev::Warn, "How much? 'borrow " + real + " <amount>', like 5B."}};
    std::ostringstream os;
    std::time_t until = std::time(nullptr) + ASK_SECONDS;
    os << "from=" << g_me << "\namount=" << amount << "\nuntil=" << (long long)until << "\n";
    if (!writeAtomic(inbox(real) / (newId() + ".ask"), os.str())) return {{Sev::Bad, "Couldn't reach " + real + "'s game."}};
    eraseAsk(g.asksOut, real, 1LL << 62);   // a new ask replaces the old one
    g.asksOut.push_back({real, amount, until});
    return {{Sev::Good, "Asked " + real + " to lend you " + num(amount) + " tokens. They answer with 'pay " + g_me +
                        "' in their game; the request stands for " + duration(ASK_SECONDS) + "."}};
}

void exchangeTick(GameState& g, MessageLog& log) {
    if (!g_open) return;
    std::time_t now = std::time(nullptr);
    if (now - g_lastBeat >= 2) beat();

    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(inbox(g_me), ec)) files.push_back(e.path());
    std::sort(files.begin(), files.end());   // ids are random, but this keeps a tick's order stable
    for (const fs::path& p : files) {
        if (p.extension() == ".pay") {
            fs::path taken = p;
            taken.replace_extension(".taken");
            fs::rename(p, taken, ec);   // the claim: if the payer took it back first, this fails
            if (ec) continue;
            auto kv = readKeys(taken);
            fs::remove(taken, ec);
            std::string from = kv["from"];
            long long amount = toLong(kv["amount"]), settles = std::clamp(toLong(kv["settles"]), 0LL, amount);
            if (from.empty() || amount <= 0) continue;
            long long before = balanceWith(g, from);
            g.tokens = addTokens(g.tokens, amount);   // not income: it was earned in the other game
            adjust(g, from, -settles);
            eraseAsk(g.asksOut, from, amount);
            std::string what = settles == 0 ? "" : before > 0 ? " That pays off what they owed you." : " It's a loan.";
            log.push(Sev::Good, from + " paid you " + num(amount) + " tokens." + what + standing(g, from, settles > 0));
        } else if (p.extension() == ".ask") {
            auto kv = readKeys(p);
            fs::remove(p, ec);
            std::string from = kv["from"];
            long long amount = toLong(kv["amount"]);
            std::time_t until = (std::time_t)toLong(kv["until"]);
            if (from.empty() || amount <= 0 || until <= now) continue;
            eraseAsk(g.asksIn, from, 1LL << 62);
            g.asksIn.push_back({from, amount, until});
            log.push(Sev::Event, from + " asks to borrow " + num(amount) + " tokens. 'pay " + from + "' lends it (they'll owe you); the request stands for " +
                                     duration(until - now) + ".");
        }
    }

    for (auto it = g.outgoing.begin(); it != g.outgoing.end();) {
        fs::path p = inbox(it->to) / (it->id + ".pay");
        if (!fs::exists(p, ec)) {   // claimed
            adjust(g, it->to, it->settles);
            log.push(Sev::Good, it->to + " got your " + num(it->amount) + " tokens." + standing(g, it->to, it->settles > 0));
            it = g.outgoing.erase(it);
            continue;
        }
        if (now - it->sent > TRANSFER_TIMEOUT) {
            fs::path back = p;
            back.replace_extension(".back");
            fs::rename(p, back, ec);   // taking it back: if they claimed it first, this fails and next second says it arrived
            if (!ec) {
                fs::remove(back, ec);
                g.tokens = addTokens(g.tokens, it->amount);
                log.push(Sev::Warn, it->to + "'s game didn't pick up your " + num(it->amount) + " tokens, so they came back.");
                it = g.outgoing.erase(it);
                continue;
            }
        }
        ++it;
    }

    auto expired = [now](const LoanAsk& a) { return a.until <= now; };
    g.asksIn.erase(std::remove_if(g.asksIn.begin(), g.asksIn.end(), expired), g.asksIn.end());
    g.asksOut.erase(std::remove_if(g.asksOut.begin(), g.asksOut.end(), expired), g.asksOut.end());
}

void exchangeStop(GameState& g) {
    if (!g_open) return;
    std::error_code ec;
    for (const Transfer& t : g.outgoing) {   // whatever wasn't claimed comes back before the final save
        fs::path p = inbox(t.to) / (t.id + ".pay"), back = p;
        back.replace_extension(".back");
        fs::rename(p, back, ec);
        if (!ec) { fs::remove(back, ec); g.tokens = addTokens(g.tokens, t.amount); }
        else adjust(g, t.to, t.settles);   // they got it
    }
    g.outgoing.clear();
    Alive a;
    if (readAlive(inbox(g_me), a) && a.session == g_session) fs::remove(inbox(g_me) / "alive", ec);   // not another window's
    g_open = false;
}
