#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>

#include "core/choices.hpp"
#include "core/contracts.hpp"
#include "core/disasters.hpp"
#include "core/game.hpp"
#include "core/save.hpp"
#include "core/util.hpp"
#include "shell/term.hpp"
#include "ui.hpp"

using namespace ftxui;

namespace tui {

// ----- Ui -----
Ui::Ui(Shared& shared) : s(shared), rng(makeSeed()) {}

void Ui::snapshot() {
    {
        std::lock_guard lk(s.m);
        snap = s.g;
        snapTime = nowSec();
    }
    setCustomPalette(snap.customTheme);   // this profile's custom theme, for whoever draws it
    useTheme(previewTheme >= 0 ? previewTheme : snap.themeSel);   // a theme being previewed on the Themes tab wins
    setTheme(snap.themeSel);   // the Console tab prints through the shell, which keeps its own copy (never a preview)
}

void Ui::run(const std::function<Notes(GameState&)>& action) {
    Notes notes;
    {
        std::lock_guard lk(s.m);
        notes = action(s.g);
        checkContract(s.g, s.log);
        checkGoals(s.g, s.log);
    }
    for (const Note& n : notes) s.log.push(n);
}

void Ui::mineNow() {
    auto now = Clock::now();
    if (now < mineReadyAt) return;   // still cooling down: the click is ignored
    mineReadyAt = now + MINE_COOLDOWN;
    {
        std::lock_guard lk(s.m);
        lastMine = mine(s.g, nowSec(), rng);
        checkContract(s.g, s.log);
        checkDisaster(s.g, s.log);
        checkRace(s.g, s.log);
        checkGoals(s.g, s.log);
    }
    hasMined = true;
    if (lastMine.jackpot) s.log.push(Sev::Event, "JACKPOT! +" + num(lastMine.gain) + " tokens.");
}

void Ui::saveNow() {
    if (::saveNow(s)) s.log.push(Sev::Good, "Saved.");
    else              s.log.push(Sev::Bad, "Save failed.");
}

bool Ui::typing() const {
    for (const ftxui::Component& c : textBoxes)
        if (c->Focused()) return true;
    return false;
}

float Ui::mineCooldownLeft() const {
    auto left = mineReadyAt - Clock::now();
    if (left <= Clock::duration::zero()) return 0.0f;
    return (float)std::chrono::duration_cast<std::chrono::milliseconds>(left).count() /
           (float)std::chrono::duration_cast<std::chrono::milliseconds>(MINE_COOLDOWN).count();
}

// ----- Themes -----
namespace {
int g_theme = 0;   // the UI thread's; the Console's own thread colors through shell/term.cpp instead

Color classicHue(Hue h) {
    switch (h) {
        case Hue::Text:          return Color::Default;
        case Hue::Muted:         return Color::GrayLight;
        case Hue::Accent:        return Color::Cyan;
        case Hue::Good:          return Color::Green;
        case Hue::Bad:           return Color::Red;
        case Hue::Warn:          return Color::Yellow;
        case Hue::Event:         return Color::Magenta;
        case Hue::Tokens:        return Color::Gold1;
        case Hue::Shards:        return Color::DeepSkyBlue1;
        case Hue::Heat:          return Color::DarkOrange;
        case Hue::Background:    return Color::Default;
        case Hue::Panel:         return Color::Grey19;
        case Hue::Button:        return Color::Blue;
        case Hue::ButtonText:    return Color::White;
        case Hue::ButtonOff:     return Color::GrayDark;
        case Hue::ButtonOffText: return Color::GrayLight;
        case Hue::Infected:      return Color::GreenYellow;
        case Hue::Count:         break;
    }
    return Color::Default;
}
}  // namespace

// The terminal's own default background (OSC 11), so the strip of padding around the cells, which
// no program can draw on, matches the theme instead of staying black. Classic, or leaving, puts the
// terminal's own color back (OSC 111). Terminals that don't know these quietly ignore them.
static void paintBackdrop(int theme) {
    static long painted = -1;   // the color last set, 0xRRGGBB; -1: the terminal's own
    Rgb c = themeRgb(theme, Hue::Background);
    long want = themeClassic(theme) ? -1 : (long)c.r << 16 | (long)c.g << 8 | c.b;   // the custom one can change while you edit it
    if (want == painted) return;
    const char* noColor = std::getenv("NO_COLOR");
    if (noColor && *noColor) return;
    painted = want;
    if (want < 0) {
        std::cout << "\x1b]111\x1b\\" << std::flush;
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "\x1b]11;#%02x%02x%02x\x1b\\", c.r, c.g, c.b);
    std::cout << buf << std::flush;
}

void useTheme(int theme) {
    g_theme = std::clamp(theme, 0, CUSTOM_THEME);
    paintBackdrop(g_theme);
}
int currentTheme() { return g_theme; }

Color hueOf(int theme, Hue h) {
    if (themeClassic(theme)) return classicHue(h);
    Rgb c = themeRgb(theme, h);
    return Color::RGB(c.r, c.g, c.b);
}

Color hue(Hue h) { return hueOf(g_theme, h); }

Decorator muted() {
    if (themeClassic(g_theme)) return dim;
    return color(hue(Hue::Muted));
}

Decorator track() {
    if (themeClassic(g_theme)) return nothing;
    return bgcolor(hue(Hue::Panel));
}

Element themed(Element e) {
    if (themeClassic(g_theme)) return e;
    return e | color(hue(Hue::Text)) | bgcolor(hue(Hue::Background));
}

// ----- Colors and small elements -----
Color sevColor(Sev sev) {
    switch (sev) {
        case Sev::Info:  return hue(Hue::Text);
        case Sev::Good:  return hue(Hue::Good);
        case Sev::Bad:   return hue(Hue::Bad);
        case Sev::Warn:  return hue(Hue::Warn);
        case Sev::Event: return hue(Hue::Event);
    }
    return hue(Hue::Text);
}

// Columns on screen: every byte but UTF-8 continuation bytes is a character.
static int columns(const std::string& s) {
    return (int)std::count_if(s.begin(), s.end(), [](char ch) { return ((unsigned char)ch & 0xC0) != 0x80; });
}

// The first `n` characters of `s` (keeping whole UTF-8 characters).
static size_t bytesFor(const std::string& s, int n) {
    size_t i = 0;
    for (int k = 0; i < s.size() && k < n; ++k)
        do ++i; while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80);
    return i;
}

