#include "term.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>

namespace {
// The Console runs commands on its own thread, printing into its own buffer, and says whether
// that thread gets colors. Anything else printing (nothing, normally) gets plain text.
thread_local std::ostream* t_out = nullptr;
thread_local std::istream* t_in = nullptr;
thread_local bool t_color = false;

std::atomic<int> g_theme{0};

// The original look: the terminal's own palette, whatever it is.
const char* classicCode(Style st) {
    switch (st) {
        case Style::Plain:  return "";
        case Style::Header: return "\x1b[1;36m";        // bold cyan
        case Style::Good:   return "\x1b[32m";          // green
        case Style::Bad:    return "\x1b[31m";          // red
        case Style::Warn:   return "\x1b[33m";          // yellow
        case Style::Event:  return "\x1b[1;35m";        // bold magenta
        case Style::Muted:  return "\x1b[90m";          // gray
        case Style::Tokens: return "\x1b[38;5;220m";    // gold
        case Style::Shards: return "\x1b[38;5;81m";     // light blue
        case Style::Heat:   return "\x1b[38;5;208m";    // orange
        case Style::Bold:   return "\x1b[1m";
    }
    return "";
}

std::string rgbCode(Rgb c, bool bold) {
    return std::string(bold ? "\x1b[1;38;2;" : "\x1b[38;2;") + std::to_string(c.r) + ";" + std::to_string(c.g) + ";" +
           std::to_string(c.b) + "m";
}

std::string codeFor(int theme, Style st) {
    if (themeClassic(theme)) return classicCode(st);
    auto hue = [&](Hue h, bool bold = false) { return rgbCode(themeRgb(theme, h), bold); };
    switch (st) {
        case Style::Plain:  return "";
        case Style::Header: return hue(Hue::Accent, true);
        case Style::Good:   return hue(Hue::Good);
        case Style::Bad:    return hue(Hue::Bad);
        case Style::Warn:   return hue(Hue::Warn);
        case Style::Event:  return hue(Hue::Event, true);
        case Style::Muted:  return hue(Hue::Muted);
        case Style::Tokens: return hue(Hue::Tokens);
        case Style::Shards: return hue(Hue::Shards);
        case Style::Heat:   return hue(Hue::Heat);
        case Style::Bold:   return "\x1b[1m";
    }
    return "";
}
}  // namespace

void setTheme(int theme) { g_theme = std::clamp(theme, 0, CUSTOM_THEME); }
int currentTheme() { return g_theme; }

bool colorOn() { return t_color; }

std::ostream& out() { return t_out ? *t_out : std::cout; }
std::istream& in() { return t_in ? *t_in : std::cin; }

void redirectThisThread(std::ostream* o, std::istream* i, bool color) {
    t_out = o;
    t_in = i;
    t_color = o && color;
}
size_t visibleWidth(const std::string& s) {
    size_t width = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0x1b && i + 1 < s.size() && s[i + 1] == '[') {   // skip "ESC [ ... letter"
            i += 2;
            while (i < s.size() && !std::isalpha((unsigned char)s[i])) ++i;
            continue;
        }
        if ((c & 0xC0) != 0x80) ++width;   // count only the first byte of each UTF-8 character
    }
    return width;
}

std::string paint(Style st, const std::string& text) { return paintIn(g_theme, st, text); }

std::string paintIn(int theme, Style st, const std::string& text) {
    if (!colorOn() || st == Style::Plain || text.empty()) return text;
    return codeFor(theme, st) + text + "\x1b[0m";
}

std::string paintHue(Hue h, const std::string& text, bool bold) {
    if (!colorOn() || text.empty()) return text;
    if (!themeClassic(g_theme)) return rgbCode(themeRgb(g_theme, h), bold) + text + "\x1b[0m";
    static const char* const classic[HUE_COUNT] = {
        "39", "90", "36", "32", "31", "33", "35", "38;5;220", "38;5;81", "38;5;208",   // Text .. Heat
        "49", "48;5;236", "44", "97", "100", "37", "38;5;154",                        // Background .. Infected
    };
    return std::string("\x1b[") + (bold ? "1;" : "") + classic[(int)h] + "m" + text + "\x1b[0m";
}

std::string paintRgb(Rgb color, const std::string& text, bool bold) {
    if (!colorOn() || text.empty()) return text;
    return rgbCode(color, bold) + text + "\x1b[0m";
}

std::string paint256(int color, const std::string& text, bool bold) {
    if (!colorOn() || text.empty()) return text;
    return std::string(bold ? "\x1b[1;38;5;" : "\x1b[38;5;") + std::to_string(color) + "m" + text + "\x1b[0m";
}

Style styleFor(Sev sev) {
    switch (sev) {
        case Sev::Info:  return Style::Plain;
        case Sev::Good:  return Style::Good;
        case Sev::Bad:   return Style::Bad;
        case Sev::Warn:  return Style::Warn;
        case Sev::Event: return Style::Event;
    }
    return Style::Plain;
}

std::string padRight(const std::string& s, size_t width) {
    return s.size() >= width ? s : s + std::string(width - s.size(), ' ');
}

std::string padLeft(const std::string& s, size_t width) {
    return s.size() >= width ? s : std::string(width - s.size(), ' ') + s;
}

std::string padVisible(const std::string& painted, size_t width) {
    size_t w = visibleWidth(painted);
    return w >= width ? painted : painted + std::string(width - w, ' ');
}

void printNotes(const Notes& notes) {
    for (const Note& n : notes) out() << paint(styleFor(n.sev), n.text) << "\n";
}

void printEntry(const LogEntry& e) {
    out() << "  " << paint(Style::Muted, e.stamp) << "  " << paint(styleFor(e.sev), e.text) << "\n";
}

std::string sparkline(const std::deque<double>& values) {
    static const char* const blocks[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    if (values.empty()) return "";
    auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    double span = *hi - *lo;
    std::string out;
    for (double v : values) {
        int i = span <= 0.0 ? 3 : (int)((v - *lo) / span * 7.0 + 0.5);
        out += blocks[std::clamp(i, 0, 7)];
    }
    return out;
}

std::string heatBar(int heat, int max) {
    std::string out;
    for (int i = 0; i < max; ++i) out += i < heat ? "■" : "□";
    return out;
}
