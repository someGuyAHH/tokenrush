#pragma once
// Small helpers with no game knowledge in them.

#include <chrono>
#include <cstdint>
#include <ctime>
#include <ios>
#include <stop_token>
#include <string>
#include <vector>

std::time_t nowSec();
unsigned makeSeed();

std::string lower(std::string s);
std::string trim(const std::string& s);   // strips whitespace, including a stray '\r' from CRLF files

inline bool hasBit(std::uint32_t mask, int i) { return ((mask >> i) & 1u) != 0; }

std::string num(long long v);                                   // 1234567 -> "1,234,567"
std::string perSecond(double v);
bool parseAmount(const std::string& text, long long& out);   // "2000", "2,000", "2k", "1.5m" (positive whole numbers)                              // 12.34 -> "12.3", 26100000 -> "26.1M"
std::string compact(long long v);
// A block chart, `height` rows tall (top row first), one column per value, on a log scale so
// a run that grew from 100 to 100B still shows its shape. Uses the ▁▂▃▄▅▆▇█ characters.
std::vector<std::string> logChart(const std::vector<long long>& values, int height);                               // 1234567 -> "1.23M", 950 -> "950"
std::string bar(long long cur, long long target, int width = 10); // "[####------]"
bool parseLongLong(const std::string& s, long long& out);
std::string fixed(double v, int decimals);                      // fixed(1.5, 2) -> "1.50"
std::string percent(double fraction, int decimals = 1);         // percent(0.052) -> "5.2%"
std::string duration(long long seconds);                        // 125 -> "2m 05s"
std::string count(long long n, const std::string& word);

// Token amounts stop at a quintillion. long long overflows at about 9.2 quintillion, and a
// late game with Resonance really does get there, so every computed amount goes through here.
inline constexpr long long TOKEN_CAP = 1'000'000'000'000'000'000LL;
long long toTokens(double v);                     // rounds, and clamps to [0, TOKEN_CAP]
long long addTokens(long long a, long long b);    // a + b, clamped to TOKEN_CAP        // count(1, "shard") -> "1 shard", 3 -> "3 shards"

// Restores stream flags/precision when it goes out of scope.
struct StreamStateGuard {
    std::ios& s;
    std::ios::fmtflags f;
    std::streamsize p;
    explicit StreamStateGuard(std::ios& s_) : s(s_), f(s_.flags()), p(s_.precision()) {}
    ~StreamStateGuard() { s.flags(f); s.precision(p); }
};

// Sleeps for `d`, waking early if stop is requested. Returns false when the thread should exit.
bool sleepFor(const std::stop_token& st, std::chrono::milliseconds d);
