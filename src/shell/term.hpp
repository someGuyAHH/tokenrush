#pragma once
// Colors, output and small text helpers for the typed commands (the TUI's Console tab).
//
// Commands print to out() and read from in(), never to std::cout/std::cin directly: the Console
// points them at its own buffers for its thread (see redirectThisThread), and says whether that
// thread gets colors (not when NO_COLOR is set, https://no-color.org, or --no-color was passed).

#include <deque>
#include <iosfwd>
#include <string>

#include "core/state.hpp"

enum class Style { Plain, Header, Good, Bad, Warn, Event, Muted, Tokens, Shards, Heat, Bold };

bool colorOn();   // this thread prints in color
// The theme paint() draws with (an index into THEMES). Classic uses the original 16/256-color
// codes; every other theme writes 24-bit codes (ESC[38;2;r;g;bm). Safe to call from any thread.
void setTheme(int theme);
int currentTheme();
std::ostream& out();
std::istream& in();
// Points out()/in() at other streams for the calling thread only (nullptr: back to std::cout/cin, uncolored).
void redirectThisThread(std::ostream* out, std::istream* in, bool color);

size_t visibleWidth(const std::string& s); // length on screen: skips escape codes, counts UTF-8 characters

std::string paint(Style st, const std::string& text);
std::string paint256(int color, const std::string& text, bool bold = false);   // one of the 256 xterm colors
std::string paintRgb(Rgb color, const std::string& text, bool bold = false);   // 24-bit, whatever the theme
std::string paintIn(int theme, Style st, const std::string& text);             // as another theme would (previews)
std::string paintHue(Hue h, const std::string& text, bool bold = false);       // a theme role directly (the sky map)
Style styleFor(Sev sev);

std::string padRight(const std::string& s, size_t width);   // pad BEFORE painting, or escape codes break alignment
std::string padLeft(const std::string& s, size_t width);
std::string padVisible(const std::string& painted, size_t width);   // padRight for text that's already painted

void printNotes(const Notes& notes);
void printEntry(const LogEntry& e);

std::string sparkline(const std::deque<double>& values);   // "▁▂▃▅▇" style chart
std::string heatBar(int heat, int max);                     // "■■■□□□□□□□"