static std::vector<std::string> wrapWords(const std::string& s, int width) {
    width = std::max(width, 8);
    std::vector<std::string> lines;
    std::string line, word;
    std::istringstream in(s);
    while (in >> word) {
        while (columns(word) > width) {   // a word wider than the box: cut it
            if (!line.empty()) { lines.push_back(line); line.clear(); }
            size_t cut = bytesFor(word, width);
            lines.push_back(word.substr(0, cut));
            word.erase(0, cut);
        }
        if (word.empty()) continue;
        if (line.empty()) line = word;
        else if (columns(line) + 1 + columns(word) <= width) line += " " + word;
        else { lines.push_back(line); line = word; }
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}

Elements logLines(const LogEntry& e, int width, int maxLines) {
    // Wrapped to fit; the time only on the first line, so each message reads as one block.
    int stampW = (int)e.stamp.size() + 2;
    Elements out;
    std::vector<std::string> parts = wrapWords(e.text, width - stampW);
    if (maxLines > 0 && (int)parts.size() > maxLines) {   // no room for all of it: its start, then "..."
        parts.resize((size_t)maxLines);
        std::string& last = parts.back();
        int room = std::max(width - stampW, 8) - 3;
        if (columns(last) > room) last.resize(bytesFor(last, room));
        last += "...";
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        Element msg = text(parts[i]) | color(sevColor(e.sev));
        if (e.sev == Sev::Event) msg = msg | bold;
        out.push_back(hbox({text(i == 0 ? e.stamp + "  " : "") | muted() | size(WIDTH, EQUAL, stampW), msg}));
    }
    return out;
}

// A line with color codes, cut into rows `width` columns wide at spaces (the color carries on into
// the next row, which is indented by two). Codes take no room.
std::vector<std::string> wrapAnsi(const std::string& line, int width) {
    width = std::max(width, 20);
    std::vector<std::string> out;
    std::string cur, active, activeAtSpace;   // active: the codes in force (since the last reset)
    int curW = 0;
    size_t spaceAt = std::string::npos;       // in cur, just after the last space
    int wAtSpace = 0;
    for (size_t i = 0; i < line.size();) {
        if (line[i] == '\x1b' && i + 1 < line.size() && line[i + 1] == '[') {   // a code: keep it, track it
            size_t j = i + 2;
            while (j < line.size() && !std::isalpha((unsigned char)line[j])) ++j;
            std::string code = line.substr(i, j + 1 - i);
            if (code == "\x1b[0m" || code == "\x1b[m") active.clear();
            else active += code;
            cur += code;
            i = j + 1;
            continue;
        }
        size_t len = 1;   // one UTF-8 character, one column
        while (i + len < line.size() && ((unsigned char)line[i + len] & 0xC0) == 0x80) ++len;
        if (curW >= width) {
            std::string rest;
            if (spaceAt != std::string::npos && wAtSpace > 2) {   // break at the last space
                rest = activeAtSpace + cur.substr(spaceAt);
                cur.erase(spaceAt);
                curW -= wAtSpace;
            } else {
                rest = active;
                curW = 0;
            }
            out.push_back(cur + "\x1b[0m");
            cur = "  " + rest;
            curW += 2;
            spaceAt = std::string::npos;
        }
        cur += line.substr(i, len);
        curW += 1;
        if (line[i] == ' ') { spaceAt = cur.size(); wAtSpace = curW; activeAtSpace = active; }
        i += len;
    }
    out.push_back(cur);
    return out;
}

// Understands the codes the shell's paint() writes: reset, bold, the 16 basic colors (30-37,
// 90-97), the 256-color palette (38;5;n) and 24-bit color (38;2;r;g;b, and 48;2;r;g;b for the
// theme previews' backgrounds). Anything else is skipped.
Element ansiLine(const std::string& line) {
    Elements parts;
    std::string run;
    bool isBold = false;
    int fg16 = -1, fg256 = -1;
    long fgRgb = -1, bgRgb = -1;   // 0xRRGGBB, or -1
    auto rgb = [](long v) { return Color::RGB((uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v); };
    auto flush = [&] {
        if (run.empty()) return;
        Element e = text(run);
        if (fgRgb >= 0) e = e | color(rgb(fgRgb));
        else if (fg256 >= 0) e = e | color(Color((Color::Palette256)fg256));
        else if (fg16 >= 0) e = e | color(Color((Color::Palette16)fg16));
        if (bgRgb >= 0) e = e | bgcolor(rgb(bgRgb));
        if (isBold) e = e | bold;
        parts.push_back(e);
        run.clear();
    };
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '\x1b' || i + 1 >= line.size() || line[i + 1] != '[') { run += line[i]; continue; }
        size_t end = line.find_first_of("ABCDEFGHJKSTfmnsu", i + 2);
        if (end == std::string::npos) break;
        if (line[end] == 'm') {
            flush();
            std::vector<int> codes;
            std::string num;
            for (size_t k = i + 2; k <= end; ++k) {
                if (std::isdigit((unsigned char)line[k])) num += line[k];
                else { codes.push_back(num.empty() ? 0 : std::stoi(num)); num.clear(); }
            }
            for (size_t k = 0; k < codes.size(); ++k) {
                int c = codes[k];
                if (c == 0) { isBold = false; fg16 = fg256 = -1; fgRgb = bgRgb = -1; }
                else if (c == 1) isBold = true;
                else if (c >= 30 && c <= 37) { fg16 = c - 30; fg256 = -1; fgRgb = -1; }
                else if (c >= 90 && c <= 97) { fg16 = c - 90 + 8; fg256 = -1; fgRgb = -1; }
                else if (c == 39) { fg16 = fg256 = -1; fgRgb = -1; }
                else if (c == 49) bgRgb = -1;
                else if (c == 38 && k + 2 < codes.size() && codes[k + 1] == 5) { fg256 = codes[k + 2]; fgRgb = -1; k += 2; }
                else if ((c == 38 || c == 48) && k + 4 < codes.size() && codes[k + 1] == 2) {
                    long v = (long)(codes[k + 2] & 255) << 16 | (long)(codes[k + 3] & 255) << 8 | (long)(codes[k + 4] & 255);
                    (c == 38 ? fgRgb : bgRgb) = v;
                    k += 4;
                }
            }
        }
        i = end;
    }
    flush();
    if (parts.empty()) return text("");
    return hbox(parts);
}

