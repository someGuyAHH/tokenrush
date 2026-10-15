#include <algorithm>
#include <cctype>

#include "core/contracts.hpp"
#include "core/game.hpp"
#include "core/save.hpp"
#include "core/util.hpp"
#include "ui.hpp"

using namespace ftxui;

namespace tui {

// ----- Ui -----
Ui::Ui(Shared& shared) : s(shared), rng(makeSeed()) {}

void Ui::snapshot() {
    std::lock_guard lk(s.m);
    snap = s.g;
    snapTime = nowSec();
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

// ----- Colors and small elements -----
Color sevColor(Sev sev) {
    switch (sev) {
        case Sev::Info:  return Color::Default;
        case Sev::Good:  return Color::Green;
        case Sev::Bad:   return Color::Red;
        case Sev::Warn:  return Color::Yellow;
        case Sev::Event: return Color::Magenta;
    }
    return Color::Default;
}

Element logLine(const LogEntry& e) {
    Element msg = text(e.text) | color(sevColor(e.sev));
    if (e.sev == Sev::Event) msg = msg | bold;
    return hbox({text(e.stamp + "  ") | dim, msg});
}

// Understands the codes the shell's paint() writes: reset, bold, the 16 basic colors (30-37,
// 90-97) and the 256-color palette (38;5;n). Anything else is skipped.
Element ansiLine(const std::string& line) {
    Elements parts;
    std::string run;
    bool isBold = false;
    int fg16 = -1, fg256 = -1;
    auto flush = [&] {
        if (run.empty()) return;
        Element e = text(run);
        if (fg256 >= 0) e = e | color(Color((Color::Palette256)fg256));
        else if (fg16 >= 0) e = e | color(Color((Color::Palette16)fg16));
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
                if (c == 0) { isBold = false; fg16 = fg256 = -1; }
                else if (c == 1) isBold = true;
                else if (c >= 30 && c <= 37) { fg16 = c - 30; fg256 = -1; }
                else if (c >= 90 && c <= 97) { fg16 = c - 90 + 8; fg256 = -1; }
                else if (c == 39) fg16 = fg256 = -1;
                else if (c == 38 && k + 2 < codes.size() && codes[k + 1] == 5) { fg256 = codes[k + 2]; k += 2; }
            }
        }
        i = end;
    }
    flush();
    if (parts.empty()) return text("");
    return hbox(parts);
}

Element cell(const std::string& s, int width) { return text(s) | size(WIDTH, EQUAL, width); }
Element tokens(long long n) { return text(num(n)) | color(Color::Gold1); }
Element shards(long long n) { return text(num(n)) | color(Color::DeepSkyBlue1); }

Element heatBar(int heat) {
    std::string on, off;
    for (int i = 0; i < MAX_HEAT; ++i) (i < heat ? on : off) += (i < heat ? "■" : "□");
    return hbox({text(on) | color(Color::DarkOrange), text(off) | dim});
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
    return text(out) | color(Color::DeepSkyBlue1);
}

Element progress(long long cur, long long target, int width) {
    cur = std::clamp(cur, 0LL, std::max(target, 1LL));
    float f = target > 0 ? (float)cur / (float)target : 1.0f;
    return hbox({gauge(f) | size(WIDTH, EQUAL, width) | color(pick(f >= 1.0f, Color::Green, Color::Gold1)),
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
        e = on ? e | bgcolor(Color::Blue) | color(Color::White) : e | bgcolor(Color::GrayDark) | color(Color::GrayLight);
        if (st.focused) e = e | inverted | bold;
        return e;
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
    TextBoxBase(std::string* content, int* cursor, std::string placeholder, std::function<void()> onEnter, int width)
        : content_(content), cursor_(cursor), placeholder_(std::move(placeholder)), width_(width) {
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
            e = text(placeholder_) | dim;
        } else {
            size_t at = (size_t)std::clamp(*cursor_, 0, (int)s.size());
            size_t len = at < s.size() ? 1 : 0;
            while (at + len < s.size() && ((unsigned char)s[at + len] & 0xC0) == 0x80) ++len;   // a whole UTF-8 character
            std::string under = len ? s.substr(at, len) : " ";
            e = hbox({text(s.substr(0, at)), on ? text(under) | inverted : text(under), text(s.substr(at + len))});
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
    std::string* content_;
    int* cursor_;
    std::string placeholder_;
    int width_;
    Box box_;
};

}  // namespace

Component TextBox(std::string* content, int* cursor, std::string placeholder, std::function<void()> onEnter, int width) {
    return Make<TextBoxBase>(content, cursor, std::move(placeholder), std::move(onEnter), width);
}

// The big one. The gauge refills over the cooldown; clicks during the cooldown do nothing.
Component MineButton(Ui& ui) {
    ButtonOption opt;
    opt.on_click = [&ui] { ui.mineNow(); };
    opt.transform = [&ui](const EntryState& st) {
        float left = ui.mineCooldownLeft();
        Color c = Color::GrayDark;   // (an if, not a ternary: Gold1 and GrayDark are different enum types)
        if (left <= 0.0f) c = Color::Gold1;
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
