#include "util.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include <random>
#include <sstream>

std::time_t nowSec() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

unsigned makeSeed() {
    // random_device can be deterministic on some MinGW builds, so mix in the clock.
    return std::random_device{}() ^
           (unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::string num(long long v) {
    unsigned long long mag = v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v;
    std::string s = std::to_string(mag);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return v < 0 ? "-" + s : s;
}

std::vector<std::string> logChart(const std::vector<long long>& values, int height) {
    static const char* const blocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    std::vector<std::string> rows((size_t)std::max(0, height));
    if (values.empty() || height <= 0) return rows;
    std::vector<double> logs;
    for (long long v : values) logs.push_back(std::log10((double)std::max(1LL, v)));
    double lo = *std::min_element(logs.begin(), logs.end()), hi = *std::max_element(logs.begin(), logs.end());
    for (double l : logs) {
        int eighths = hi > lo ? (int)std::lround((l - lo) / (hi - lo) * height * 8) : height * 4;
        eighths = std::max(eighths, 1);   // every column shows at least a sliver
        for (int r = 0; r < height; ++r) {
            int fromBottom = height - 1 - r;
            int fill = std::clamp(eighths - fromBottom * 8, 0, 8);
            rows[(size_t)r] += blocks[fill];
        }
    }
    return rows;
}

bool parseAmount(const std::string& text, long long& out) {
    std::string t;
    for (char c : text) if (c != ',' && c != '_' && !std::isspace((unsigned char)c)) t += (char)std::tolower((unsigned char)c);
    if (t.empty()) return false;
    double mult = 1.0;
    if (t.back() == 'k') mult = 1e3;
    else if (t.back() == 'm') mult = 1e6;
    else if (t.back() == 'b') mult = 1e9;
    if (mult > 1.0) t.pop_back();
    if (t.empty() || t.find_first_not_of("0123456789.") != std::string::npos || std::count(t.begin(), t.end(), '.') > 1) return false;
    double v = std::stod(t) * mult;
    if (!(v >= 1.0) || v > 9.0e18 || v != std::floor(v)) return false;
    out = (long long)v;
    return true;
}

std::string perSecond(double v) {
    if (!(v >= 0.0)) v = 0.0;
    return v < 1000.0 ? fixed(v, 1) : compact((long long)std::min(v, 9.0e18));
}

std::string compact(long long v) {
    static const char* const units[] = {"", "K", "M", "B", "T", "Qa", "Qi"};
    double x = (double)v;
    int u = 0;
    while (std::abs(x) >= 1000.0 && u < 6) { x /= 1000.0; ++u; }
    if (u == 0) return num(v);
    int decimals = std::abs(x) < 10.0 ? 2 : std::abs(x) < 100.0 ? 1 : 0;
    std::string s = fixed(x, decimals);
    if (s.find('.') != std::string::npos) {                 // "5.50" -> "5.5", "17.0" -> "17"
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s + units[u];
}

std::string bar(long long cur, long long target, int width) {
    if (target <= 0) target = 1;
    long long filled = std::clamp<long long>(cur * width / target, 0, width);
    return "[" + std::string((size_t)filled, '#') + std::string((size_t)(width - filled), '-') + "]";
}

bool parseLongLong(const std::string& s, long long& out) {
    std::istringstream is(s);
    long long v;
    if (!(is >> v)) return false;
    out = v;
    return true;
}

std::string fixed(double v, int decimals) {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(decimals);
    os << v;
    return os.str();
}

std::string percent(double fraction, int decimals) { return fixed(fraction * 100.0, decimals) + "%"; }

std::string duration(long long seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds < 60) return std::to_string(seconds) + "s";
    std::string s = std::to_string(seconds % 60);
    if (s.size() < 2) s = "0" + s;
    return std::to_string(seconds / 60) + "m " + s + "s";
}

long long toTokens(double v) {
    if (!(v > 0.0)) return 0;                      // also catches NaN
    if (v >= (double)TOKEN_CAP) return TOKEN_CAP;
    return std::llround(v);
}

long long addTokens(long long a, long long b) {
    if (b <= 0) return a + b;
    return a >= TOKEN_CAP - b ? TOKEN_CAP : a + b;
}

std::string count(long long n, const std::string& word) { return num(n) + " " + word + (n == 1 ? "" : "s"); }

bool sleepFor(const std::stop_token& st, std::chrono::milliseconds d) {
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock lk(m);
    cv.wait_for(lk, st, d, [] { return false; });
    return !st.stop_requested();
}