Element cell(const std::string& s, int width) { return text(s) | size(WIDTH, EQUAL, width); }
Element tokens(long long n) { return text(num(n)) | color(hue(Hue::Tokens)); }
Element shards(long long n) { return text(num(n)) | color(hue(Hue::Shards)); }

Element heatBar(int heat) {
    std::string on, off;
    for (int i = 0; i < MAX_HEAT; ++i) (i < heat ? on : off) += (i < heat ? "■" : "□");
    return hbox({text(on) | color(hue(Hue::Heat)), text(off) | muted()});
}

Element sparkline(const std::deque<double>& values) {
    static const char* const blocks[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    if (values.empty()) return text("");
    auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    double span = *hi - *lo;
    std::string out;
    for (double v : values) {
        int i = span <= 0.0 ? 3 : (int)((v - *lo) / span * 7.0 + 0.5);
        out += blocks[std::clamp(i, 0, 7)];
    }
    return text(out) | color(hue(Hue::Shards));
}

Element progress(long long cur, long long target, int width) {
    cur = std::clamp(cur, 0LL, std::max(target, 1LL));
    float f = target > 0 ? (float)cur / (float)target : 1.0f;
    return hbox({gauge(f) | size(WIDTH, EQUAL, width) | color(pick(f >= 1.0f, hue(Hue::Good), hue(Hue::Tokens))) | track(),
                 text(" " + compact(cur) + "/" + compact(target))});
}

// ----- Buttons -----
// A one-line button: blue when you can use it, gray when you can't (clicking a gray one
// still runs the action, which explains in the log why it didn't work).
Component Btn(std::function<std::string()> label, std::function<void()> onClick, std::function<bool()> enabled) {
    ButtonOption opt;
    opt.on_click = std::move(onClick);
    opt.transform = [label = std::move(label), enabled = std::move(enabled)](const EntryState& st) {
        bool on = !enabled || enabled();
        Element e = text(" " + label() + " ");
        e = on ? e | bgcolor(hue(Hue::Button)) | color(hue(Hue::ButtonText)) : e | bgcolor(hue(Hue::ButtonOff)) | color(hue(Hue::ButtonOffText));
        if (st.focused) e = e | inverted | bold;
        return e;
    };
    return Button(opt);
}

Component FlatBtn(std::function<std::string()> label, std::function<void()> onClick, std::function<bool()> warn) {
    ButtonOption opt;
    opt.on_click = std::move(onClick);
    opt.transform = [label = std::move(label), warn = std::move(warn)](const EntryState& st) {
        Element e = text(label());
        bool hot = st.focused;   // (FTXUI counts the mouse hovering as focused too)
        if (warn && warn()) return e | color(hue(Hue::Bad)) | (hot ? underlined : nothing);
        return hot ? e | color(hue(Hue::Text)) | underlined : e | muted();
    };
    return Button(opt);
}

Component Btn(const std::string& label, std::function<void()> onClick, std::function<bool()> enabled) {
    return Btn([label] { return label; }, std::move(onClick), std::move(enabled));
}

// ----- Text boxes -----
namespace {

class TextBoxBase : public ComponentBase {
public:
    TextBoxBase(std::string* content, int* cursor, std::string placeholder, std::function<void()> onEnter, int width,
                std::function<std::string()> ghost)
        : content_(content), cursor_(cursor), placeholder_(std::move(placeholder)), width_(width), ghost_(std::move(ghost)) {
        InputOption opt;
        opt.multiline = false;
        opt.cursor_position = cursor;
        if (onEnter) opt.on_enter = std::move(onEnter);
        Add(Input(content, opt));   // it handles the typing; we only draw
    }

    Element OnRender() override {
        bool on = Focused();
        const std::string& s = *content_;
        Element e;
        if (s.empty() && !on) {
            e = text(placeholder_) | muted();
        } else {
            size_t at = (size_t)std::clamp(*cursor_, 0, (int)s.size());
            size_t len = at < s.size() ? 1 : 0;
            while (at + len < s.size() && ((unsigned char)s[at + len] & 0xC0) == 0x80) ++len;   // a whole UTF-8 character
            // A suggestion (at the end of the line only): greyed out after the cursor, the cursor on its first letter.
            std::string ghost = on && ghost_ && at == s.size() ? ghost_() : "";
            if (!ghost.empty()) {
                Color dim = ghostColor();
                e = hbox({text(s), text(ghost.substr(0, 1)) | color(dim) | inverted, text(ghost.substr(1)) | color(dim)});
            } else {
                std::string under = len ? s.substr(at, len) : " ";
                e = hbox({text(s.substr(0, at)), on ? text(under) | inverted : text(under), text(s.substr(at + len))});
            }
        }
        if (width_ > 0) e = e | size(WIDTH, EQUAL, width_);
        return e | reflect(box_);
    }

    bool OnEvent(Event event) override {
        if (event.is_mouse() && event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Pressed &&
            box_.Contain(event.mouse().x, event.mouse().y)) {
            TakeFocus();
            return true;
        }
        return ComponentBase::OnEvent(event);
    }

private:
    // The text color, halfway to the background: there, but quieter than what's typed.
    static Color ghostColor() {
        Rgb t = themeRgb(currentTheme(), Hue::Text), b = themeRgb(currentTheme(), Hue::Background);
        auto mix = [](unsigned char x, unsigned char y) { return (uint8_t)((x * 45 + y * 55) / 100); };
        return Color::RGB(mix(t.r, b.r), mix(t.g, b.g), mix(t.b, b.b));
    }

    std::string* content_;
    int* cursor_;
    std::string placeholder_;
    int width_;
    std::function<std::string()> ghost_;
    Box box_;
};

}  // namespace

Component TextBox(std::string* content, int* cursor, std::string placeholder, std::function<void()> onEnter, int width,
                  std::function<std::string()> ghost) {
    return Make<TextBoxBase>(content, cursor, std::move(placeholder), std::move(onEnter), width, std::move(ghost));
}

// ----- Grids -----
namespace {

class GridBase : public ComponentBase {
public:
    explicit GridBase(Components rows) { Add(Container::Vertical(std::move(rows))); }

    bool OnEvent(Event event) override {
        Component before = ChildAt(0)->ActiveChild();
        bool handled = ComponentBase::OnEvent(event);
        Component row = ChildAt(0)->ActiveChild();
        if (!row || row->ChildCount() < 2) return handled;
        bool moved = handled && row != before && !event.is_mouse() && event != Event::Tab && event != Event::TabReverse;
        if (moved) land(row);            // up or down: keep the column
        else want_ = column(row);        // left, right, a click: that's the column now
        return handled;
    }

private:
    static int column(const Component& row) {
        for (size_t i = 0; i < row->ChildCount(); ++i)
            if (row->ChildAt(i) == row->ActiveChild()) return (int)i;
        return 0;
    }

    // The wanted column, or the nearest one that can take focus. `want_` stays as it was, so
    // passing through a shorter row and back lands on the old column again.
    void land(const Component& row) const {
        int n = (int)row->ChildCount();
        for (int d = 0; d < n; ++d)
            for (int c : {want_ - d, want_ + d})
                if (c >= 0 && c < n && row->ChildAt((size_t)c)->Focusable()) {
                    row->SetActiveChild(row->ChildAt((size_t)c));
                    return;
                }
    }

    int want_ = 0;
};

}  // namespace

Component Grid(Components rows) { return Make<GridBase>(std::move(rows)); }

// The big one. The gauge refills over the cooldown; clicks during the cooldown do nothing.
Component MineButton(Ui& ui) {
    ButtonOption opt;
    opt.on_click = [&ui] { ui.mineNow(); };
    opt.transform = [&ui](const EntryState& st) {
        float left = ui.mineCooldownLeft();
        Color c = left <= 0.0f ? hue(Hue::Tokens) : hue(Hue::ButtonOff);
        Element face = vbox({
            text("        MINE        ") | bold,
            gauge(1.0f - left) | color(c),
        }) | borderRounded | color(c);
        if (st.focused) face = face | inverted;
        return face;
    };
    return Button(opt);
}

}  // namespace tui
